cmake_minimum_required(VERSION 3.20)
project(limit_order_book LANGUAGES CXX)

# ---- Standard ----------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if no build type was specified (single-config generators).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- Optimization flags ------------------------------------------------------
# Release: aggressive, machine-tuned. Debug: assertions + symbols.
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

option(LOB_NATIVE_ARCH "Build with -march=native" ON)
if(LOB_NATIVE_ARCH)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-march=native" LOB_HAS_MARCH_NATIVE)
  if(LOB_HAS_MARCH_NATIVE)
    add_compile_options($<$<CONFIG:Release>:-march=native>)
  endif()
endif()

# ---- Core library (header-only interface for now) ----------------------------
add_library(lob INTERFACE)
target_include_directories(lob INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(lob INTERFACE cxx_std_20)

# ---- Tests -------------------------------------------------------------------
option(LOB_BUILD_TESTS "Build unit tests" ON)
if(LOB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---- Benchmarks --------------------------------------------------------------
option(LOB_BUILD_BENCH "Build benchmarks" ON)
if(LOB_BUILD_BENCH)
  add_subdirectory(bench)
endif()
