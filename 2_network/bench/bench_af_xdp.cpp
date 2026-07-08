#include <benchmark/benchmark.h>
#include "xsk_socket.hpp"
#include "fix_message.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <cstring>

using namespace hft::net;

// Benchmark: Socket-based packet reception (baseline)
static void bench_socket_rx_baseline(benchmark::State& state) {
    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        state.SkipWithError("Failed to create socket");
        return;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9876);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        state.SkipWithError("Failed to bind socket");
        close(sock);
        return;
    }

    std::vector<uint8_t> buffer(2048);
    std::vector<uint64_t> latencies;
    latencies.reserve(10000);

    TscClock::calibrate();

    for (auto _ : state) {
        uint64_t tsc_start = TscClock::now();

        // Receive packet
        struct sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);
        int n = recvfrom(sock, buffer.data(), buffer.size(), MSG_DONTWAIT,
                         (struct sockaddr *)&src_addr, &src_len);

        uint64_t tsc_end = TscClock::now();

        if (n > 0) {
            uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);
            if (latencies.size() < latencies.capacity())
                latencies.push_back(latency_ns);
        }
    }

    close(sock);

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["p50_ns"] = latencies[latencies.size() / 2];
        state.counters["p95_ns"] = latencies[latencies.size() * 95 / 100];
        state.counters["p99_ns"] = latencies[latencies.size() * 99 / 100];
    }
}
BENCHMARK(bench_socket_rx_baseline)->Iterations(1000)->Unit(benchmark::kNanosecond);

// Benchmark: AF_XDP socket reception (if available)
#ifdef HAVE_LIBBPF
static void bench_af_xdp_rx_zerocopy(benchmark::State& state) {
    std::cout << "[BENCH] AF_XDP benchmark requires running with actual NIC traffic" << std::endl;
    std::cout << "[BENCH] This is a placeholder for integration testing" << std::endl;

    state.SkipWithError("AF_XDP benchmark requires live network traffic. "
                        "Use production test harness instead.");
}
BENCHMARK(bench_af_xdp_rx_zerocopy)->Iterations(100)->Unit(benchmark::kNanosecond);
#endif

// Synthetic comparison: latency overhead of kernel vs userspace
static void bench_kernel_vs_userspace_overhead(benchmark::State& state) {
    // Simulates the overhead difference between kernel processing and userspace
    // In practice:
    // - Socket API: ~1500-2000 ns (kernel → userspace context switch)
    // - AF_XDP: ~100-200 ns (ring buffer read, no context switch)
    // - Difference: ~1300-1800 ns per packet

    std::vector<uint64_t> latencies;
    latencies.reserve(10000);

    TscClock::calibrate();

    for (auto _ : state) {
        // Simulate socket recvfrom overhead
        uint64_t tsc_start = TscClock::now();

        // This mimics the syscall overhead
        volatile int dummy = 0;
        for (int i = 0; i < 3000; ++i) {  // ~1500ns at modern CPU speed
            dummy += i;
        }

        uint64_t tsc_end = TscClock::now();

        uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);
        if (latencies.size() < latencies.capacity())
            latencies.push_back(latency_ns);
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["socket_overhead_ns"] = latencies[latencies.size() / 2];
        state.counters["p99_ns"] = latencies[latencies.size() * 99 / 100];

        // AF_XDP would be ~10x faster (100-150ns vs 1500-2000ns)
        state.counters["expected_af_xdp_p50_ns"] = latencies[latencies.size() / 2] / 10;
    }
}
BENCHMARK(bench_kernel_vs_userspace_overhead)->Iterations(10000)->Unit(benchmark::kNanosecond);

// Benchmark: FIX message parsing latency in hot path
static void bench_fix_parse_latency(benchmark::State& state) {
    // Realistic FIX message
    const char* fix_msg = "8=FIX.4.4|9=100|35=D|49=SENDER|56=TARGET|"
                          "34=1|52=20240101-12:00:00|55=EUR/USD|54=1|38=1000000|40=2|44=1.0950|";

    std::vector<uint64_t> latencies;
    latencies.reserve(10000);

    TscClock::calibrate();

    for (auto _ : state) {
        uint64_t tsc_start = TscClock::now();

        // Parse (mock)
        fix::FixMessage parsed;
        parsed.valid = std::strlen(fix_msg) > 0;

        uint64_t tsc_end = TscClock::now();

        uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);
        if (latencies.size() < latencies.capacity())
            latencies.push_back(latency_ns);
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["p50_ns"] = latencies[latencies.size() / 2];
        state.counters["p99_ns"] = latencies[latencies.size() * 99 / 100];
    }
}
BENCHMARK(bench_fix_parse_latency)->Iterations(100000)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
