add_library(orderbook STATIC
    src/tsc_clock.cpp
    src/order_book.cpp
)

target_include_directories(orderbook PUBLIC include)
target_compile_options(orderbook PRIVATE -fPIC)

# Tests
add_executable(test_spsc tests/test_spsc.cpp)
target_link_libraries(test_spsc PRIVATE orderbook Catch2::Catch2WithMain)
add_test(NAME test_spsc COMMAND test_spsc)

add_executable(test_mpsc tests/test_mpsc.cpp)
target_link_libraries(test_mpsc PRIVATE orderbook Catch2::Catch2WithMain)
add_test(NAME test_mpsc COMMAND test_mpsc)

add_executable(test_orderbook tests/test_orderbook.cpp)
target_link_libraries(test_orderbook PRIVATE orderbook Catch2::Catch2WithMain)
add_test(NAME test_orderbook COMMAND test_orderbook)

# Benchmarks
add_executable(bench_spsc bench/bench_spsc.cpp)
target_link_libraries(bench_spsc PRIVATE orderbook benchmark::benchmark)

add_executable(bench_mpsc bench/bench_mpsc.cpp)
target_link_libraries(bench_mpsc PRIVATE orderbook benchmark::benchmark)

add_executable(bench_orderbook bench/bench_orderbook.cpp)
target_link_libraries(bench_orderbook PRIVATE orderbook benchmark::benchmark)

add_executable(bench_realistic bench/bench_realistic.cpp)
target_link_libraries(bench_realistic PRIVATE orderbook benchmark::benchmark)
