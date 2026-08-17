#pragma once

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <string_view>

namespace barealloc::core {

/// Default system alignment, matching alignof(std::max_align_t) (typically 8 or 16 bytes).
inline constexpr std::size_t DEFAULT_ALIGNMENT = alignof(std::max_align_t);

/// Cache-line alignment for high-performance data structures avoiding false sharing.
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

/// High-resolution telemetry stats for allocator diagnostics and profiling.
struct AllocationStats {
    std::size_t total_allocated{0};     // Total cumulative bytes requested
    std::size_t current_allocated{0};   // Active bytes currently in use
    std::size_t peak_allocated{0};      // Maximum memory footprint reached
    std::size_t allocation_count{0};    // Total allocations performed
    std::size_t deallocation_count{0};  // Total deallocations performed

    [[nodiscard]] constexpr std::size_t active_allocations() const noexcept {
        return allocation_count - deallocation_count;
    }
};

/// Concept defining the fundamental contract for all barealloc memory allocators.
template <typename T>
concept AllocatorConcept = requires(T alloc, std::size_t size, std::size_t alignment, void* ptr) {
    { alloc.allocate(size, alignment) } -> std::same_as<void*>;
    { alloc.deallocate(ptr) } -> std::same_as<void>;
    { alloc.reset() } -> std::same_as<void>;
    { alloc.stats() } -> std::convertible_to<const AllocationStats&>;
};

} // namespace barealloc::core
