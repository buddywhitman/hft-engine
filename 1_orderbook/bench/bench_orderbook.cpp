#include <benchmark/benchmark.h>
#include "order_book.hpp"
#include "tsc_clock.hpp"
#include <vector>
#include <random>
#include <algorithm>

using namespace hft;

static void orderbook_add_order_no_match(benchmark::State& state) {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult&) { matches.push_back({}); });
    TscClock::calibrate();

    std::vector<uint64_t> latencies;
    latencies.reserve(100000);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> price_dist(5000, 15000);
    std::uniform_int_distribution<uint32_t> qty_dist(10, 1000);

    for (auto _ : state) {
        uint64_t tsc_start = TscClock::now();
        ob.add_order(Side::Buy, price_dist(rng), qty_dist(rng), OrderType::Limit);
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
BENCHMARK(orderbook_add_order_no_match)->Iterations(100000)->Unit(benchmark::kNanosecond);

static void orderbook_with_matching(benchmark::State& state) {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult&) { matches.push_back({}); });
    TscClock::calibrate();

    // Pre-populate with resting orders
    for (int i = 0; i < 50; ++i) {
        ob.add_order(Side::Buy, 10000 - i * 10, 100, OrderType::Limit);
        ob.add_order(Side::Sell, 10100 + i * 10, 100, OrderType::Limit);
    }

    std::vector<uint64_t> latencies;
    latencies.reserve(100000);

    for (auto _ : state) {
        uint64_t tsc_start = TscClock::now();
        ob.add_order(Side::Sell, 9500, 100, OrderType::Limit);
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
BENCHMARK(orderbook_with_matching)->Iterations(100000)->Unit(benchmark::kNanosecond);

static void orderbook_sequential_adds(benchmark::State& state) {
    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult&) { matches.push_back({}); });
    TscClock::calibrate();

    std::vector<uint64_t> latencies;
    latencies.reserve(1000000);

    for (auto _ : state) {
        for (int i = 0; i < 1000; ++i) {
            uint64_t tsc_start = TscClock::now();
            ob.add_order(
                (i % 2 == 0) ? Side::Buy : Side::Sell,
                10000 + i,
                100,
                OrderType::Limit
            );
            uint64_t tsc_end = TscClock::now();

            uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);
            if (latencies.size() < latencies.capacity())
                latencies.push_back(latency_ns);
        }
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["p50_ns"] = latencies[latencies.size() / 2];
        state.counters["p99_ns"] = latencies[latencies.size() * 99 / 100];
        state.counters["p99.99_ns"] = latencies[latencies.size() * 9999 / 10000];
        state.counters["max_ns"] = latencies.back();
    }
}
BENCHMARK(orderbook_sequential_adds)->Iterations(100)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
