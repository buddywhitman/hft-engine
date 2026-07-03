#include <benchmark/benchmark.h>
#include "mpsc_queue.hpp"
#include "tsc_clock.hpp"
#include <thread>
#include <vector>

using namespace hft;

static void mpsc_single_producer(benchmark::State& state) {
    MpscQueue<uint64_t> q;

    for (auto _ : state) {
        for (int i = 0; i < 1000; ++i) {
            q.push(i);
        }
        while (q.pop()) {
        }
    }
}
BENCHMARK(mpsc_single_producer)->Unit(benchmark::kNanosecond);

static void mpsc_multi_producer(benchmark::State& state) {
    MpscQueue<uint64_t> q;
    const int num_threads = 4;

    for (auto _ : state) {
        std::vector<std::thread> producers;

        for (int t = 0; t < num_threads; ++t) {
            producers.emplace_back([&q, t]() {
                for (int i = 0; i < 250; ++i) {
                    q.push(t * 10000 + i);
                }
            });
        }

        for (auto& t : producers)
            t.join();

        int count = 0;
        while (q.pop()) {
            count++;
        }
    }
}
BENCHMARK(mpsc_multi_producer)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
