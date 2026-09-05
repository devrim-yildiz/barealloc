cmake_minimum_required(VERSION 3.20)
project(barealloc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Header-only core library target
add_library(barealloc INTERFACE)
target_include_directories(barealloc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(barealloc INTERFACE cxx_std_20)

# Compiler-specific warnings and optimization options
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus)
    # Enable release optimizations for performance targets
    set(RELEASE_FLAGS /O2 /Oi /Ot /GL)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion)
    set(RELEASE_FLAGS -O3 -march=native)
endif()

# Test Suite Executable
add_executable(barealloc_tests tests/test_suite.cpp)
target_link_libraries(barealloc_tests PRIVATE barealloc)

# Benchmark Suite Executable
add_executable(barealloc_benchmarks benchmarks/benchmark_suite.cpp)
target_link_libraries(barealloc_benchmarks PRIVATE barealloc)
target_compile_options(barealloc_benchmarks PRIVATE $<$<CONFIG:Release>:${RELEASE_FLAGS}>)

# Game Loop Simulation Demo Executable
add_executable(barealloc_demo examples/game_loop_demo.cpp)
target_link_libraries(barealloc_demo PRIVATE barealloc)
