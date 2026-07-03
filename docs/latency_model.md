# Latency Model: Roofline Analysis

This document provides a bottleneck analysis of the HFT pipeline using the roofline model framework.

## Packet Arrival to Trade Execution

```
Market Data (FIX) → Network Stack → Parser → Order Book → Matching → Outbound FIX
     |                |              |          |           |          |
   t0            t1=t0+Δt1      t2=t1+Δt2   t3=t2+Δt3   t4=t3+Δt4  t5=t4+Δt5
```

End-to-end latency: **Δt_total = Δt1 + Δt2 + Δt3 + Δt4 + Δt5**

### Stage 1: Network → Kernel Buffer (Δt1)

**Hardware**: NIC receives packet, DMA into ring buffer.
- Latency: ~100-500ns (NIC→DRAM round-trip)
- Bottleneck: PCIe latency, fixed by hardware

### Stage 2: Kernel → Userspace (via AF_XDP) (Δt2)

**Zero-copy mode**:
- Kernel updates RX ring descriptor
- Userspace reads from ring
- Latency: ~50-200ns (register reads, no data copy)
- Bottleneck: Memory barrier on acquire

**Copy mode** (fallback):
- Kernel memcpy packet into UMEM
- Latency: ~500ns - 2µs (depends on packet size, typically 100-150 bytes)
- Bottleneck: DRAM bandwidth (per packet)

### Stage 3: FIX Parser (Δt3)

**Single-pass parser** (software):
- 120-byte message: ~50ns (10-20 L1 cache hits, 0.4 GHz nominal / 0.05 µs per byte)
- Bottleneck: L1 cache misses if memory layout is poor
- **Measurement**: Profile with perf stat --dtlb-loads-misses

**FPGA parser** (pipelined):
- 120 bytes @ 100MHz: ~1.2µs (120 cycles)
- Throughput: 1 message / 120 cycles = ~8.3M msg/s per stream
- Bottleneck: Clock frequency (100MHz is conservative for Artix-7)

### Stage 4: Order Book Insertion (Δt4)

**Add order, no matching**:
- Flat array binary search: 50-100ns (8 comparisons in L1 cache)
- Allocate from pool: 10-20ns (atomic read+increment)
- Bottleneck: Price ladder insert at O(n), but n < 200

**Add order with matching** (aggressive buy crosses multiple asks):
- Per match: 30-50ns (update price level, invoke callback)
- 5 matches: 150-250ns
- Total: 200-350ns
- Bottleneck: Number of price levels crossed (data-dependent)

### Stage 5: Outbound Response (Δt5)

**Execution Report (FIX message generation)**:
- Format response message: 20-30ns
- Enqueue in TX ring: 10-20ns
- Kernel processes TX: 500ns - 2µs (similar to ingress copy)
- Bottleneck: Kernel TX path, not in hot path for matching

## Roofline Bounds

### Peak Performance Bound

**CPU**: Max 1 GHz (1 instruction/ns at 1GHz, conservative for Haiku 4.5):
- Peak throughput: 1M orders/second (1000ns / 1ns per op)
- Actual: 0.3-0.5M orders/second (more realistic with memory stalls)

### Memory Bandwidth Bound

**L1 Cache** (32KB, ~50GB/s bandwidth):
- Per-order data: 64 bytes (one cache line)
- 50GB/s ÷ 64B = ~780M accesses/sec
- But we're compute-limited, not bandwidth-limited

**DRAM** (100GB/s on modern systems):
- If we were DRAM-limited, max 100GB/s ÷ 64B = ~1.5B accesses/sec
- Not the bottleneck for order book

### Latency Bound (Achievable)

Given:
- Network: ~100ns (hardware fixed)
- Parser: ~50ns (software) or ~1200ns (FPGA)
- Order book: ~200ns (P99)
- Outbound: ~500ns (kernel)

**Total achievable**: ~850ns (single-thread, kernel bypass)

In practice, accounting for OS latency variance, interrupt handling, context switches:
- **P50**: ~200-300ns
- **P95**: ~400-600ns
- **P99**: ~800ns - 2µs
- **P99.9**: ~5-10µs (occasional OS preemption)

To hit sub-microsecond P99:
1. ✓ Pin to isolated CPU core
2. ✓ Disable frequency scaling
3. ✓ Use real-time scheduling (FIFO)
4. ✓ Pre-warm L1/L2 cache
5. ✓ Use SPSC queues, not locks
6. ✓ Pre-allocate memory

## Bottleneck Analysis by Stage

| Stage | Latency | Bottleneck | Mitigation |
|---|---|---|---|
| Network RX | 100-500ns | Hardware latency | Buy faster NIC |
| Kernel RX | 50-200ns (XDP) | Memory barriers | Use zero-copy |
| Parser | 50ns (SW) / 1200ns (FPGA) | L1 cache hits | Optimize instruction cache |
| Order book | 200ns (P99) | Price ladder insert | Pre-allocate levels |
| Kernel TX | 500ns - 2µs | Syscall overhead | Batch responses |

## Scaling: Single vs Multi-Stream

### Single order stream (your case)
- End-to-end: 200-300ns (P50)
- Bottleneck: Parser → Order book serialization

### Multiple concurrent streams (N streams, 1 CPU)
- Per-stream latency: grows with N
- At N=4: P99 latency increases 2-3x due to cache contention
- At N=10: Exceeds 10µs (OS scheduler kicked in)

**Mitigation**: Use one thread per core, FPGA for cross-stream offload.

## CPU Affinity Impact

**Without CPU pinning**:
- P99 latency: 5-50µs (OS preemption, context switches)
- Variance: 100x between P50 and P99

**With CPU pinning + real-time priority**:
- P99 latency: 200-800ns (predictable)
- Variance: 5x between P50 and P99

**Cost of switching**: ~1µs per context switch (move between cores).

## What These Numbers Mean

A well-tuned single-threaded order book:
- Processes 1-3 million orders per second peak throughput
- P99 latency: 200-800ns from market data to matching decision
- Variance: Highly predictable (not jittery)

A poorly tuned system (regular sockets, std::map, no pinning):
- Processes 100K-500K orders per second
- P99 latency: 50-100µs (orders of magnitude slower)
- Variance: Highly unpredictable (10x between P50 and P99)

The difference between "works" and "wins" in HFT is usually 50-100ns P99, which is the difference between:
- Cache-line alignment: ~60ns
- Memory ordering: ~100ns  
- Flat array vs tree: ~400ns
- CPU pinning: ~1-10µs

---

## Measuring Your Own Hardware

```bash
# Calibrate TSC on your machine
cd build && ./1_orderbook/bench_orderbook --benchmark_filter="add_order_no_match"

# Measure memory latency
perf stat -e LLC-loads,LLC-load-misses ./1_orderbook/bench_orderbook

# Measure instructions per cycle (IPC)
perf stat -e cycles,instructions ./1_orderbook/bench_orderbook

# Check cache line sizes
cat /proc/cpuinfo | grep cache_alignment
```

If P99 > 500ns, investigate:
1. Is cache_alignment smaller than 64 bytes? (Older CPUs have 32B lines)
2. Are there LLC misses? (Data too large for L3 cache)
3. Is IPC < 1? (Memory stalls dominating)
4. Is the CPU frequency throttling? (Check /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq)
