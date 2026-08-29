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
 * @brief Dynamic general-purpose allocator with intrusive free-list, best-fit search,
 * block splitting, and immediate boundary coalescing to eliminate fragmentation.
 */
class FreeListAllocator {
public:
    enum class PlacementPolicy {
        FindFirst,
        FindBest
    };

    struct BlockHeader {
        std::size_t size{0};        // Total size of this physical block (including header)
        std::size_t padding{0};     // Total offset from BlockHeader to user payload
        bool is_free{true};
        BlockHeader* next{nullptr}; // Pointer to next free block in address-sorted free list
        BlockHeader* prev{nullptr}; // Pointer to prev free block in address-sorted free list
    };

    static constexpr std::size_t HEADER_SIZE = sizeof(BlockHeader);
    static constexpr std::size_t TAG_SIZE = sizeof(std::uint32_t);
    static constexpr std::size_t MIN_PAYLOAD = 16;
    static constexpr std::size_t MIN_BLOCK_SIZE = HEADER_SIZE + TAG_SIZE + MIN_PAYLOAD;

    explicit FreeListAllocator(std::size_t capacity, PlacementPolicy policy = PlacementPolicy::FindBest)
        : capacity_(core::align_up(capacity, core::DEFAULT_ALIGNMENT)),
          policy_(policy) {
        
        buffer_ = static_cast<std::byte*>(std::malloc(capacity_));
        if (!buffer_) {
            throw std::bad_alloc();
        }

        reset();
    }

    ~FreeListAllocator() noexcept {
        if (buffer_) {
            std::free(buffer_);
        }
    }

    FreeListAllocator(const FreeListAllocator&) = delete;
    FreeListAllocator& operator=(const FreeListAllocator&) = delete;

    FreeListAllocator(FreeListAllocator&& other) noexcept
        : buffer_(other.buffer_),
          capacity_(other.capacity_),
          free_head_(other.free_head_),
          policy_(other.policy_),
          stats_(other.stats_) {
        other.buffer_ = nullptr;
        other.capacity_ = 0;
        other.free_head_ = nullptr;
    }

    FreeListAllocator& operator=(FreeListAllocator&& other) noexcept {
        if (this != &other) {
            if (buffer_) {
                std::free(buffer_);
            }
            buffer_ = other.buffer_;
            capacity_ = other.capacity_;
            free_head_ = other.free_head_;
            policy_ = other.policy_;
            stats_ = other.stats_;

            other.buffer_ = nullptr;
            other.capacity_ = 0;
            other.free_head_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = core::DEFAULT_ALIGNMENT) noexcept {
        if (size == 0) [[unlikely]] {
            return nullptr;
        }

        BlockHeader* best_block = nullptr;
        std::size_t best_padding = 0;
        std::size_t best_diff = SIZE_MAX;

        BlockHeader* curr = free_head_;
        while (curr) {
            // User payload must be aligned, and we need space for a uint32_t tag immediately before it
            const auto block_addr = reinterpret_cast<uintptr_t>(curr);
            const auto min_payload_addr = block_addr + HEADER_SIZE + TAG_SIZE;
            const auto aligned_payload_addr = core::align_up(min_payload_addr, alignment);
            const auto total_offset = static_cast<std::size_t>(aligned_payload_addr - block_addr);
            const auto total_needed = total_offset + size;

            if (curr->size >= total_needed) {
                if (policy_ == PlacementPolicy::FindFirst) {
                    best_block = curr;
                    best_padding = total_offset;
                    break;
                }
                const auto diff = curr->size - total_needed;
                if (diff < best_diff) {
                    best_diff = diff;
                    best_block = curr;
                    best_padding = total_offset;
                    if (diff == 0) break; // Perfect fit
                }
            }
            curr = curr->next;
        }

        if (!best_block) [[unlikely]] {
            return nullptr; // Out of memory / no fitting chunk
        }

        const auto total_required = best_padding + size;
        const auto remaining = best_block->size - total_required;

        // Split block if excess is large enough
        if (remaining >= MIN_BLOCK_SIZE) {
            auto* const new_free_block = reinterpret_cast<BlockHeader*>(
                reinterpret_cast<std::byte*>(best_block) + total_required
            );
            new_free_block->size = remaining;
            new_free_block->padding = 0;
            new_free_block->is_free = true;
            new_free_block->next = nullptr;
            new_free_block->prev = nullptr;

            insert_free_block(new_free_block);
            best_block->size = total_required;
        }

        remove_free_block(best_block);
        best_block->is_free = false;
        best_block->padding = best_padding;

        std::byte* const payload = reinterpret_cast<std::byte*>(best_block) + best_padding;

        // Store 32-bit offset directly preceding payload for O(1) header lookup on free()
        auto* const tag = reinterpret_cast<std::uint32_t*>(payload - TAG_SIZE);
        *tag = static_cast<std::uint32_t>(best_padding);

        stats_.allocation_count++;
        stats_.total_allocated += size;
        stats_.current_allocated += best_block->size;
        stats_.peak_allocated = std::max(stats_.peak_allocated, stats_.current_allocated);

        return static_cast<void*>(payload);
    }

    void deallocate(void* ptr) noexcept {
        if (!ptr) [[unlikely]] {
            return;
        }

        assert(owns(ptr) && "Pointer does not belong to this FreeListAllocator");

        auto* const byte_ptr = static_cast<std::byte*>(ptr);
        auto* const tag = reinterpret_cast<const std::uint32_t*>(byte_ptr - TAG_SIZE);
        const std::size_t offset = *tag;

        auto* const header = reinterpret_cast<BlockHeader*>(byte_ptr - offset);
        assert(!header->is_free && "Double free detected in FreeListAllocator");

        header->is_free = true;
        stats_.deallocation_count++;
        if (stats_.current_allocated >= header->size) {
            stats_.current_allocated -= header->size;
        }

        insert_free_block(header);
        coalesce();
    }

    void reset() noexcept {
        free_head_ = reinterpret_cast<BlockHeader*>(buffer_);
        free_head_->size = capacity_;
        free_head_->padding = 0;
        free_head_->is_free = true;
        free_head_->next = nullptr;
        free_head_->prev = nullptr;

        stats_.current_allocated = 0;
    }

    [[nodiscard]] bool owns(const void* ptr) const noexcept {
        const auto p = reinterpret_cast<uintptr_t>(ptr);
        const auto start = reinterpret_cast<uintptr_t>(buffer_);
        const auto end = start + capacity_;
        return p >= start && p < end;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] const core::AllocationStats& stats() const noexcept { return stats_; }

private:
    void insert_free_block(BlockHeader* block) noexcept {
        block->is_free = true;
        block->next = nullptr;
        block->prev = nullptr;

        if (!free_head_) {
            free_head_ = block;
            return;
        }

        if (block < free_head_) {
            block->next = free_head_;
            free_head_->prev = block;
            free_head_ = block;
            return;
        }

        BlockHeader* curr = free_head_;
        while (curr->next && curr->next < block) {
            curr = curr->next;
        }

        block->next = curr->next;
        block->prev = curr;
        if (curr->next) {
            curr->next->prev = block;
        }
        curr->next = block;
    }

    void remove_free_block(BlockHeader* block) noexcept {
        if (block->prev) {
            block->prev->next = block->next;
        } else {
            free_head_ = block->next;
        }
        if (block->next) {
            block->next->prev = block->prev;
        }
        block->next = nullptr;
        block->prev = nullptr;
    }

    void coalesce() noexcept {
        BlockHeader* curr = free_head_;
        while (curr && curr->next) {
            const auto curr_end = reinterpret_cast<std::byte*>(curr) + curr->size;
            if (curr_end == reinterpret_cast<std::byte*>(curr->next)) {
                // Contiguous physical neighbor is free: coalesce immediately
                BlockHeader* const next_block = curr->next;
                curr->size += next_block->size;
                curr->next = next_block->next;
                if (next_block->next) {
                    next_block->next->prev = curr;
                }
            } else {
                curr = curr->next;
            }
        }
    }

    std::byte* buffer_{nullptr};
    std::size_t capacity_{0};
    BlockHeader* free_head_{nullptr};
    PlacementPolicy policy_{PlacementPolicy::FindBest};
    core::AllocationStats stats_{};
};

} // namespace barealloc
