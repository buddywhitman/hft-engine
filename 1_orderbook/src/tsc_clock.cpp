#include "tsc_clock.hpp"
#include <time.h>

namespace hft {

void TscClock::calibrate() {
    // Measure TSC frequency by comparing against clock_gettime().
    // Run for ~10ms to get a stable measurement.

    const uint64_t iterations = 100'000;
    uint64_t tsc_start = __rdtsc();

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    // Busy-loop to minimize context switches
    for (uint64_t i = 0; i < iterations; ++i) {
        __asm__ volatile("" : : : "memory");
    }

    uint64_t tsc_end = __rdtsc();
    clock_gettime(CLOCK_MONOTONIC, &ts_end);

    uint64_t tsc_delta = tsc_end - tsc_start;
    uint64_t ns_delta = (ts_end.tv_sec - ts_start.tv_sec) * 1'000'000'000 +
                        (ts_end.tv_nsec - ts_start.tv_nsec);

    if (ns_delta > 0) {
        double ticks_per_ns = static_cast<double>(tsc_delta) / ns_delta;
        ticks_per_ns_.store(ticks_per_ns, std::memory_order_release);
    }
}

uint64_t TscClock::tsc_to_ns(uint64_t tsc_delta) noexcept {
    double ticks_per_ns = ticks_per_ns_.load(std::memory_order_acquire);
    if (ticks_per_ns > 0.0)
        return static_cast<uint64_t>(tsc_delta / ticks_per_ns);
    return 0;
}

} // namespace hft
