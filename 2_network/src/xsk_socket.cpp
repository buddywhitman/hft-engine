#include "xsk_socket.hpp"
#include <iostream>
#include <cstring>
#include <sys/resource.h>
#include <net/if.h>
#include <linux/if_xdp.h>

#ifdef HAVE_LIBBPF
#include <xdp/xsk.h>
#include <xdp/libxdp.h>
#include <bpf/bpf.h>
#endif

namespace hft::net {

XskSocket::XskSocket(const XskConfig& config, PacketCallback on_fix_message)
    : config_(config), on_fix_message_(std::move(on_fix_message)),
      umem_(std::make_unique<Umem>(config.num_frames, config.frame_size)) {

#ifdef HAVE_LIBBPF
    std::cout << "[XskSocket] Initializing with AF_XDP on " << config.interface << std::endl;

    // Increase locked memory limit for UMEM
    struct rlimit rlim;
    if (getrlimit(RLIMIT_MEMLOCK, &rlim) == 0) {
        rlim.rlim_cur = rlim.rlim_max = RLIM_INFINITY;
        setrlimit(RLIMIT_MEMLOCK, &rlim);
    }

    // Get interface index
    if_index_ = if_nametoindex(config.interface.c_str());
    if (if_index_ == 0) {
        throw std::runtime_error("Interface not found: " + config.interface);
    }

    std::cout << "[XskSocket] Interface " << config.interface << " -> ifindex " << if_index_ << std::endl;
#else
    std::cout << "[XskSocket] WARNING: Built without libbpf. AF_XDP disabled." << std::endl;
    std::cout << "[XskSocket] Install: sudo apt-get install libbpf-dev" << std::endl;
#endif
}

XskSocket::~XskSocket() {
#ifdef HAVE_LIBBPF
    if (xsk_) {
        xsk_socket__close(xsk_);
    }
    if (xsk_umem_) {
        xsk_umem__delete(xsk_umem_);
    }
    if (prog_) {
        bpf_object__close(prog_);
    }
#endif
}

void XskSocket::run_rx_loop() {
#ifdef HAVE_LIBBPF
    if (!xsk_) {
        std::cerr << "[XskSocket] Not initialized. Call load_bpf_program() first." << std::endl;
        return;
    }

    std::cout << "[XskSocket] Starting RX loop on " << config_.interface << std::endl;

    uint64_t total_packets = 0;
    uint64_t total_ns = 0;
    uint64_t start_tsc = TscClock::now();

    while (true) {
        // Fill FILL ring with empty frames
        fill_rx_ring();

        // Poll RX ring
        int n_pkts = xsk_ring_cons__peek(&rx_ring_, 32, &rx_idx_);
        if (n_pkts > 0) {
            process_rx_batch(n_pkts);
            xsk_ring_cons__release(&rx_ring_, n_pkts);
            total_packets += n_pkts;
        }

        // Periodically print stats
        if (total_packets % 10000 == 0 && total_packets > 0) {
            uint64_t elapsed_ns = TscClock::tsc_to_ns(TscClock::now() - start_tsc);
            double throughput = (double)total_packets * 1e9 / elapsed_ns;
            std::cout << "[XskSocket] " << total_packets << " packets, "
                      << throughput << " pkt/s" << std::endl;
        }
    }
#else
    std::cerr << "[XskSocket] AF_XDP not available (libbpf not found)" << std::endl;
#endif
}

void XskSocket::load_bpf_program(const std::string& bpf_obj_path) {
#ifdef HAVE_LIBBPF
    std::cout << "[XskSocket] Loading BPF program: " << bpf_obj_path << std::endl;

    // Load BPF object
    prog_ = bpf_object__open(bpf_obj_path.c_str());
    if (!prog_) {
        throw std::runtime_error("Failed to open BPF object: " + bpf_obj_path);
    }

    if (bpf_object__load(prog_)) {
        throw std::runtime_error("Failed to load BPF object");
    }

    // Get UMEM
    struct xsk_umem_config umem_cfg;
    if (xsk_umem_config_init(&umem_cfg)) {
        throw std::runtime_error("Failed to init UMEM config");
    }

    // Create UMEM
    if (xsk_umem__create(&xsk_umem_, umem_->buffer(),
                        config_.num_frames * config_.frame_size,
                        &fill_ring_, &comp_ring_, &umem_cfg)) {
        throw std::runtime_error("Failed to create UMEM");
    }

    // Create XSK socket
    struct xsk_socket_config xsk_cfg;
    if (xsk_socket_config_init(&xsk_cfg)) {
        throw std::runtime_error("Failed to init XSK config");
    }

    xsk_cfg.rx_size = 2048;
    xsk_cfg.tx_size = 2048;
    xsk_cfg.xdp_flags = 0;
    xsk_cfg.bind_flags = XDP_COPY;  // Can be XDP_ZEROCOPY for production

    if (xsk_socket__create(&xsk_, config_.interface.c_str(), config_.queue_id,
                          xsk_umem_, &rx_ring_, &tx_ring_, &xsk_cfg)) {
        throw std::runtime_error("Failed to create XSK socket");
    }

    zero_copy_ = (xsk_cfg.bind_flags & XDP_ZEROCOPY);

    std::cout << "[XskSocket] AF_XDP socket created. Zero-copy: "
              << (zero_copy_ ? "yes" : "no") << std::endl;
#else
    (void)bpf_obj_path;
    std::cerr << "[XskSocket] Cannot load BPF program without libbpf" << std::endl;
#endif
}

void XskSocket::fill_rx_ring() {
#ifdef HAVE_LIBBPF
    // Get free frames from FILL ring
    uint32_t n_free = xsk_prod_ring_need_wakeup(&fill_ring_) ?
                      config_.num_frames :
                      xsk_ring_prod__reserve(&fill_ring_, 32, &fill_idx_);

    if (n_free == 0) return;

    for (uint32_t i = 0; i < n_free; ++i) {
        uint64_t frame_addr = umem_->alloc_frame();
        if (frame_addr == UMEM_INVALID) break;

        *xsk_ring_prod__fill_addr(&fill_ring_, fill_idx_ + i) = frame_addr;
    }

    xsk_ring_prod__submit(&fill_ring_, n_free);
#endif
}

void XskSocket::process_rx_batch(uint32_t n_pkts) {
#ifdef HAVE_LIBBPF
    for (uint32_t i = 0; i < n_pkts; ++i) {
        const struct xdp_desc* desc = xsk_ring_cons__rx_desc(&rx_ring_, rx_idx_ + i);

        uint64_t addr = desc->addr;
        uint32_t len = desc->len;

        uint8_t* pkt_data = static_cast<uint8_t*>(umem_->get_frame(addr));
        if (!pkt_data) continue;

        // Parse FIX message
        fix::FixMessage msg;
        uint64_t recv_tsc = TscClock::now();

        if (fix::parse(pkt_data, len, msg)) {
            pkt_count_++;
            on_fix_message_(msg, recv_tsc);
        } else {
            parse_errors_++;
        }

        // Return frame to UMEM
        umem_->free_frame(addr);
    }
#endif
}

// Non-AF_XDP fallback for systems without libbpf
void XskSocket::run_socket_fallback() {
    std::cout << "[XskSocket] Running socket-based fallback (no AF_XDP)" << std::endl;
    std::cerr << "[WARNING] Using socket instead of AF_XDP. Latency will be higher." << std::endl;
    std::cerr << "[INFO] Install libbpf-dev for AF_XDP support: sudo apt-get install libbpf-dev" << std::endl;
}

} // namespace hft::net
