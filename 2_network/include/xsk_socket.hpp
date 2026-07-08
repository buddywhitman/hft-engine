#pragma once
#include "umem.hpp"
#include "fix_message.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <memory>
#include <stdexcept>

#ifdef HAVE_LIBBPF
#include <xdp/xsk.h>
#include <bpf/bpf.h>
#endif

namespace hft::net {

struct XskConfig {
    std::string interface;      // e.g., "eth0"
    uint16_t    fix_port = 0;   // FIX destination port (for BPF filter)
    uint32_t    queue_id = 0;   // RX queue ID
    uint32_t    num_frames = 4096;   // UMEM frames
    uint32_t    frame_size = 2048;   // Bytes per frame
    bool        force_zero_copy = false;
};

// TSC (Time Stamp Counter) for ultra-low latency measurements
class TscClock {
public:
    static uint64_t now() noexcept {
        uint32_t lo, hi;
        asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
        return ((uint64_t)hi << 32) | lo;
    }

    static void calibrate() noexcept {
        // Calibrate TSC to nanoseconds
        // On most modern systems: 1 TSC tick ≈ 1/cpu_freq ns
        // This is system-specific and ideally read from /proc/cpuinfo
        calibrated_ = true;
    }

    static uint64_t tsc_to_ns(uint64_t tsc_delta) noexcept {
        // On a 3.0 GHz CPU: 1 TSC = 0.333 ns ≈ tsc_delta / 3
        // This is approximate; production code should calibrate to CPU frequency
        // Conservative estimate: assume 2 GHz
        return (tsc_delta * 1000) / 2000;
    }

private:
    static bool calibrated_;
};

// Main AF_XDP socket for zero-copy packet reception
class XskSocket {
public:
    using PacketCallback = std::function<void(const fix::FixMessage&, uint64_t recv_tsc)>;

    explicit XskSocket(const XskConfig& config, PacketCallback on_fix_message);
    ~XskSocket();

    XskSocket(const XskSocket&) = delete;
    XskSocket& operator=(const XskSocket&) = delete;

    // Main RX loop: busy-polls, never yields to OS
    void run_rx_loop();

    // Load and attach BPF redirect program
    void load_bpf_program(const std::string& bpf_obj_path);

    // Socket fallback (no AF_XDP)
    void run_socket_fallback();

    [[nodiscard]] bool is_zero_copy() const noexcept { return zero_copy_; }
    [[nodiscard]] uint64_t packets_received() const noexcept { return pkt_count_; }
    [[nodiscard]] uint64_t parse_errors() const noexcept { return parse_errors_; }

private:
    void fill_rx_ring();
    void process_rx_batch(uint32_t n_pkts);

    XskConfig config_;
    PacketCallback on_fix_message_;
    bool zero_copy_ = false;
    uint64_t pkt_count_ = 0;
    uint64_t parse_errors_ = 0;
    uint32_t if_index_ = 0;

    std::unique_ptr<Umem> umem_;

#ifdef HAVE_LIBBPF
    struct xsk_socket* xsk_ = nullptr;
    struct xsk_umem* xsk_umem_ = nullptr;
    struct xsk_ring_cons rx_ring_;
    struct xsk_ring_prod fill_ring_;
    struct xsk_ring_prod tx_ring_;
    struct xsk_ring_cons comp_ring_;
    uint32_t rx_idx_ = 0;
    uint32_t fill_idx_ = 0;
    struct bpf_object* prog_ = nullptr;
#endif
};

} // namespace hft::net
