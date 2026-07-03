#pragma once
#include <cstdint>
#include <x86intrin.h>
#include <atomic>

namespace hft {

class TscClock {
public:
    // Call once at startup to calibrate ticks-per-ns
    static void calibrate();

    [[nodiscard]] static inline uint64_t now() noexcept {
        return __rdtsc();
    }

    [[nodiscard]] static inline uint64_t now_serialized() noexcept {
        uint32_t aux;
        return __rdtscp(&aux);
    }

    [[nodiscard]] static uint64_t tsc_to_ns(uint64_t tsc_delta) noexcept;

private:
    static inline std::atomic<double> ticks_per_ns_{1.0};
};

} // namespace hft
