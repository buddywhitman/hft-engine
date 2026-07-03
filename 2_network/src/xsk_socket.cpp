#include "xsk_socket.hpp"
#include <iostream>

namespace hft::net {

XskSocket::XskSocket(const XskConfig& config, PacketCallback on_fix_message)
    : config_(config), on_fix_message_(std::move(on_fix_message)),
      umem_(std::make_unique<Umem>(config.num_frames, config.frame_size)) {
    // AF_XDP socket creation would happen here
    // For now, this is a stub for the interface
}

XskSocket::~XskSocket() {
    // Cleanup would happen here
}

void XskSocket::run_rx_loop() {
    // Busy-poll loop would go here
    // In a real implementation, this would:
    // 1. Busy-poll on the RX ring
    // 2. Read packets from UMEM
    // 3. Parse with fix::parse()
    // 4. Invoke on_fix_message_ callback
}

void XskSocket::load_bpf_program(const std::string& bpf_obj_path) {
    // Would load and attach BPF program using libbpf
    (void)bpf_obj_path;
}

void XskSocket::fill_rx_ring() {
    // Add free frames to FILL ring so kernel knows where to put packets
}

void XskSocket::process_rx_batch(uint32_t n_pkts) {
    (void)n_pkts;
    // Read packets from RX ring and invoke callbacks
}

} // namespace hft::net
