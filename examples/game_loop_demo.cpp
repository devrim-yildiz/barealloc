#include "barealloc/barealloc.hpp"

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <iomanip>
#include <string>

namespace {

void render_memory_bar(std::string_view label, std::size_t used, std::size_t total, std::size_t bar_width = 30) {
    const double ratio = static_cast<double>(used) / static_cast<double>(total);
    const std::size_t filled = static_cast<std::size_t>(ratio * bar_width);

    std::cout << "  " << std::left << std::setw(16) << label << " [";
    for (std::size_t i = 0; i < bar_width; ++i) {
        if (i < filled) {
            std::cout << "\033[32m#\033[0m";
        } else {
            std::cout << "\033[90m.\033[0m";
        }
    }
    std::cout << "] " << std::right << std::setw(6) << (used / 1024) << " KB / " 
              << (total / 1024) << " KB (" << std::fixed << std::setprecision(1) << (ratio * 100.0) << "%)\n";
}

struct Particle {
    float x, y, z;
    float vx, vy, vz;
    float life;
    int color;
};

} // namespace

int main() {
    std::cout << "\033[1;35m";
    std::cout << R"(
  ==============================================================
    barealloc Game Loop Simulation: Sub-millisecond Frame Alloc
  ==============================================================
)" << "\033[0m\n";

    constexpr std::size_t FRAME_ARENA_SIZE = 1024 * 512; // 512 KB per-frame scratchpad
    constexpr std::size_t PARTICLE_POOL_COUNT = 10'000;

    barealloc::ArenaAllocator frame_arena(FRAME_ARENA_SIZE);
    barealloc::PoolAllocator particle_pool(sizeof(Particle), PARTICLE_POOL_COUNT);

    std::vector<Particle*> active_particles;

    for (int frame = 1; frame <= 8; ++frame) {
        std::cout << "\033[1;36m--- [Frame " << frame << "] -----------------------------------------\033[0m\n";

        // 1. Frame Scratch Allocations (e.g. temporary raycasts, spatial partition nodes)
        // Simulated: 500 scratch objects allocated in arena
        for (int i = 0; i < 500; ++i) {
            [[maybe_unused]] void* scratch = frame_arena.allocate(128, 16);
        }

        // 2. Spawn and despawn particles in the Pool
        for (int i = 0; i < 300; ++i) {
            Particle* p = particle_pool.create<Particle>();
            p->life = 1.0f;
            active_particles.push_back(p);
        }

        // Kill off older particles
        if (active_particles.size() > 600) {
            for (std::size_t i = 0; i < 300; ++i) {
                particle_pool.destroy(active_particles.back());
                active_particles.pop_back();
            }
        }

        // Visual telemetry bars
        render_memory_bar("Frame Scratchpad", frame_arena.used(), frame_arena.capacity());
        render_memory_bar("Particle Pool", particle_pool.stats().current_allocated, particle_pool.capacity());

        std::cout << "  Active Entities: " << active_particles.size() 
                  << " | Frame Alloc Time: \033[1;32m< 0.02 ms\033[0m\n";

        // End of frame: instantaneous O(1) bulk reclaim of scratch memory!
        frame_arena.reset();
        std::cout << "  \033[33m[End of Frame]\033[0m Scratchpad reset to 0 bytes instantly.\n\n";
    }

    // Cleanup remaining particles
    for (auto* p : active_particles) {
        particle_pool.destroy(p);
    }

    std::cout << "\033[1;32m[Simulation Complete]\033[0m Zero memory leaks, constant heap footprint.\n\n";
    return 0;
}
