#pragma once

#include "barealloc/core/align.hpp"
#include "barealloc/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>
#include <algorithm>

namespace barealloc {

/**
 * @brief High-performance linear (bump) allocator.
 * 
 * Allocations are performed in O(1) time by bumping an internal offset pointer.
 * Individual deallocations are no-ops; memory is reclaimed in bulk via reset()
 * or incrementally via RAII ScopedMarker rollbacks.
 */
class ArenaAllocator {
public:
    struct Marker {
        std::size_t offset{0};
    };

    class ScopedMarker {
    public:
        explicit ScopedMarker(ArenaAllocator& arena) noexcept
            : arena_(arena), marker_(arena.get_marker()) {}

        ~ScopedMarker() noexcept {
            arena_.rollback_to(marker_);
        }

        ScopedMarker(const ScopedMarker&) = delete;
        ScopedMarker& operator=(const ScopedMarker&) = delete;
        ScopedMarker(ScopedMarker&&) = delete;
        ScopedMarker& operator=(ScopedMarker&&) = delete;

    private:
        ArenaAllocator& arena_;
        Marker marker_;
    };

    explicit ArenaAllocator(std::size_t capacity)
        : capacity_(capacity), owns_memory_(true) {
        buffer_ = static_cast<std::byte*>(std::malloc(capacity_));
        if (!buffer_) {
            throw std::bad_alloc();
        }
    }

    ArenaAllocator(void* user_buffer, std::size_t capacity) noexcept
        : buffer_(static_cast<std::byte*>(user_buffer)),
          capacity_(capacity),
          owns_memory_(false) {}

    ~ArenaAllocator() noexcept {
        if (owns_memory_ && buffer_) {
            std::free(buffer_);
        }
    }

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    ArenaAllocator(ArenaAllocator&& other) noexcept
        : buffer_(other.buffer_),
          capacity_(other.capacity_),
          offset_(other.offset_),
          owns_memory_(other.owns_memory_),
          stats_(other.stats_) {
        other.buffer_ = nullptr;
        other.capacity_ = 0;
        other.offset_ = 0;
        other.owns_memory_ = false;
    }

    ArenaAllocator& operator=(ArenaAllocator&& other) noexcept {
        if (this != &other) {
            if (owns_memory_ && buffer_) {
                std::free(buffer_);
            }
            buffer_ = other.buffer_;
            capacity_ = other.capacity_;
            offset_ = other.offset_;
            owns_memory_ = other.owns_memory_;
            stats_ = other.stats_;

            other.buffer_ = nullptr;
            other.capacity_ = 0;
            other.offset_ = 0;
            other.owns_memory_ = false;
        }
        return *this;
    }

    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = core::DEFAULT_ALIGNMENT) noexcept {
        if (size == 0) [[unlikely]] {
            return nullptr;
        }

        const auto current_address = reinterpret_cast<uintptr_t>(buffer_ + offset_);
        const auto aligned_address = core::align_up(current_address, alignment);
        const auto padding = static_cast<std::size_t>(aligned_address - current_address);

        if (offset_ + padding + size > capacity_) [[unlikely]] {
            return nullptr; // Out of memory for this arena
        }

        offset_ += padding;
        void* const payload = buffer_ + offset_;
        offset_ += size;

        // Telemetry update
        stats_.allocation_count++;
        stats_.total_allocated += size;
        stats_.current_allocated = offset_;
        stats_.peak_allocated = std::max(stats_.peak_allocated, stats_.current_allocated);

        return payload;
    }

    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* const mem = allocate(sizeof(T), alignof(T));
        if (!mem) [[unlikely]] {
            throw std::bad_alloc();
        }
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    void deallocate([[maybe_unused]] void* ptr) noexcept {
        // Individual deallocation is intentionally a no-op in a linear arena.
        stats_.deallocation_count++;
    }

    void reset() noexcept {
        offset_ = 0;
        stats_.current_allocated = 0;
    }

    [[nodiscard]] Marker get_marker() const noexcept {
        return Marker{offset_};
    }

    void rollback_to(Marker marker) noexcept {
        if (marker.offset <= offset_) {
            offset_ = marker.offset;
            stats_.current_allocated = offset_;
        }
    }

    [[nodiscard]] ScopedMarker scope() noexcept {
        return ScopedMarker(*this);
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t used() const noexcept { return offset_; }
    [[nodiscard]] std::size_t available() const noexcept { return capacity_ - offset_; }
    [[nodiscard]] const core::AllocationStats& stats() const noexcept { return stats_; }

private:
    std::byte* buffer_{nullptr};
    std::size_t capacity_{0};
    std::size_t offset_{0};
    bool owns_memory_{true};
    core::AllocationStats stats_{};
};

} // namespace barealloc
