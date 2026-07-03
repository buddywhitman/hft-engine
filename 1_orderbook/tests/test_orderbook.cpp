#include <catch2/catch_test_macros.hpp>
#include "order_book.hpp"
#include <vector>

using namespace hft;

TEST_CASE("OrderBook: add resting order", "[orderbook]") {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult& m) { matches.push_back(m); });

    uint64_t order_id = ob.add_order(Side::Buy, 10000, 100, OrderType::Limit);
    REQUIRE(order_id > 0);
    REQUIRE(matches.empty());

    auto* bid = ob.best_bid();
    REQUIRE(bid != nullptr);
    REQUIRE(bid->price_ticks == 10000);
    REQUIRE(bid->total_quantity == 100);
}

TEST_CASE("OrderBook: immediate match", "[orderbook]") {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult& m) { matches.push_back(m); });

    // Place resting buy order
    ob.add_order(Side::Buy, 10000, 100, OrderType::Limit);

    // Aggressive sell order matches it
    ob.add_order(Side::Sell, 9500, 100, OrderType::Limit);

    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].quantity == 100);
    REQUIRE(matches[0].price_ticks == 10000);  // Maker's price
}

TEST_CASE("OrderBook: partial match", "[orderbook]") {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult& m) { matches.push_back(m); });

    ob.add_order(Side::Buy, 10000, 100, OrderType::Limit);
    ob.add_order(Side::Sell, 9500, 60, OrderType::Limit);

    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].quantity == 60);

    // 40 should remain on the bid side
    auto* bid = ob.best_bid();
    REQUIRE(bid != nullptr);
    REQUIRE(bid->total_quantity == 40);
}

TEST_CASE("OrderBook: multiple price levels", "[orderbook]") {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult& m) { matches.push_back(m); });

    ob.add_order(Side::Buy, 10000, 100, OrderType::Limit);
    ob.add_order(Side::Buy, 9500, 100, OrderType::Limit);
    ob.add_order(Side::Buy, 9000, 100, OrderType::Limit);

    // Aggressive sell that crosses multiple levels
    ob.add_order(Side::Sell, 8500, 250, OrderType::Limit);

    REQUIRE(matches.size() == 3);
    REQUIRE(matches[0].price_ticks == 10000);  // Best bid first
    REQUIRE(matches[1].price_ticks == 9500);
    REQUIRE(matches[2].price_ticks == 9000);
}

TEST_CASE("OrderBook: spread", "[orderbook]") {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult& m) { matches.push_back(m); });

    ob.add_order(Side::Buy, 10000, 100, OrderType::Limit);
    ob.add_order(Side::Sell, 10100, 100, OrderType::Limit);

    REQUIRE(ob.spread_ticks() == 100);
}

TEST_CASE("OrderBook: empty spread", "[orderbook]") {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult& m) { matches.push_back(m); });

    REQUIRE(ob.spread_ticks() == INT64_MAX);
}

TEST_CASE("OrderBook: sell side ordering", "[orderbook]") {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult& m) { matches.push_back(m); });

    ob.add_order(Side::Sell, 10500, 100, OrderType::Limit);
    ob.add_order(Side::Sell, 10000, 100, OrderType::Limit);
    ob.add_order(Side::Sell, 10200, 100, OrderType::Limit);

    // Best ask should be the lowest price
    auto* ask = ob.best_ask();
    REQUIRE(ask != nullptr);
    REQUIRE(ask->price_ticks == 10000);
}
