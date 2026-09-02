#pragma once

#include <cstddef>
#include <new>
#include <type_traits>

namespace barealloc {

/**
 * @brief C++ standard library compliant allocator adapter.
 * 
 * Enables seamless usage of barealloc allocators (Arena, Pool, FreeList)
 * within standard STL containers like std::vector, std::list, and std::basic_string.
 */
template <typename T, typename AllocatorType>
class STLAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U>
    struct rebind {
        using other = STLAllocator<U, AllocatorType>;
    };

    explicit STLAllocator(AllocatorType& allocator) noexcept
        : allocator_(&allocator) {}

    template <typename U>
    STLAllocator(const STLAllocator<U, AllocatorType>& other) noexcept
        : allocator_(other.allocator_) {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (n == 0) {
            return nullptr;
        }

        if (n > (static_cast<std::size_t>(-1) / sizeof(T))) {
            throw std::bad_array_new_length();
        }

        void* const p = allocator_->allocate(n * sizeof(T), alignof(T));
        if (!p) {
            throw std::bad_alloc();
        }
        return static_cast<T*>(p);
    }

    void deallocate(T* p, [[maybe_unused]] std::size_t n) noexcept {
        if (p) {
            allocator_->deallocate(p);
        }
    }

    template <typename U, typename OtherAlloc>
    bool operator==(const STLAllocator<U, OtherAlloc>& rhs) const noexcept {
        return allocator_ == rhs.allocator_;
    }

    template <typename U, typename OtherAlloc>
    bool operator!=(const STLAllocator<U, OtherAlloc>& rhs) const noexcept {
        return !(*this == rhs);
    }

    [[nodiscard]] AllocatorType& underlying() noexcept { return *allocator_; }
    [[nodiscard]] const AllocatorType& underlying() const noexcept { return *allocator_; }

    template <typename U, typename OtherAlloc>
    friend class STLAllocator;

private:
    AllocatorType* allocator_{nullptr};
};

} // namespace barealloc
