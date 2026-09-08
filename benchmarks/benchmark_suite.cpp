#include "barealloc/barealloc.hpp"

#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <iomanip>
#include <string>
#include <numeric>

namespace {

using Clock = std::chrono::high_resolution_clock;
using DurationMs = std::chrono::duration<double, std::milli>;
using DurationNs = std::chrono::duration<double, std::nano>;

void print_banner() {
    std::cout << "\033[1;36m";
    std::cout << R"(
  ____                     _ _            
 | __ )  __ _ _ __ ___  __ _| | | ___   ___ 
 |  _ \ / _` | '__/ _ \/ _` | | |/ _ \ / __|
 | |_) | (_| | | |  __/ (_| | | | (_) | (__ 
 |____/ \__,_|_|  \___|\__,_|_|_|\___/ \___|
    High-Performance Memory Allocator Benchmark Suite
    Architecture: Modern C++20 | Zero-Dependency
)" << "\033[0m\n";
}

void print_table_header(std::string_view title) {
    std::cout << "\n\033[1;33m>>> " << title << "\033[0m\n";
    std::cout << "+-----------------------+---------------+---------------+---------------+\n";
    std::cout << "| Allocator Strategy    | Total Time    | Throughput    | Speedup       |\n";
    std::cout << "+-----------------------+---------------+---------------+---------------+\n";
}

void print_table_row(std::string_view name, double elapsed_ms, std::size_t ops, double baseline_ms) {
    const double mops = (ops / (elapsed_ms / 1000.0)) / 1'000'000.0;
    const double speedup = baseline_ms / elapsed_ms;

    std::cout << "| " << std::left << std::setw(21) << name
              << " | " << std::right << std::setw(10) << std::fixed << std::setprecision(2) << elapsed_ms << " ms"
              << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(2) << mops << " Mops/s"
              << " | ";

    if (speedup >= 1.05) {
        std::cout << "\033[1;32m" << std::right << std::setw(11) << std::fixed << std::setprecision(2) << speedup << "x\033[0m";
    } else if (speedup >= 0.95) {
        std::cout << "\033[37m" << std::right << std::setw(11) << "1.00x (baseline)" << "\033[0m";
    } else {
        std::cout << "\033[1;31m" << std::right << std::setw(11) << std::fixed << std::setprecision(2) << speedup << "x\033[0m";
    }
    std::cout << " |\n";
}

void print_table_footer() {
    std::cout << "+-----------------------+---------------+---------------+---------------+\n";
}

// ============================================================================
// Benchmark 1: Variable-Size Linear Burst Allocation (Arena vs std::malloc)
// ============================================================================
void benchmark_arena_vs_malloc(std::size_t count) {
    // Generate variable allocation sizes (16 bytes up to 1024 bytes)
    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<std::size_t> dist(16, 1024);

    std::vector<std::size_t> sizes(count);
    std::size_t total_memory = 0;
    for (std::size_t i = 0; i < count; ++i) {
        sizes[i] = dist(rng);
        total_memory += sizes[i] + 32; // Include overhead estimate
    }

    print_table_header("Benchmark 1: Linear Burst Allocations (" + std::to_string(count) + " Variable Chunks, 16B - 1024B)");

    // Baseline: std::malloc
    std::vector<void*> malloc_ptrs(count, nullptr);
    auto t0 = Clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        malloc_ptrs[i] = std::malloc(sizes[i]);
    }
    auto t1 = Clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        std::free(malloc_ptrs[i]);
    }
    const double malloc_ms = DurationMs(t1 - t0).count();
    print_table_row("std::malloc / free", malloc_ms, count, malloc_ms);

    // Barealloc: ArenaAllocator
    barealloc::ArenaAllocator arena(total_memory * 2);
    t0 = Clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        [[maybe_unused]] void* p = arena.allocate(sizes[i], 16);
    }
    t1 = Clock::now();
    const double arena_ms = DurationMs(t1 - t0).count();
    print_table_row("barealloc::Arena", arena_ms, count, malloc_ms);

    print_table_footer();
}

// ============================================================================
// Benchmark 2: Fixed-Size Object Cycle (Pool vs operator new/delete)
// ============================================================================
struct Projectile {
    float pos[3]{0.0f, 0.0f, 0.0f};
    float vel[3]{1.0f, 0.0f, 0.0f};
    float damage{25.0f};
    int id{0};
    char pad[32]{}; // 64 bytes total
};
static_assert(sizeof(Projectile) == 64);

void benchmark_pool_vs_new(std::size_t iterations) {
    constexpr std::size_t BATCH_SIZE = 50'000;
    print_table_header("Benchmark 2: Homogeneous Entity Churn (" + std::to_string(iterations) + " Alloc/Dealloc Cycles, 64B each)");

    // Baseline: new / delete
    auto t0 = Clock::now();
    for (std::size_t iter = 0; iter < iterations / BATCH_SIZE; ++iter) {
        std::vector<Projectile*> objs;
        objs.reserve(BATCH_SIZE);
        for (std::size_t i = 0; i < BATCH_SIZE; ++i) {
            objs.push_back(new Projectile());
        }
        for (auto* obj : objs) {
            delete obj;
        }
    }
    auto t1 = Clock::now();
    const double new_ms = DurationMs(t1 - t0).count();
    print_table_row("::operator new/delete", new_ms, iterations, new_ms);

    // Barealloc: PoolAllocator
    barealloc::PoolAllocator pool(sizeof(Projectile), BATCH_SIZE, alignof(Projectile));
    t0 = Clock::now();
    for (std::size_t iter = 0; iter < iterations / BATCH_SIZE; ++iter) {
        std::vector<Projectile*> objs;
        objs.reserve(BATCH_SIZE);
        for (std::size_t i = 0; i < BATCH_SIZE; ++i) {
            objs.push_back(pool.create<Projectile>());
        }
        for (auto* obj : objs) {
            pool.destroy(obj);
        }
    }
    t1 = Clock::now();
    const double pool_ms = DurationMs(t1 - t0).count();
    print_table_row("barealloc::Pool", pool_ms, iterations, new_ms);

    print_table_footer();
}

// ============================================================================
// Benchmark 3: Variable-Sized Mixed Workload with Fragmentation & Coalescing
// ============================================================================
void benchmark_freelist_vs_malloc(std::size_t operations) {
    std::mt19937_64 rng(42);
    // Bimodal distribution: 75% small (32-128B), 20% medium (128-1024B), 5% large (1024-4096B)
    std::uniform_int_distribution<int> type_dist(1, 100);
    std::uniform_int_distribution<std::size_t> small_dist(32, 128);
    std::uniform_int_distribution<std::size_t> med_dist(128, 1024);
    std::uniform_int_distribution<std::size_t> large_dist(1024, 4096);

    std::vector<std::size_t> request_sizes(operations);
    for (std::size_t i = 0; i < operations; ++i) {
        const int roll = type_dist(rng);
        if (roll <= 75) request_sizes[i] = small_dist(rng);
        else if (roll <= 95) request_sizes[i] = med_dist(rng);
        else request_sizes[i] = large_dist(rng);
    }

    print_table_header("Benchmark 3: Dynamic Mixed Lifetimes & Coalescing (" + std::to_string(operations) + " Ops)");

    // Windowed allocate and interleaved free
    constexpr std::size_t WINDOW = 2500;

    // Baseline: malloc / free
    auto t0 = Clock::now();
    {
        std::vector<void*> active(WINDOW, nullptr);
        for (std::size_t i = 0; i < operations; ++i) {
            const auto slot = i % WINDOW;
            if (active[slot]) {
                std::free(active[slot]);
            }
            active[slot] = std::malloc(request_sizes[i]);
        }
        for (void* p : active) {
            if (p) std::free(p);
        }
    }
    auto t1 = Clock::now();
    const double malloc_ms = DurationMs(t1 - t0).count();
    print_table_row("std::malloc / free", malloc_ms, operations, malloc_ms);

    // Barealloc: FreeListAllocator
    barealloc::FreeListAllocator freelist(64 * 1024 * 1024); // 64 MB heap
    t0 = Clock::now();
    {
        std::vector<void*> active(WINDOW, nullptr);
        for (std::size_t i = 0; i < operations; ++i) {
            const auto slot = i % WINDOW;
            if (active[slot]) {
                freelist.deallocate(active[slot]);
            }
            active[slot] = freelist.allocate(request_sizes[i], 16);
        }
        for (void* p : active) {
            if (p) freelist.deallocate(p);
        }
    }
    t1 = Clock::now();
    const double freelist_ms = DurationMs(t1 - t0).count();
    print_table_row("barealloc::FreeList", freelist_ms, operations, malloc_ms);

    print_table_footer();
}

} // namespace

int main() {
    print_banner();

    std::cout << "  [Config] OS: Windows x64 | Compiler: MSVC C++20 | Workload: Dynamic Pseudo-random\n";
    std::cout << "  Starting benchmarks...\n";

    benchmark_arena_vs_malloc(500'000);
    benchmark_pool_vs_new(200'000);
    benchmark_freelist_vs_malloc(100'000);

    std::cout << "\n\033[1;32m[SUCCESS]\033[0m All benchmark suites finished successfully.\n\n";
    return 0;
}
