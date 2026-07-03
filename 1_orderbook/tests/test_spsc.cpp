#include <catch2/catch_test_macros.hpp>
#include "spsc_queue.hpp"

using namespace hft;

TEST_CASE("SpscQueue: basic push/pop", "[spsc]") {
    SpscQueue<int, 64> q;

    REQUIRE(q.empty());
    REQUIRE(q.push(42));
    REQUIRE(!q.empty());

    auto val = q.pop();
    REQUIRE(val.has_value());
    REQUIRE(*val == 42);
    REQUIRE(q.empty());
}

TEST_CASE("SpscQueue: multiple items", "[spsc]") {
    SpscQueue<int, 128> q;

    for (int i = 0; i < 100; ++i) {
        REQUIRE(q.push(i));
    }

    for (int i = 0; i < 100; ++i) {
        auto val = q.pop();
        REQUIRE(val.has_value());
        REQUIRE(*val == i);
    }

    REQUIRE(q.empty());
}

TEST_CASE("SpscQueue: full detection", "[spsc]") {
    SpscQueue<int, 16> q;

    // Fill queue completely (capacity - 1, as one slot is always reserved)
    for (int i = 0; i < 15; ++i) {
        REQUIRE(q.push(i));
    }

    // Next push should fail
    REQUIRE(!q.push(999));
}

TEST_CASE("SpscQueue: empty on pop", "[spsc]") {
    SpscQueue<int, 64> q;

    auto val = q.pop();
    REQUIRE(!val.has_value());
}

TEST_CASE("SpscQueue: wrap-around", "[spsc]") {
    SpscQueue<int, 8> q;

    // Fill and drain multiple times
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 7; ++i) {
            REQUIRE(q.push(round * 100 + i));
        }
        for (int i = 0; i < 7; ++i) {
            auto val = q.pop();
            REQUIRE(val.has_value());
            REQUIRE(*val == round * 100 + i);
        }
    }
}
