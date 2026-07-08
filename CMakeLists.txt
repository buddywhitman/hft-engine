cmake_minimum_required(VERSION 3.20)
project(hft-engine VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Production-grade compiler flags: maximize performance
# -O3: maximum optimization
# -march=native: use CPU-specific instructions
# -funroll-loops: loop unrolling for pipelined execution
# -flto: link-time optimization
# -fno-exceptions: disable exceptions (not used in HFT path)
# Note: Removed -Werror to allow warnings in dependencies
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -funroll-loops -flto -fno-exceptions")

# Profiling flags (optional, enabled by default)
option(ENABLE_PROFILING "Enable performance profiling" ON)
if(ENABLE_PROFILING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
endif()

# FetchContent for dependencies
include(FetchContent)

# Google Benchmark for benchmarks - disable Werror
FetchContent_Declare(google_benchmark
    URL https://github.com/google/benchmark/archive/v1.8.1.zip
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
set(BENCHMARK_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "Download benchmark dependencies")
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "Disable Werror in benchmark")
FetchContent_MakeAvailable(google_benchmark)

# Catch2 for tests
FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/v3.4.0.zip
)
FetchContent_MakeAvailable(Catch2)

# Optional: AF_XDP support with libbpf
find_package(libbpf QUIET)
find_package(libxdp QUIET)

if(libbpf_FOUND)
    message(STATUS "[HFT] libbpf found: AF_XDP kernel-bypass enabled")
    add_compile_definitions(HAVE_LIBBPF)
else()
    message(WARNING "[HFT] libbpf not found. AF_XDP disabled. Install: sudo apt-get install libbpf-dev")
endif()

# Component 1: Lock-free Order Book
add_subdirectory(1_orderbook)

# Component 2: AF_XDP Network
add_subdirectory(2_network)

# Component 3: FPGA (simulation only without hardware)
option(BUILD_FPGA_SIM "Build FPGA Verilator simulation" OFF)
if(BUILD_FPGA_SIM)
    add_subdirectory(3_fpga)
endif()

# Test target
enable_testing()
