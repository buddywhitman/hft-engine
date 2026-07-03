#pragma once
#include "umem.hpp"
#include "fix_message.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <memory>

namespace hft::net {

struct XskConfig {
    std::string interface;
    uint16_t    fix_port;
    uint32_t    queue_id = 0;
    uint32_t    num_frames = 4096;
    uint32_t    frame_size = 2048;
    bool        force_zero_copy = false;
};

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

    std::unique_ptr<Umem> umem_;
    // AF_XDP socket structures would go here (requires libbpf/libxdp)
};

} // namespace hft::net
