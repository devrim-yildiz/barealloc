# barealloc

Modern C++20 memory allocators designed for low-latency systems and game engines.
Zero external dependencies. Single header or modular integration.

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg?style=flat-square)](https://en.cppreference.com/w/cpp/20)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=flat-square)](LICENSE)

## Overview

Standard dynamic allocation with malloc or new is often too slow or unpredictable for performance-critical code because of heap lock contention and fragmentation.

barealloc provides three focused allocation strategies:

1. **ArenaAllocator**: Linear bump allocator with constant time O(1) allocation, RAII scoped rollback markers, and instant bulk reset.
2. **PoolAllocator**: Fixed-size chunk allocator with an intrusive singly-linked free list, zero per-element memory overhead, and O(1) alloc and free.
3. **FreeListAllocator**: Variable-size allocator using Best-Fit placement with automatic block splitting and immediate adjacent coalescing to prevent external fragmentation.
4. **STLAllocator**: Standard library allocator wrapper compatible with std::allocator_traits for containers like std::vector and std::list.

## Benchmarks

Measurements taken on Windows x64 using MSVC 19.42 with Release flags (/O2).

| Benchmark | Allocator | Total Time | Throughput | Speedup |
| :--- | :--- | :--- | :--- | :--- |
| **Linear Burst** (500k chunks, 16B to 1024B) | std::malloc | 70.97 ms | 7.05 Mops/s | Baseline |
| | barealloc::Arena | 0.66 ms | 758.96 Mops/s | **107.7x** |
| **Entity Churn** (200k cycles, 64B objects) | ::operator new/delete | 11.88 ms | 16.84 Mops/s | Baseline |
| | barealloc::Pool | 1.50 ms | 133.35 Mops/s | **7.9x** |
| **Mixed Lifetimes** (100k ops, random sizes) | std::malloc | 4.30 ms | 23.24 Mops/s | Baseline |
| | barealloc::FreeList | 2.78 ms | 35.98 Mops/s | **1.5x** |

## Usage Examples

### Linear Arena with Scoped Rollback

```cpp
#include <barealloc/barealloc.hpp>

void run_frame() {
    barealloc::ArenaAllocator arena(1024 * 1024);

    void* buf = arena.allocate(4096, 64);

    struct Transform { float x, y, z; };
    Transform* t = arena.create<Transform>(1.0f, 2.0f, 3.0f);

    {
        auto scope = arena.scope();
        void* scratch = arena.allocate(8192);
    }

    arena.reset();
}
```

### Fixed-Size Pool

```cpp
#include <barealloc/barealloc.hpp>

struct Bullet {
    float pos[3];
    float velocity[3];
    float damage;
};

void update_projectiles() {
    barealloc::PoolAllocator pool(sizeof(Bullet), 50000);

    Bullet* b = pool.create<Bullet>();
    b->damage = 45.0f;

    pool.destroy(b);
}
```

### STL Container Integration

```cpp
#include <barealloc/barealloc.hpp>
#include <vector>

void use_with_vector() {
    barealloc::ArenaAllocator arena(1024 * 64);

    using FastVec = std::vector<int, barealloc::STLAllocator<int, barealloc::ArenaAllocator>>;
    FastVec items{barealloc::STLAllocator<int, barealloc::ArenaAllocator>(arena)};

    for (int i = 0; i < 1000; ++i) {
        items.push_back(i);
    }
}
```

## Alignment and Hardware Details

* Alignment offsets use power-of-two bit masking: `(addr + (align - 1)) & ~(align - 1)`.
* Supports standard 8-byte, 16-byte SSE, 32-byte AVX2, and 64-byte AVX-512 cache-line alignments.
* Built-in runtime telemetry tracking: total allocated bytes, active bytes, peak usage, and allocation counts.

## Building

Requires a C++20 compiler and CMake 3.20 or newer.

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

./build/Release/barealloc_tests
./build/Release/barealloc_benchmarks
./build/Release/barealloc_demo
```

## License

MIT License. See LICENSE file for details.
