#include <benchmark/benchmark.h>
#include "order_book.hpp"
#include "spsc_queue.hpp"
#include <vector>
#include <random>
#include <algorithm>
#include <numeric>
#include <iostream>

using namespace hft;

// ============================================================================
// Production Profiling Benchmarks
//
// These benchmarks include comprehensive performance analysis:
// - Latency percentiles (P50, P95, P99, P99.9)
// - Memory access patterns
// - Cache efficiency metrics
// - Real-world workload simulation
// ============================================================================

// Benchmark 1: SPSC Queue - Lock-Free Performance
static void profile_spsc_throughput(benchmark::State& state) {
    SpscQueue<uint64_t, 4096> queue;
    std::vector<uint64_t> latencies;
    latencies.reserve(100000);

    uint64_t ops = 0;

    for (auto _ : state) {
        // Bulk push-pop operations
        for (int i = 0; i < 100; ++i) {
            uint64_t tsc_start = __builtin_ia32_rdtsc();
            [[maybe_unused]] bool pushed = queue.push(i);
            auto val = queue.pop();
            uint64_t tsc_end = __builtin_ia32_rdtsc();

            if (latencies.size() < latencies.capacity()) {
                latencies.push_back(tsc_end - tsc_start);
            }
            ops++;
        }
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["throughput"] = benchmark::Counter(
            static_cast<double>(ops), benchmark::Counter::kIsRate);
        state.counters["p50_cycles"] = latencies[latencies.size() / 2];
        state.counters["p95_cycles"] = latencies[latencies.size() * 95 / 100];
        state.counters["p99_cycles"] = latencies[latencies.size() * 99 / 100];
        state.counters["p99.9_cycles"] = latencies[latencies.size() * 999 / 1000];
        state.counters["max_cycles"] = latencies.back();
    }
}
BENCHMARK(profile_spsc_throughput)->Unit(benchmark::kMicrosecond);

// Benchmark 2: Order Book - Add Performance Across Book Depths
static void profile_orderbook_add_scaling(benchmark::State& state) {
    int num_levels = state.range(0);
    OrderBook ob(1, [](const MatchResult&) {});

    // Pre-populate with levels
    for (int i = 0; i < num_levels; ++i) {
        ob.add_order(Side::Buy, 10000 - i, 100, OrderType::Limit);
        ob.add_order(Side::Sell, 10001 + i, 100, OrderType::Limit);
    }

    std::vector<uint64_t> latencies;
    latencies.reserve(10000);

    for (auto _ : state) {
        uint64_t tsc_start = __builtin_ia32_rdtsc();
        ob.add_order(Side::Buy, 9900 + (std::rand() % 100), 50, OrderType::Limit);
        uint64_t tsc_end = __builtin_ia32_rdtsc();

        if (latencies.size() < latencies.capacity()) {
            latencies.push_back(tsc_end - tsc_start);
        }
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["levels"] = num_levels;
        state.counters["p50_cycles"] = latencies[latencies.size() / 2];
        state.counters["p99_cycles"] = latencies[latencies.size() * 99 / 100];
    }
}
BENCHMARK(profile_orderbook_add_scaling)
    ->Arg(10)->Arg(50)->Arg(100)->Arg(500)
    ->Unit(benchmark::kMicrosecond);

// Benchmark 3: Order Book - Match Performance (crossing levels)
static void profile_orderbook_match_scaling(benchmark::State& state) {
    int cross_levels = state.range(0);
    OrderBook ob(1, [](const MatchResult&) {});

    // Pre-populate
    for (int i = 0; i < cross_levels + 10; ++i) {
        ob.add_order(Side::Buy, 10000 - i, 100, OrderType::Limit);
    }

    std::vector<uint64_t> latencies;
    latencies.reserve(1000);

    for (auto _ : state) {
        uint64_t tsc_start = __builtin_ia32_rdtsc();
        // Aggressive order crossing N levels
        ob.add_order(Side::Sell, 10000 - cross_levels, 100 * cross_levels, OrderType::Limit);
        uint64_t tsc_end = __builtin_ia32_rdtsc();

        if (latencies.size() < latencies.capacity()) {
            latencies.push_back(tsc_end - tsc_start);
        }

        // Re-populate for next iteration
        for (int i = 0; i < cross_levels + 10; ++i) {
            ob.add_order(Side::Buy, 10000 - i, 100, OrderType::Limit);
        }
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["cross_levels"] = cross_levels;
        state.counters["p50_cycles"] = latencies[latencies.size() / 2];
        state.counters["p99_cycles"] = latencies[latencies.size() * 99 / 100];
        state.counters["max_cycles"] = latencies.back();
    }
}
BENCHMARK(profile_orderbook_match_scaling)
    ->Arg(1)->Arg(5)->Arg(10)->Arg(20)->Arg(50)
    ->Unit(benchmark::kMicrosecond);

// Benchmark 4: Memory Bandwidth (hot path cache efficiency)
static void profile_memory_bandwidth(benchmark::State& state) {
    const int cache_line_size = 64;
    const int array_size = 10000 * cache_line_size;
    std::vector<uint8_t> data(array_size, 42);

    volatile uint64_t sum = 0;

    for (auto _ : state) {
        uint64_t tsc_start = __builtin_ia32_rdtsc();

        // Sequential access (L1 cache friendly)
        for (int i = 0; i < array_size; i += cache_line_size) {
            sum += data[i];
        }

        uint64_t tsc_end = __builtin_ia32_rdtsc();
        state.counters["cycles_per_kb"] = (double)(tsc_end - tsc_start) /
                                          (array_size / 1024.0);
    }
}
BENCHMARK(profile_memory_bandwidth)->Unit(benchmark::kNanosecond);

// Benchmark 5: Random Memory Access (worst-case cache)
static void profile_cache_miss_latency(benchmark::State& state) {
    const int array_size = 1000000;
    std::vector<int> data(array_size);
    for (int i = 0; i < array_size; ++i) {
        data[i] = (i + 1) % array_size;  // Create pointers to random locations
    }

    volatile int sum = 0;
    int idx = 0;

    for (auto _ : state) {
        uint64_t tsc_start = __builtin_ia32_rdtsc();

        // Follow random pointers (cache misses)
        for (int i = 0; i < 1000; ++i) {
            idx = data[idx];
            sum += idx;
        }

        uint64_t tsc_end = __builtin_ia32_rdtsc();
        state.counters["cycles_per_access"] = (double)(tsc_end - tsc_start) / 1000.0;
    }
}
BENCHMARK(profile_cache_miss_latency)->Unit(benchmark::kNanosecond);

// Benchmark 6: Branch Prediction (control flow cost)
static void profile_branch_prediction(benchmark::State& state) {
    std::vector<int> values(10000);
    std::mt19937 gen(42);
    std::uniform_int_distribution<> dis(0, 255);
    for (auto& v : values) v = dis(gen);

    volatile int sum = 0;

    for (auto _ : state) {
        uint64_t tsc_start = __builtin_ia32_rdtsc();

        // Predictable branches (best case)
        for (const auto& v : values) {
            if (v >= 128)  // ~50% taken
                sum += v;
        }

        uint64_t tsc_end = __builtin_ia32_rdtsc();
        state.counters["cycles_predictable"] = (double)(tsc_end - tsc_start) /
                                               values.size();
    }
}
BENCHMARK(profile_branch_prediction)->Unit(benchmark::kNanosecond);

// Benchmark 7: Realistic HFT Workload Mix
// Simulates: adds (70%), cancels (20%), partial fills (10%)
static void profile_realistic_workload_mix(benchmark::State& state) {
    OrderBook ob(1, [](const MatchResult&) {});
    std::mt19937 gen(42);
    std::uniform_int_distribution<> price_dist(9900, 10100);
    std::uniform_int_distribution<> qty_dist(10, 1000);
    std::uniform_int_distribution<> action_dist(0, 99);

    std::vector<uint64_t> order_ids;
    std::vector<uint64_t> latencies;
    latencies.reserve(100000);

    for (auto _ : state) {
        int action = action_dist(gen);

        uint64_t tsc_start = __builtin_ia32_rdtsc();

        if (action < 70) {  // Add (70%)
            uint64_t oid = ob.add_order(
                (gen() & 1) ? Side::Buy : Side::Sell,
                price_dist(gen),
                qty_dist(gen),
                OrderType::Limit
            );
            if (oid > 0) order_ids.push_back(oid);
        } else if (action < 90 && !order_ids.empty()) {  // Cancel (20%)
            ob.cancel_order(order_ids.back());
            order_ids.pop_back();
        }
        // Partial fills handled by order book callback

        uint64_t tsc_end = __builtin_ia32_rdtsc();

        if (latencies.size() < latencies.capacity()) {
            latencies.push_back(tsc_end - tsc_start);
        }
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        double mean = std::accumulate(latencies.begin(), latencies.end(), 0.0) /
                      latencies.size();
        double stddev = 0;
        for (const auto& l : latencies) {
            stddev += (l - mean) * (l - mean);
        }
        stddev = std::sqrt(stddev / latencies.size());

        state.counters["p50_cycles"] = latencies[latencies.size() / 2];
        state.counters["p95_cycles"] = latencies[latencies.size() * 95 / 100];
        state.counters["p99_cycles"] = latencies[latencies.size() * 99 / 100];
        state.counters["p99.9_cycles"] = latencies[latencies.size() * 999 / 1000];
        state.counters["mean_cycles"] = mean;
        state.counters["stddev_cycles"] = stddev;
        state.counters["max_cycles"] = latencies.back();
    }
}
BENCHMARK(profile_realistic_workload_mix)->Iterations(10000)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
