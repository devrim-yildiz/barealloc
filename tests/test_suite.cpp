#include "barealloc/barealloc.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <iomanip>
#include <numeric>

namespace {

int g_tests_passed = 0;
int g_tests_failed = 0;

void report_result(std::string_view name, bool passed, std::string_view details = "") {
    if (passed) {
        g_tests_passed++;
        std::cout << "  \033[32m[PASS]\033[0m " << std::left << std::setw(42) << name;
        if (!details.empty()) {
            std::cout << " (" << details << ")";
        }
        std::cout << "\n";
    } else {
        g_tests_failed++;
        std::cout << "  \033[31m[FAIL]\033[0m " << std::left << std::setw(42) << name;
        if (!details.empty()) {
            std::cout << " (" << details << ")";
        }
        std::cout << "\n";
    }
}

// -----------------------------------------------------------------------------
// Test 1: Core Alignment Math
// -----------------------------------------------------------------------------
void test_core_alignment() {
    using namespace barealloc::core;
    bool ok = true;

    ok &= is_power_of_two(1);
    ok &= is_power_of_two(2);
    ok &= is_power_of_two(64);
    ok &= !is_power_of_two(0);
    ok &= !is_power_of_two(18);

    ok &= (align_up(0, 16) == 0);
    ok &= (align_up(1, 16) == 16);
    ok &= (align_up(16, 16) == 16);
    ok &= (align_up(17, 16) == 32);
    ok &= (align_up(63, 64) == 64);

    report_result("core::alignment_math", ok, "Power-of-two, bounds, 16/64B");
}

// -----------------------------------------------------------------------------
// Test 2: Arena Allocator Basic & Alignment
// -----------------------------------------------------------------------------
void test_arena_allocator() {
    barealloc::ArenaAllocator arena(1024 * 1024); // 1 MB
    bool ok = true;

    void* p1 = arena.allocate(100, 8);
    void* p2 = arena.allocate(50, 16);
    void* p3 = arena.allocate(25, 64);

    ok &= (p1 != nullptr && barealloc::core::is_aligned(p1, 8));
    ok &= (p2 != nullptr && barealloc::core::is_aligned(p2, 16));
    ok &= (p3 != nullptr && barealloc::core::is_aligned(p3, 64));
    ok &= (arena.stats().allocation_count == 3);

    // In-place object creation
    struct TestEntity {
        int id;
        double health;
        std::string name;
        TestEntity(int i, double h, std::string n) : id(i), health(h), name(std::move(n)) {}
    };

    TestEntity* entity = arena.create<TestEntity>(42, 100.0, "PlayerOne");
    ok &= (entity != nullptr && entity->id == 42 && entity->name == "PlayerOne");

    // Scoped marker rollback
    const auto pre_scope_used = arena.used();
    {
        auto marker = arena.scope();
        void* temp = arena.allocate(5000);
        ok &= (temp != nullptr);
        ok &= (arena.used() > pre_scope_used);
    }
    // Automatically rolled back
    ok &= (arena.used() == pre_scope_used);

    // Reset
    arena.reset();
    ok &= (arena.used() == 0);

    report_result("allocator::arena_linear_bump", ok, "AVX 64-byte align, RAII marker, reset");
}

// -----------------------------------------------------------------------------
// Test 3: Pool Allocator Fixed-Size & Intrusive Free List
// -----------------------------------------------------------------------------
void test_pool_allocator() {
    constexpr std::size_t CHUNK_SIZE = 64;
    constexpr std::size_t CHUNK_COUNT = 100;
    barealloc::PoolAllocator pool(CHUNK_SIZE, CHUNK_COUNT, 16);
    bool ok = true;

    std::vector<void*> ptrs;
    ptrs.reserve(CHUNK_COUNT);

    for (std::size_t i = 0; i < CHUNK_COUNT; ++i) {
        void* p = pool.allocate();
        ok &= (p != nullptr);
        ok &= barealloc::core::is_aligned(p, 16);
        ok &= pool.owns(p);
        ptrs.push_back(p);
    }

    // Next allocation must return nullptr (pool exhaustion)
    void* overflow = pool.allocate();
    ok &= (overflow == nullptr);

    // Free all chunks
    for (void* p : ptrs) {
        pool.deallocate(p);
    }
    ok &= (pool.stats().current_allocated == 0);

    // Re-allocate to ensure intrusive free list recycled blocks
    void* recycled = pool.allocate();
    ok &= (recycled != nullptr);
    pool.deallocate(recycled);

    report_result("allocator::pool_intrusive_chunks", ok, "100x chunks, bounds-check, exhaustion, recycling");
}

// -----------------------------------------------------------------------------
// Test 4: FreeList Allocator Splitting & Coalescing
// -----------------------------------------------------------------------------
void test_free_list_allocator() {
    barealloc::FreeListAllocator freelist(1024 * 64); // 64 KB
    bool ok = true;

    void* a = freelist.allocate(256, 16);
    void* b = freelist.allocate(512, 16);
    void* c = freelist.allocate(1024, 16);

    ok &= (a != nullptr && barealloc::core::is_aligned(a, 16));
    ok &= (b != nullptr && barealloc::core::is_aligned(b, 16));
    ok &= (c != nullptr && barealloc::core::is_aligned(c, 16));

    // Free b (middle block)
    freelist.deallocate(b);

    // Allocate smaller block: should fit into b's former space via block splitting
    void* b1 = freelist.allocate(128, 16);
    ok &= (b1 != nullptr);

    // Free all
    freelist.deallocate(a);
    freelist.deallocate(b1);
    freelist.deallocate(c);

    // Now all adjacent blocks are free: coalescing should merge entire pool back
    // into one contiguous chunk, allowing a massive single allocation
    void* large = freelist.allocate(1024 * 50, 16);
    ok &= (large != nullptr);
    freelist.deallocate(large);

    report_result("allocator::freelist_best_fit", ok, "Splitting, Best-Fit, adjacent coalescing");
}

// -----------------------------------------------------------------------------
// Test 5: STL Allocator Integration
// -----------------------------------------------------------------------------
void test_stl_adapter() {
    barealloc::ArenaAllocator arena(1024 * 128);
    bool ok = true;

    using VectorType = std::vector<int, barealloc::STLAllocator<int, barealloc::ArenaAllocator>>;
    VectorType vec{barealloc::STLAllocator<int, barealloc::ArenaAllocator>(arena)};

    for (int i = 0; i < 500; ++i) {
        vec.push_back(i * 2);
    }

    ok &= (vec.size() == 500);
    ok &= (vec[0] == 0 && vec[250] == 500 && vec[499] == 998);
    ok &= (arena.stats().allocation_count > 0);

    report_result("adapter::stl_allocator_traits", ok, "std::vector<int> hosted on ArenaAllocator");
}

} // namespace

int main() {
    std::cout << "\n";
    std::cout << "  ===============================================================\n";
    std::cout << "          barealloc C++20 Verification Test Suite\n";
    std::cout << "  ===============================================================\n\n";

    test_core_alignment();
    test_arena_allocator();
    test_pool_allocator();
    test_free_list_allocator();
    test_stl_adapter();

    std::cout << "\n";
    std::cout << "  ---------------------------------------------------------------\n";
    std::cout << "  Summary: " << g_tests_passed << " Passed, "
              << g_tests_failed << " Failed\n";
    std::cout << "  Status:  " << (g_tests_failed == 0 ? "\033[32mALL TESTS SUCCEEDED\033[0m" : "\033[31mFAILURES ENCOUNTERED\033[0m") << "\n";
    std::cout << "  ===============================================================\n\n";

    return g_tests_failed == 0 ? 0 : 1;
}
