#pragma once

#include "barealloc/core/align.hpp"
#include "barealloc/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>
#include <algorithm>
#include <cassert>

namespace barealloc {

/**
 * @brief High-performance fixed-size chunk pool allocator.
 * 
 * Uses an intrusive singly-linked free list where free nodes store the pointer
 * to the next available block directly within their unused payload.
 * Guarantees O(1) allocation and O(1) deallocation with zero external fragmentation.
 */
class PoolAllocator {
private:
    struct FreeNode {
        FreeNode* next{nullptr};
    };

public:
    PoolAllocator(std::size_t chunk_size, std::size_t chunk_count, std::size_t alignment = core::DEFAULT_ALIGNMENT)
        : chunk_count_(chunk_count),
          alignment_(std::max(alignment, alignof(FreeNode*))) {
        
        // Chunk must be at least large enough to store a pointer to the next free node
        const std::size_t min_size = std::max(sizeof(FreeNode), chunk_size);
        actual_chunk_size_ = core::align_up(min_size, alignment_);
        total_size_ = actual_chunk_size_ * chunk_count_;

        buffer_ = static_cast<std::byte*>(std::malloc(total_size_));
        if (!buffer_) {
            throw std::bad_alloc();
        }

        reset();
    }

    ~PoolAllocator() noexcept {
        if (buffer_) {
            std::free(buffer_);
        }
    }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    PoolAllocator(PoolAllocator&& other) noexcept
        : buffer_(other.buffer_),
          free_list_(other.free_list_),
          chunk_count_(other.chunk_count_),
          actual_chunk_size_(other.actual_chunk_size_),
          total_size_(other.total_size_),
          alignment_(other.alignment_),
          stats_(other.stats_) {
        other.buffer_ = nullptr;
        other.free_list_ = nullptr;
        other.chunk_count_ = 0;
        other.total_size_ = 0;
    }

    PoolAllocator& operator=(PoolAllocator&& other) noexcept {
        if (this != &other) {
            if (buffer_) {
                std::free(buffer_);
            }
            buffer_ = other.buffer_;
            free_list_ = other.free_list_;
            chunk_count_ = other.chunk_count_;
            actual_chunk_size_ = other.actual_chunk_size_;
            total_size_ = other.total_size_;
            alignment_ = other.alignment_;
            stats_ = other.stats_;

            other.buffer_ = nullptr;
            other.free_list_ = nullptr;
            other.chunk_count_ = 0;
            other.total_size_ = 0;
        }
        return *this;
    }

    [[nodiscard]] void* allocate([[maybe_unused]] std::size_t size = 0,
                                [[maybe_unused]] std::size_t alignment = core::DEFAULT_ALIGNMENT) noexcept {
        if (!free_list_) [[unlikely]] {
            return nullptr; // Pool exhausted
        }

        FreeNode* const node = free_list_;
        free_list_ = free_list_->next;

        stats_.allocation_count++;
        stats_.total_allocated += actual_chunk_size_;
        stats_.current_allocated += actual_chunk_size_;
        stats_.peak_allocated = std::max(stats_.peak_allocated, stats_.current_allocated);

        return static_cast<void*>(node);
    }

    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        static_assert(sizeof(T) <= 4096, "Type exceeds reasonable pool chunk size");
        void* const mem = allocate(sizeof(T), alignof(T));
        if (!mem) [[unlikely]] {
            throw std::bad_alloc();
        }
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    template <typename T>
    void destroy(T* ptr) noexcept {
        if (ptr) {
            ptr->~T();
            deallocate(ptr);
        }
    }

    void deallocate(void* ptr) noexcept {
        if (!ptr) [[unlikely]] {
            return;
        }

        assert(owns(ptr) && "Deallocating pointer not owned by this pool");

        auto* const node = static_cast<FreeNode*>(ptr);
        node->next = free_list_;
        free_list_ = node;

        stats_.deallocation_count++;
        if (stats_.current_allocated >= actual_chunk_size_) {
            stats_.current_allocated -= actual_chunk_size_;
        }
    }

    void reset() noexcept {
        free_list_ = nullptr;
        // Chain all chunks in intrusive free list
        for (std::size_t i = 0; i < chunk_count_; ++i) {
            auto* const node = reinterpret_cast<FreeNode*>(buffer_ + (i * actual_chunk_size_));
            node->next = free_list_;
            free_list_ = node;
        }
        stats_.current_allocated = 0;
    }

    [[nodiscard]] bool owns(const void* ptr) const noexcept {
        const auto p = reinterpret_cast<uintptr_t>(ptr);
        const auto start = reinterpret_cast<uintptr_t>(buffer_);
        const auto end = start + total_size_;
        return p >= start && p < end;
    }

    [[nodiscard]] std::size_t chunk_size() const noexcept { return actual_chunk_size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return total_size_; }
    [[nodiscard]] std::size_t active_chunks() const noexcept { return stats_.active_allocations(); }
    [[nodiscard]] const core::AllocationStats& stats() const noexcept { return stats_; }

private:
    std::byte* buffer_{nullptr};
    FreeNode* free_list_{nullptr};
    std::size_t chunk_count_{0};
    std::size_t actual_chunk_size_{0};
    std::size_t total_size_{0};
    std::size_t alignment_{core::DEFAULT_ALIGNMENT};
    core::AllocationStats stats_{};
};

} // namespace barealloc
