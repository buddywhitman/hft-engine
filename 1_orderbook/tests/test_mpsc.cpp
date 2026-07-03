#include <catch2/catch_test_macros.hpp>
#include "mpsc_queue.hpp"
#include <thread>
#include <vector>

using namespace hft;

TEST_CASE("MpscQueue: single producer", "[mpsc]") {
    MpscQueue<int> q;

    q.push(42);
    q.push(43);
    q.push(44);

    auto val = q.pop();
    REQUIRE(val.has_value());
    REQUIRE(*val == 42);

    val = q.pop();
    REQUIRE(val.has_value());
    REQUIRE(*val == 43);

    val = q.pop();
    REQUIRE(val.has_value());
    REQUIRE(*val == 44);

    val = q.pop();
    REQUIRE(!val.has_value());
}

TEST_CASE("MpscQueue: multiple producers", "[mpsc]") {
    MpscQueue<int> q;

    std::vector<std::thread> producers;
    const int num_producers = 4;
    const int items_per_producer = 25;

    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&q, p]() {
            for (int i = 0; i < items_per_producer; ++i) {
                q.push(p * 1000 + i);
            }
        });
    }

    for (auto& t : producers)
        t.join();

    int count = 0;
    while (auto val = q.pop()) {
        REQUIRE(val.has_value());
        count++;
    }

    REQUIRE(count == num_producers * items_per_producer);
}

TEST_CASE("MpscQueue: empty on pop", "[mpsc]") {
    MpscQueue<int> q;
    auto val = q.pop();
    REQUIRE(!val.has_value());
}

TEST_CASE("MpscQueue: alternating push/pop", "[mpsc]") {
    MpscQueue<int> q;

    q.push(1);
    auto val = q.pop();
    REQUIRE(*val == 1);

    q.push(2);
    q.push(3);
    val = q.pop();
    REQUIRE(*val == 2);
    val = q.pop();
    REQUIRE(*val == 3);
}
