#include <benchmark/benchmark.h>
#include "spsc_queue.hpp"
#include "tsc_clock.hpp"
#include <numeric>
#include <algorithm>
#include <vector>

using namespace hft;

static void spsc_push_pop_ping_pong(benchmark::State& state) {
    SpscQueue<uint64_t, 8192> q;
    TscClock::calibrate();

    std::vector<uint64_t> latencies;
    latencies.reserve(100000);

    for (auto _ : state) {
        uint64_t tsc_start = TscClock::now();
        [[maybe_unused]] bool pushed = q.push(tsc_start);
        auto val = q.pop();
        uint64_t tsc_end = TscClock::now();

        if (val.has_value()) {
            uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);
            if (latencies.size() < latencies.capacity())
                latencies.push_back(latency_ns);
        }
    }

    std::sort(latencies.begin(), latencies.end());

    state.counters["p50_ns"] = latencies[latencies.size() / 2];
    state.counters["p95_ns"] = latencies[latencies.size() * 95 / 100];
    state.counters["p99_ns"] = latencies[latencies.size() * 99 / 100];
    state.counters["p99.9_ns"] = latencies[latencies.size() * 999 / 1000];
}
BENCHMARK(spsc_push_pop_ping_pong)->Iterations(100000)->Unit(benchmark::kNanosecond);

static void spsc_throughput_push_only(benchmark::State& state) {
    SpscQueue<uint64_t, 65536> q;

    for (auto _ : state) {
        for (int i = 0; i < 10000; ++i) {
            [[maybe_unused]] bool pushed = q.push(i);
        }
        while (q.pop()) {
        }
    }
}
BENCHMARK(spsc_throughput_push_only)->Unit(benchmark::kNanosecond);

static void spsc_throughput_sustained(benchmark::State& state) {
    SpscQueue<uint64_t, 65536> q;
    TscClock::calibrate();

    uint64_t total_ops = 0;
    std::vector<uint64_t> latencies;
    latencies.reserve(1000000);

    for (auto _ : state) {
        for (int i = 0; i < 1000; ++i) {
            uint64_t tsc_start = TscClock::now();
            [[maybe_unused]] bool pushed = q.push(i);
            uint64_t tsc_end = TscClock::now();

            uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);
            if (latencies.size() < latencies.capacity())
                latencies.push_back(latency_ns);
            total_ops++;
        }

        while (q.pop()) {
        }
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["p50_ns"] = latencies[latencies.size() / 2];
        state.counters["p99_ns"] = latencies[latencies.size() * 99 / 100];
        state.counters["max_ns"] = latencies.back();
    }
}
BENCHMARK(spsc_throughput_sustained)->Iterations(10)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
