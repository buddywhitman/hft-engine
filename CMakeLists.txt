cmake_minimum_required(VERSION 3.20)
project(hft-engine VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -funroll-loops -fno-exceptions -Werror")

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
