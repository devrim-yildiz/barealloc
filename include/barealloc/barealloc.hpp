#pragma once

/**
 * @file barealloc.hpp
 * @brief Master header for barealloc: High-performance memory allocators in modern C++20.
 *
 * Includes core alignment utilities, ArenaAllocator, PoolAllocator,
 * FreeListAllocator, and STL container adapters.
 */

#include "barealloc/core/align.hpp"
#include "barealloc/core/types.hpp"
#include "barealloc/allocators/arena_allocator.hpp"
#include "barealloc/allocators/pool_allocator.hpp"
#include "barealloc/allocators/free_list_allocator.hpp"
#include "barealloc/adapters/stl_adapter.hpp"
