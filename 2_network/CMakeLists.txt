add_library(network STATIC
    src/fix_message.cpp
    src/umem.cpp
    src/xsk_socket.cpp
)

target_include_directories(network PUBLIC include)
target_compile_options(network PRIVATE -fPIC)

# Look for libbpf and libxdp (optional)
find_package(libbpf QUIET)
find_package(libxdp QUIET)

if(libbpf_FOUND)
    target_link_libraries(network PUBLIC libbpf::libbpf)
    target_compile_definitions(network PUBLIC HAVE_LIBBPF)
endif()

# Tests
add_executable(test_fix_parser tests/test_fix_parser.cpp)
target_link_libraries(test_fix_parser PRIVATE network Catch2::Catch2WithMain)
add_test(NAME test_fix_parser COMMAND test_fix_parser)

# Benchmarks
add_executable(bench_fix_pipeline bench/bench_fix_pipeline.cpp)
target_link_libraries(bench_fix_pipeline PRIVATE network orderbook benchmark::benchmark)
