#pragma once

#include <cstddef>
#include <cstdint>
#include <bit>
#include <type_traits>

namespace barealloc::core {

/// Checks whether an unsigned integer is a power of two.
[[nodiscard]] constexpr bool is_power_of_two(std::size_t x) noexcept {
    return x > 0 && (x & (x - 1)) == 0;
}

/// Aligns an address upward to the given byte alignment (must be a power of two).
[[nodiscard]] inline uintptr_t align_up(uintptr_t address, std::size_t alignment) noexcept {
    const uintptr_t mask = static_cast<uintptr_t>(alignment - 1);
    return (address + mask) & ~mask;
}

/// Aligns a raw pointer upward to the given byte alignment.
template <typename T>
[[nodiscard]] inline T* align_ptr(T* ptr, std::size_t alignment) noexcept {
    const auto address = reinterpret_cast<uintptr_t>(ptr);
    const auto aligned = align_up(address, alignment);
    return reinterpret_cast<T*>(aligned);
}

/// Calculates the padding required to align an address to the given alignment.
[[nodiscard]] inline std::size_t align_offset(const void* ptr, std::size_t alignment) noexcept {
    const auto address = reinterpret_cast<uintptr_t>(ptr);
    const auto aligned = align_up(address, alignment);
    return static_cast<std::size_t>(aligned - address);
}

/// Calculates padding required when a header of size `header_size` precedes the aligned payload.
[[nodiscard]] inline std::size_t align_offset_with_header(const void* ptr,
                                                          std::size_t alignment,
                                                          std::size_t header_size) noexcept {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    p += header_size;
    const auto aligned = align_up(p, alignment);
    return static_cast<std::size_t>(aligned - reinterpret_cast<uintptr_t>(ptr));
}

/// Validates whether a pointer matches the given alignment.
[[nodiscard]] inline bool is_aligned(const void* ptr, std::size_t alignment) noexcept {
    return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
}

} // namespace barealloc::core
