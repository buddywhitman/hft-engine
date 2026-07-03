#include <benchmark/benchmark.h>
#include "order_book.hpp"
#include "tsc_clock.hpp"
#include <vector>
#include <random>
#include <algorithm>

using namespace hft;

// Realistic scenario 1: Deep price ladder (500+ levels)
// Typical for illiquid instruments or multi-leg strategies
static void realistic_deep_ladder_insert(benchmark::State& state) {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult&) { matches.push_back({}); });
    TscClock::calibrate();

    // Build deep ladder: 500 buy levels, 500 sell levels
    for (int i = 0; i < 500; ++i) {
        ob.add_order(Side::Buy, 10000 - i, 100, OrderType::Limit);
        ob.add_order(Side::Sell, 10001 + i, 100, OrderType::Limit);
    }

    std::vector<uint64_t> latencies;
    latencies.reserve(10000);

    // Now insert in the middle of the spread
    for (auto _ : state) {
        uint64_t tsc_start = TscClock::now();
        ob.add_order(Side::Buy, 9900, 50, OrderType::Limit);  // Won't match
        uint64_t tsc_end = TscClock::now();

        uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);
        if (latencies.size() < latencies.capacity())
            latencies.push_back(latency_ns);
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["p50_ns"] = latencies[latencies.size() / 2];
        state.counters["p95_ns"] = latencies[latencies.size() * 95 / 100];
        state.counters["p99_ns"] = latencies[latencies.size() * 99 / 100];
        state.counters["p99.9_ns"] = latencies[latencies.size() * 999 / 1000];
    }
}
BENCHMARK(realistic_deep_ladder_insert)->Iterations(10000)->Unit(benchmark::kNanosecond);

// Realistic scenario 2: Multi-level matching
// Order crosses 10+ price levels (worst case)
static void realistic_multi_level_match(benchmark::State& state) {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult&) { matches.push_back({}); });
    TscClock::calibrate();

    // Build 20-level ladder on each side
    for (int i = 0; i < 20; ++i) {
        ob.add_order(Side::Buy, 10000 - i * 10, 100, OrderType::Limit);
        ob.add_order(Side::Sell, 10001 + i * 10, 100, OrderType::Limit);
    }

    std::vector<uint64_t> latencies;
    latencies.reserve(10000);

    // Aggressive sell that crosses 15 buy levels
    for (auto _ : state) {
        uint64_t tsc_start = TscClock::now();
        ob.add_order(Side::Sell, 9500, 1500, OrderType::Limit);  // Will match 15 levels
        uint64_t tsc_end = TscClock::now();

        uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);
        if (latencies.size() < latencies.capacity())
            latencies.push_back(latency_ns);
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["p50_ns"] = latencies[latencies.size() / 2];
        state.counters["p95_ns"] = latencies[latencies.size() * 95 / 100];
        state.counters["p99_ns"] = latencies[latencies.size() * 99 / 100];
    }
}
BENCHMARK(realistic_multi_level_match)->Iterations(10000)->Unit(benchmark::kNanosecond);

// Realistic scenario 3: High-frequency order churn
// 1000 orders/second with cancellations (typical HFT)
static void realistic_order_churn(benchmark::State& state) {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult&) { matches.push_back({}); });
    TscClock::calibrate();

    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> price_dist(9950, 10050);
    std::uniform_int_distribution<uint32_t> qty_dist(10, 100);
    std::vector<uint64_t> order_ids;
    order_ids.reserve(100);

    std::vector<uint64_t> latencies;
    latencies.reserve(100000);

    for (auto _ : state) {
        // Add 10 orders
        for (int i = 0; i < 10; ++i) {
            uint64_t tsc_start = TscClock::now();
            uint64_t oid = ob.add_order(
                (i % 2 == 0) ? Side::Buy : Side::Sell,
                price_dist(rng),
                qty_dist(rng),
                OrderType::Limit
            );
            uint64_t tsc_end = TscClock::now();
            if (oid > 0) order_ids.push_back(oid);

            uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);
            if (latencies.size() < latencies.capacity())
                latencies.push_back(latency_ns);
        }

        // Cancel 5 oldest orders
        for (int i = 0; i < 5 && !order_ids.empty(); ++i) {
            uint64_t tsc_start = TscClock::now();
            ob.cancel_order(order_ids.back());
            uint64_t tsc_end = TscClock::now();
            order_ids.pop_back();

            uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);
            if (latencies.size() < latencies.capacity())
                latencies.push_back(latency_ns);
        }
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["p50_ns"] = latencies[latencies.size() / 2];
        state.counters["p95_ns"] = latencies[latencies.size() * 95 / 100];
        state.counters["p99_ns"] = latencies[latencies.size() * 99 / 100];
        state.counters["p99.9_ns"] = latencies[latencies.size() * 999 / 1000];
    }
}
BENCHMARK(realistic_order_churn)->Iterations(1000)->Unit(benchmark::kNanosecond);

// Realistic scenario 4: Worst case - crossing entire book
// Scenario: Market order crosses entire order book
static void realistic_market_order_worst_case(benchmark::State& state) {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult&) { matches.push_back({}); });
    TscClock::calibrate();

    std::vector<uint64_t> latencies;
    latencies.reserve(1000);

    for (auto _ : state) {
        // Build fresh ladder each iteration for worst case
        matches.clear();

        // Pre-populate with 100 levels on sell side
        for (int i = 0; i < 100; ++i) {
            ob.add_order(Side::Sell, 10001 + i, 100, OrderType::Limit);
        }

        // Massive aggressive buy that crosses all 100 levels
        uint64_t tsc_start = TscClock::now();
        ob.add_order(Side::Buy, 10500, 10000, OrderType::Limit);  // Will match all 100 levels
        uint64_t tsc_end = TscClock::now();

        uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);
        if (latencies.size() < latencies.capacity())
            latencies.push_back(latency_ns);
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["p50_ns"] = latencies[latencies.size() / 2];
        state.counters["p99_ns"] = latencies[latencies.size() * 99 / 100];
        state.counters["max_ns"] = latencies.back();
    }
}
BENCHMARK(realistic_market_order_worst_case)->Iterations(1000)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
