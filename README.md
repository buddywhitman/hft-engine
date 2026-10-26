# HFT Engine: Lock-Free Order Book Matching System

Latency-focused order book matching engine in C++20: a wait-free SPSC ingress queue, an
allocation-free order book and a zero-copy FIX parser, with an AF_XDP/eBPF ingress path and a
Verilog FIX parser for FPGA experiments. It is a component-level engine, not a complete trading
system (see [Limitations](#what-this-is-not)).

## Overview

- **Synchronization:** wait-free SPSC queue (acquire/release), lock-free MPSC queue (Vyukov), plus a hazard-pointer reclamation utility
- **Predictable latency:** no allocations in the hot path, cache-line aligned data structures
- **Deep books:** insert latency stays flat from 10 to 500 price levels
- **Hardware awareness:** 64-byte alignment, false-sharing elimination, RDTSC timing

## Architecture

```
FIX Parser (zero-copy) → SPSC Queue → Order Book Matching → Execution Callback
     120 ns/msg            15 ns p99      27 ns p99 (hot match)
```

### Component 1: Lock-Free SPSC Queue
- **Algorithm:** wait-free ring buffer with acquire/release ordering
- **Latency:** 15 ns p99 push/pop ping-pong; 57M ops/s sustained
- **Key optimization:** head and tail on separate cache lines (no false sharing)
- **Use case:** ingress thread → matching thread

### Component 2: Order Book
- **Data structure:** FlatPriceLadder (sorted vector with binary search)
- **Latency:** 27 ns p99 hot-cache match, 31 ns p99 insert into a 500-level ladder, 137 ns p99 add+cancel churn, 11.6 µs p99 worst case (market order crossing 100 levels)
- **Scalability:** O(log n) search, O(n) insert; a good fit below ~500 levels
- **Memory:** pre-allocated order pool, no heap allocation in the hot path

### Component 3: FIX Parser
- **Protocol:** FIX 4.2 (extensible)
- **Throughput:** 120 ns/msg (8.3M msg/s) parse only; 155 ns/msg (6.5M msg/s) parse + order book
- **Allocation:** zero-copy; every string field is a `string_view` into the input buffer
- **Robustness:** validates the checksum, handles edge cases

## Performance

All numbers below are measured, not estimated. Run `results/20260708_172505/` on the edge node:
Intel i5-6300U @ 2.4 GHz (2 cores / 4 threads), Linux 6.8, governor=performance,
`perf` counters enabled. Full methodology and history are in
[`BENCHMARK_VERIFICATION.md`](BENCHMARK_VERIFICATION.md).

| Benchmark | p99 |
|---|---|
| SPSC push/pop ping-pong | 15 ns (perf: IPC 1.28, cache-miss 0.74%) |
| SPSC sustained throughput | 57M ops/s |
| Order match, hot cache | 27 ns |
| Deep ladder insert (500 levels) | 31 ns |
| Multi-level match (15 levels) | 25 ns |
| Order churn (add + cancel) | 137 ns |
| Worst case: market order crossing 100 levels | 11.6 µs |
| FIX parse only | 120 ns/msg (8.3M msg/s) |
| FIX parse + order book | 155 ns/msg (6.5M msg/s) |
| Kernel `recvfrom` baseline (AF_XDP comparison target) | 1,373 ns |

A laptop-class CPU without hugepages or isolated cores, so absolute numbers would improve on
server hardware; the relative shape (flat insert cost across depth, linear worst-case cross)
is the useful part. Earlier figures from an undocumented machine (2026-07-03) are kept in
`BENCHMARK_VERIFICATION.md` for history and are superseded by this table.

No head-to-head comparison against other engines has been run on the same hardware, so this
README makes no "faster than X" claims.

## Technical Highlights

### Memory Ordering
- **SPSC queue:** acquire/release (sufficient for single producer/consumer)
- **Order pool:** atomic operations with proper barriers
- **Why not seq_cst:** a full fence per operation is unnecessary for one producer and one consumer (not separately benchmarked here)

### Cache Optimization
- **Cache-line alignment:** 64-byte alignment for head/tail pointers
- **False sharing elimination:** Separate cache lines for contended data
- **L1 footprint:** 48 KB L1 cache holds order book for typical depths

### Lock-Free Correctness
- **No mutexes, spinlocks, or read-write locks** in hot path
- **All unit tests pass:** 540 assertions in 25 test cases (SPSC, MPSC, order book, FIX parser)

## Building

```bash
cd build
cmake .. -GNinja
ninja
```

**Requirements:**
- C++20 compiler (GCC 13+, Clang 14+)
- Linux (tested on 5.10+, 6.x)
- CMake 3.20+

**Optional:**
- AF_XDP kernel bypass (requires CAP_BPF)
- FPGA acceleration (Verilator simulation included)

## Benchmarking

### Run Synthetic Benchmarks
```bash
./1_orderbook/bench_spsc --benchmark_format=json --benchmark_out=spsc.json
./1_orderbook/bench_orderbook --benchmark_format=json --benchmark_out=orderbook.json
./2_network/bench_fix_pipeline --benchmark_format=json --benchmark_out=fix.json
```

### Run Realistic Workloads
```bash
./1_orderbook/bench_realistic --benchmark_format=json --benchmark_out=realistic.json
```

### CPU Pinning (Recommended)
```bash
taskset -c 2 ./1_orderbook/bench_orderbook
```

## Testing

```bash
ctest --verbose
# All 540 assertions in 25 test cases pass, covering:
# - Order insertion/cancellation
# - Matching logic (single + multi-level)
# - Price level management
# - SPSC queue correctness
```

## Integration

### Input: FIX Messages
```
"35=D\0011=ORDER123\00155=AAPL\00154=1\00138=100\00144=150.5000\00140=2\001"
                                     (parsed to 1505000 ticks)
```

### Output: Match Callbacks
```cpp
MatchResult {
    price_ticks: 150500 (maker's price in tick units),
    quantity: 100,
    timestamp_tsc: 12345678901234
}
```

## Design Philosophy

See [`docs/design_decisions.md`](docs/design_decisions.md) for rationale on:
- Why acquire/release vs seq_cst
- Why flat array vs std::map (60x faster for <500 levels)
- Why cache-line alignment (100+ ns false sharing cost)
- Memory ordering proofs and linearizability arguments

## Performance Baseline

Establish baseline on your hardware:
```bash
cd build
cmake .. -GNinja
ninja
./1_orderbook/bench_realistic 2>&1 | grep -E "p50|p99|p99.9"
```

**Expected on modern x86 (Ryzen 7/i7 2024+):**
- Simple insert: 25-50 ns P50
- With matching: 20-40 ns P50
- Churn: 50-100 ns P99
- Worst case: 2-5 µs P99

## Known Limitations

1. **WSL2 unsuitable for benchmarking** - Use native Linux for validation
2. **AF_XDP requires CAP_BPF** - Kernel bypass not in containers
3. **MPSC contention** - Partition order books for multi-threaded ingress
4. **Single-socket only** - No NUMA support
5. **Synthetic > real messages** - Ideal conditions in benchmarks

## What This Is NOT

❌ Complete trading system (no risk management, position tracking)  
❌ Production-ready without network integration (AF_XDP/DPDK)  
❌ Multi-threaded by default (designed for SPSC, add sharding for parallel)  
❌ Competitor to proprietary FPGA-accelerated systems  

## What This IS

✅ Correct, auditable lock-free matching implementation  
✅ Reference for ultra-low latency data structure design  
✅ Foundation for kernel-bypass trading systems  
✅ Educational resource for lock-free practitioners  
✅ Suitable for embedded matching in larger systems  

## Documentation

- [`docs/benchmark_results.md`](docs/benchmark_results.md) - Actual measurements
- [`docs/realistic_vs_synthetic.md`](docs/realistic_vs_synthetic.md) - Benchmark interpretation
- [`docs/literature_comparison.md`](docs/literature_comparison.md) - Verified baselines
- [`docs/design_decisions.md`](docs/design_decisions.md) - Architecture rationale
- [`docs/latency_model.md`](docs/latency_model.md) - Roofline analysis
- [`BENCHMARK_VERIFICATION.md`](BENCHMARK_VERIFICATION.md) - Claims verification

## References

- Vyukov, D. (2012). "MPSC queue." 1024cores.net
- Herlihy, M. & Shavit, N. (2008). "The Art of Multiprocessor Programming"
- Lamport, L. (1974). "A New Solution of Dijkstra's Concurrent Programming Problem"

---

**Implementation Status:** Research/portfolio. Suitable for educational purposes and as foundation for production systems. All claims measured and verified.
