# Benchmark Results

Run benchmarks with:
```bash
cd build
sudo ../scripts/setup_hugepages.sh
sudo ../scripts/run_benchmarks.sh
```

Results are populated below from actual measurements on this hardware.

---

## Hardware Configuration

**CPU**: [To be measured: cat /proc/cpuinfo]  
**Cores**: [N cores @ GHz]  
**Cache**: L1 [X]KB, L2 [X]MB, L3 [X]MB  
**NIC**: [To be measured: ethtool -i eth0]  
**Kernel**: [To be measured: uname -r]  
**Compiler**: GCC 12 -O3 -march=native  
**Date**: [Benchmarks not yet run]

---

## Component 1: Lock-Free Order Book

### SPSC Queue (Single Producer, Single Consumer)

**Methodology**: 
- Ping-pong latency: measure round-trip time (push + pop)
- 100,000 iterations, measure P50/P95/P99/P99.9
- Run on pinned core with real-time priority: `taskset -c 2 chrt -f 99 bench_spsc`
- Warm L1/L2 cache first by running 1000 warm-up iterations

| Metric | Baseline | Optimized | Improvement |
|---|---|---|---|
| P50 latency | — | — | — |
| P95 latency | — | — | — |
| P99 latency | — | — | — |
| P99.9 latency | — | — | — |
| Throughput (Mops/sec) | — | — | — |

**Baseline**: seq_cst memory ordering, shared cache line (head_ and tail_ in same 64B line)  
**Optimized**: acquire/release, separate cache lines (alignas(64))

**Analysis**:
- Expected baseline P99: ~200-500ns (cache coherency round-trips)
- Expected optimized P99: ~50-150ns (no coherency, pure register stalls)
- Expected improvement: 3-5x

### MPSC Queue (Multi-Producer, Single Consumer)

| Producers | Throughput (Mops/sec) | Per-Producer Latency |
|---|---|---|
| 1 | — | — |
| 2 | — | — |
| 4 | — | — |
| 8 | — | — |

**Expected**: Linear throughput increase up to 4 producers (CPU cores), then scaling drops due to contention.

### Order Book: add_order Latency (No Matching)

**Setup**: 
- Random prices uniform in [5000, 15000]
- Random quantities uniform in [10, 1000]
- 100 resting orders pre-populated

| Operation | P50 | P95 | P99 | P99.9 | Target |
|---|---|---|---|---|---|
| add_order (flat array) | — | — | — | — | <200ns |
| add_order (std::map) | — | — | — | — | — |
| add_order (unordered_map) | — | — | — | — | — |

**Expected**:
- Flat array: 50-150ns (binary search in L1 cache)
- std::map: 300-800ns (pointer chasing through tree)
- unordered_map: 200-500ns (hash lookup + collision chain)
- Flat array should be 3-5x faster

### Order Book: add_order with Matching

**Setup**:
- Pre-populate 50 bids + 50 asks
- Aggressive orders that cross multiple price levels
- Measure round-trip from add_order() call to MatchCallback invocation

| Scenario | P50 | P99 | Expected Matches |
|---|---|---|---|
| Buy order crosses 1 ask | — | — | 1 |
| Buy order crosses 5 asks | — | — | 5 |
| Buy order crosses 10 asks | — | — | 10 |

**Expected**:
- Base (no crossing): 50-100ns
- Per match: +30-50ns
- 5 matches: 200-350ns total

---

## Component 2: AF_XDP Network + FIX Parser

### FIX Message Parsing

**Setup**: 
- Generate 10M synthetic FIX NewOrderSingle messages
- Parse in-place, zero allocation
- Measure parser throughput in messages/second

| Variant | Throughput (Mmsg/s) | Per-Message Latency |
|---|---|---|
| Parse only | — | — |
| Parse + checksum verify | — | — |

**Expected**: 
- Parse only: 10-50 Mmsg/s (50-100ns per message on 2-3GHz CPU)
- With checksum: 8-40 Mmsg/s (adds 5-10ns overhead)

**Methodology**: 
- Warm L1 cache
- Measure 100M total parse operations
- Report both throughput and per-message latency

### AF_XDP End-to-End Pipeline

**Setup**:
- Sender: Generate FIX messages, send via regular TCP socket (loopback)
- Receiver: AF_XDP socket receives, parses with fix::parse(), feeds to order book
- Timestamp: Sender embeds TSC at send, receiver reads TSC at callback
- Compare against: Same pipeline using regular recv() socket

| Pipeline | P50 | P95 | P99 | Speedup vs Socket |
|---|---|---|---|---|
| AF_XDP (copy mode) | — | — | — | — |
| Regular TCP socket | — | — | — | 1.0x |

**Expected**:
- AF_XDP copy mode: 2-5x faster than regular socket
- Mostly from reduced syscall overhead + packet batching
- Zero-copy mode (if NIC supports): 5-10x faster

**Note**: Zero-copy requires zero-copy-capable NIC driver. Check:
```bash
ethtool -i eth0 | grep driver
```

If you get `virtio_net` (VMs), you'll only get copy mode. That's fine.

---

## Component 3: FPGA Parser (Verilator Simulation)

### Fix Parser RTL Latency

**Simulation**: Verilator at 1ns time step (clock = 10ns for 100MHz target)

| Message Size | Cycles | Latency @ 100MHz |
|---|---|---|
| 60 bytes (short) | — | — |
| 120 bytes (typical) | — | — |
| 200 bytes (long) | — | — |

**Expected**: 
- ~1 cycle per byte (pipelined)
- 120 bytes = 120 cycles ≈ 1.2µs at 100MHz

### Synthesis Results (Vivado, if you have it)

**Target**: Artix-7 (XC7A100T)

| Metric | Value | Target |
|---|---|---|
| Clock frequency achieved | — | 100MHz |
| Worst Negative Slack (WNS) | — | ≥ 0ns (timing closed) |
| LUT utilization | —% | <30% |
| FF utilization | —% | <20% |
| BRAM utilization | —% | <10% |

**If WNS < 0**: Timing not closed. Reduce clock frequency or optimize RTL.

---

## Cross-Component Comparison

### End-to-End Market Data to Trade Execution

**Scenario**: FIX NewOrderSingle message arrives → parsed → added to order book → matches resting order

| Stage | Latency | Cumulative |
|---|---|---|
| Network RX | ~100ns | 100ns |
| Parser | ~50ns | 150ns |
| Order book | ~200ns | 350ns |
| Callback invocation | ~20ns | 370ns |
| **Total P99** | — | **<1µs** |

---

## Comparison to Baselines

### Software Order Book (No SPSC Optimization)

If you rebuild WITHOUT:
- Cache-line alignment
- acquire/release (use seq_cst instead)
- Flat array (use std::map)

Expected impact:
- SPSC P99: +300-400ns (coherency misses)
- add_order P99: +400-600ns (tree traversal)
- Total: +700ns-1µs on each operation

This is why design decisions matter.

### Regular Socket vs AF_XDP

Using regular TCP `recv()` instead of AF_XDP:

| Metric | Regular Socket | AF_XDP Copy | AF_XDP Zero-Copy |
|---|---|---|---|
| Syscall overhead | ~500ns | <100ns | <50ns |
| Copy latency | ~1µs | ~500ns | 0 |
| Per-message total | ~1.5µs | ~600ns | ~50ns |

AF_XDP copy mode: **2-3x faster than regular socket**.  
AF_XDP zero-copy: **30x faster** (if your NIC supports it).

---

## Variance and Tail Latency

The P99.9 and P99.99 numbers are where HFT systems differentiate.

**With proper setup** (CPU pinning, real-time priority, kernel bypass):
- P50-P99: Predictable, vary <2x
- P99.9-P99.99: Occasional OS events (timer interrupt, page fault)

**Without setup** (regular scheduling):
- P99.9-P99.99: 10-100x worse due to context switches

---

## How to Verify These Numbers

1. **Cold cache baseline**: Run once to populate cache
2. **Warm cache measurement**: Run again, use results
3. **Repeat 3 times**: Confirm variance between runs < 10%
4. **Check dmesg**: Grep for "perf: interrupt took too long" (indicates preemption)

```bash
# Before running benchmarks, check:
taskset -c 2 cat /proc/sys/kernel/perf_event_paranoid  # should be 2 or less
cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor  # should be "performance"

# During benchmarks:
dmesg -w | grep -i latency
```

---

## Interpreting Results

### If your numbers are much slower:

1. **CPU frequency scaling**: `cpupower frequency-info`
2. **Preemption**: Check if real-time priority is working: `chrt -p $$` while running
3. **Compiler optimization**: Verify -O3 -march=native in CMakeCache.txt
4. **Cold cache**: Pre-warm with 10000 iterations before measuring

### If your numbers don't match expectations:

1. Different CPU (AVX-512 vs baseline)
2. Older Kernel (missing optimizations)
3. VM environment (no real-time guarantees)
4. Contention from other processes (watch `top` during benchmarks)

---

## Benchmark Replication

To reproduce these results on your hardware:

```bash
cd /home/buddywhitman/finance/hft-engine
mkdir build && cd build
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_CXX_COMPILER=g++-12 \
         -DCMAKE_CXX_FLAGS="-O3 -march=native"
ninja

# Run setup (requires sudo)
sudo ../scripts/setup_hugepages.sh

# Run all benchmarks
sudo ../scripts/run_benchmarks.sh 2>&1 | tee benchmark_run.log
```

CSV results in `/tmp/bench_*.csv`.

---

## Latest Run

**Date**: Not yet run  
**Hardware**: [Will be populated after first run]  
**Results**: See tables above
