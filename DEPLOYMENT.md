# HFT Engine: Production Deployment Guide

This guide covers deployment of the HFT engine for production trading environments. Targets Linux with real-time kernel patches and high-performance hardware.

## System Requirements

### Hardware

| Component | Recommended | Notes |
|-----------|-------------|-------|
| CPU | Intel Xeon E5/E7 or AMD EPYC | 3.5+ GHz, dedicated cores |
| RAM | 32+ GB DDR4/DDR5 | NUMA-aware allocation critical |
| NIC | 10G+ Ethernet | Low-latency vendors: Mellanox/Intel 82599 |
| Storage | NVMe SSD | For logging/snapshot only, not hot path |

### Software Stack

- **OS**: Linux 5.10+ (Ubuntu 22.04 LTS recommended)
- **Kernel**: Real-time patched kernel (PREEMPT_RT)
- **Compiler**: GCC 11+ / Clang 14+
- **libbpf**: 0.7+ (for AF_XDP kernel-bypass)
- **libxdp**: 1.0+ (optional, for advanced XDP features)

## Build for Production

### 1. Kernel Configuration

Enable real-time kernel patches for predictable latency:

```bash
# Download real-time kernel patch
linux_version="5.10.0"
rt_patch_url="https://www.kernel.org/pub/linux/kernel/projects/rt/5.10/patch-${linux_version}-rt.patch.gz"

# Configure kernel with PREEMPT_RT
./configure
# In menuconfig, enable:
# - CONFIG_PREEMPT_RT=y
# - CONFIG_NO_HZ_FULL=y (disable tick on isolated CPUs)
# - CONFIG_RCU_NOCB_CPU=y (RCU callbacks on isolated CPUs)
# - CONFIG_NUMA=y (if multi-socket)
```

### 2. System Tuning

```bash
#!/bin/bash
# Production system tuning for HFT

# Increase locked memory (for UMEM in AF_XDP)
echo "* soft memlock unlimited" >> /etc/security/limits.conf
echo "* hard memlock unlimited" >> /etc/security/limits.conf

# CPU frequency scaling: set to performance
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Disable C-states (CPU power saving) for latency
for i in /sys/devices/system/cpu/cpu*/cpuidle/state*/disable; do
    echo 1 > "$i"
done

# Increase socket buffer sizes
sysctl -w net.core.rmem_max=134217728
sysctl -w net.core.wmem_max=134217728
sysctl -w net.ipv4.tcp_rmem="4096 87380 134217728"
sysctl -w net.ipv4.tcp_wmem="4096 65536 134217728"

# Disable RX/TX offloads (process packets in software for consistency)
ethtool -K eth0 rx-generic-receive-offload off
ethtool -K eth0 tx-generic-segmentation-offload off

# Set MTU for jumbo frames (if exchange supports)
ip link set dev eth0 mtu 9000

# CPU affinity: pin to isolated cores
taskset -c 2-5 ./hft_engine
```

### 3. Build with Maximum Optimization

```bash
cd hft-engine
rm -rf build
mkdir build && cd build

# Configure with production flags
cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=native -funroll-loops -flto -DNDEBUG" \
    -DENABLE_PROFILING=ON \
    ..

# Build
ninja

# Verify libbpf integration
ldd ./2_network/bench_af_xdp | grep libbpf
```

## AF_XDP Setup (Kernel-Bypass)

### 1. Build BPF Program

```bash
cd 2_network
chmod +x build_bpf.sh
./build_bpf.sh

# Verify:
llvm-objdump -d bpf/build/xdp_fix_redirect.o
```

### 2. Load BPF Program

```bash
# Requires CAP_BPF and CAP_NET_ADMIN privileges
# On production systems with SELinux/AppArmor, configure accordingly

# Method 1: ip link (simple)
ip link set dev eth0 xdp obj ./bpf/build/xdp_fix_redirect.o

# Method 2: iw (requires libiw)
iw phy phy0 set netns 1

# Verify:
ip link show eth0
# Should show: xdp/id:N (where N is the program ID)
```

### 3. Configure XDP Socket

```cpp
hft::net::XskConfig config;
config.interface = "eth0";
config.fix_port = 9876;
config.num_frames = 4096;
config.frame_size = 2048;
config.force_zero_copy = true;

hft::net::XskSocket xsk(config, [](const fix::FixMessage& msg, uint64_t tsc) {
    // Process FIX message
    // tsc: TSC timestamp of packet arrival
});

xsk.load_bpf_program("./bpf/build/xdp_fix_redirect.o");
xsk.run_rx_loop();  // Busy-polls, never yields
```

## Order Book Configuration

### 1. Memory Pre-allocation

```cpp
// Pre-allocate all order structures (zero allocation in hot path)
hft::OrderBook ob(
    1,  // instrument ID
    [](const hft::MatchResult& m) {
        // Handle matches
        send_execution(m);
    }
);

// Reserve memory upfront (typical: 10-50k orders)
ob.reserve(50000);
```

### 2. Performance Tuning

```cpp
// Pin CPU core for NUMA awareness
hft::pin_thread(2);  // Core 2

// Bind memory to NUMA node
hft::bind_memory_numa(0);  // NUMA node 0

// Run order book processing
while (true) {
    // Busy-poll (no yields to OS scheduler)
    auto [msg, tsc] = receive_from_xsk();
    auto matches = ob.process(msg);
    send_outbound(matches);
}
```

## Profiling and Validation

### 1. Run Full Test Suite

```bash
cd hft-engine/build
../scripts/full_test.sh

# Results in: results/[timestamp]/
# - benchmarks_log.txt: Raw latencies
# - perf_stat_*.txt: Cache/branch analysis
# - system_info.txt: Hardware configuration
```

### 2. Analyze Benchmarks

```bash
# Generate performance report
python3 << 'EOF'
import json
import glob

results = {}
for log in glob.glob("results/*/perf_stat_*.txt"):
    with open(log) as f:
        content = f.read()
        # Parse perf stat output
        # Extract: cache-misses, branch-misses, context-switches, IPC

print("Performance Summary:")
print(f"  SPSC P99: {results.get('spsc_p99_ns', 'N/A')} ns")
print(f"  Order Book P99: {results.get('orderbook_p99_ns', 'N/A')} ns")
print(f"  AF_XDP E2E: {results.get('af_xdp_e2e_ns', 'N/A')} ns")
EOF
```

### 3. Production Validation Checklist

- [ ] Build completes with 0 warnings
- [ ] All unit tests pass
- [ ] Benchmarks show stable latencies (stddev < 10% of mean)
- [ ] No context switches during hot path (perf stat shows ~0)
- [ ] Cache miss rate < 5% (L1 + L2 mostly hits)
- [ ] Branch prediction rate > 95%
- [ ] AF_XDP shows 10x better latency than socket baseline
- [ ] System sustains 100k+ orders/sec with <1% drops
- [ ] Tail latencies (P99.9) acceptable for venue (typically <10µs)

## Monitoring and Observability

### 1. Deployment Monitoring

```cpp
struct HftEngineMetrics {
    uint64_t orders_processed = 0;
    uint64_t orders_matched = 0;
    uint64_t parse_errors = 0;
    uint64_t network_drops = 0;
    
    // Latency tracking
    uint64_t p50_ns = 0, p99_ns = 0, p99_9_ns = 0, max_ns = 0;
};

// Log metrics periodically (no allocation):
HftEngineMetrics metrics{};
char metric_buffer[1024];
snprintf(metric_buffer, sizeof(metric_buffer),
    "orders=%llu matched=%llu p99=%llu max=%llu",
    metrics.orders_processed, metrics.orders_matched,
    metrics.p99_ns, metrics.max_ns);
// Send to monitoring system (Prometheus, etc.)
```

### 2. Runtime Logging (Zero Allocation)

```cpp
// Pre-allocated ring buffer for logging (no malloc in hot path)
class RingLogger {
    static const size_t MAX_LOGS = 1000000;
    uint8_t buffer[MAX_LOGS * 64];  // 64-byte entries
    std::atomic<uint64_t> write_pos{0};
    
public:
    void log(const char* fmt, ...) noexcept {
        // Lock-free append to ring buffer
        uint64_t pos = write_pos.fetch_add(1);
        // Format message into buffer[pos % capacity]
    }
};
```

## Disaster Recovery

### 1. State Persistence

```cpp
// Periodic snapshots of order book state
void snapshot_order_book(const OrderBook& ob, const std::string& filename) {
    FILE* f = fopen(filename.c_str(), "wb");
    // Serialize all active orders
    // Write position tracking
    fclose(f);
}

// Recover on restart
OrderBook recover_from_snapshot(const std::string& filename) {
    OrderBook ob(1, [](const MatchResult&) {});
    FILE* f = fopen(filename.c_str(), "rb");
    // Deserialize orders
    fclose(f);
    return ob;
}
```

### 2. Graceful Shutdown

```cpp
void shutdown_handler(int signal) {
    // Signal handler called on SIGTERM/SIGINT
    
    // 1. Stop accepting new orders
    accepting_orders = false;
    
    // 2. Drain in-flight messages (10 seconds max)
    auto deadline = now() + 10s;
    while (!order_queue.empty() && now() < deadline) {
        process_pending();
    }
    
    // 3. Flush any pending matches
    flush_outbound();
    
    // 4. Snapshot state
    snapshot_order_book(order_book, "snapshot.bin");
    
    // 5. Log statistics
    log_metrics();
    
    // 6. Exit cleanly
    exit(0);
}
```

## Security Considerations

1. **Capability Restrictions**: AF_XDP requires CAP_BPF. Run with minimal privileges.
2. **Memory Safety**: Pre-allocated pools prevent heap corruption.
3. **Timeout Enforcement**: No unbounded loops in hot path.
4. **Audit Logging**: All orders logged with audit trail for compliance.

## Support and Troubleshooting

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| "Permission denied" on AF_XDP | Missing CAP_BPF | Run with sudo or configure capabilities |
| High cache misses | NUMA mismatch | Bind threads/memory to same NUMA node |
| Context switches | OS scheduler | Use PREEMPT_RT kernel + taskset -c |
| Packet loss | UMEM exhaustion | Increase num_frames in XskConfig |

### Performance Regression

```bash
# Compare benchmark results over time
git log --oneline | head -20  # Find commits
git checkout <commit>
./build/1_orderbook/bench_profiling > baseline.txt
git checkout master
./build/1_orderbook/bench_profiling > current.txt

# Compare
diff <(sort baseline.txt) <(sort current.txt)
```

## References

- Linux AF_XDP: https://www.kernel.org/doc/html/latest/networking/af_xdp.html
- libbpf documentation: https://libbpf.readthedocs.io/
- Realtime Linux: https://wiki.linuxfoundation.org/realtime/start
- FIX Protocol: https://www.fixtrading.org/
