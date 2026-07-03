# Design Decisions

## Cache-Line Alignment in SPSC Queue

**Decision**: Align head_ and tail_ to separate 64-byte cache lines.

**Why**: On modern CPUs, a single cache line is 64 bytes. When head_ and tail_ share a line, every producer store to head_ causes a coherency miss on the consumer's copy of that line (which contains tail_). This round-trip costs ~60-100ns on NUMA systems.

With separate cache lines:
- Producer writes to head_ on its cache line without invalidating consumer's cache line
- Consumer reads tail_ from its own cache line without stalling for producer

**Cost**: 128 bytes instead of 8 bytes for two atomics. Negligible in context of queue DRAM footprint.

**Measurement**: Add this to bench_spsc:
```cpp
// Measure with shared cache line vs separate
// Expected speedup: 3-5x
```

## Memory Ordering: acquire/release vs seq_cst

**Decision**: Use `std::memory_order_acquire` on loads, `std::memory_order_release` on stores.

**Why**:
- `seq_cst` generates a full memory fence (MFENCE on x86, DMB on ARM). Cost: ~10x more expensive than acquire/release.
- For SPSC, there is exactly one writer and one reader. No need for full sequential consistency.
- The acquire/release pair is sufficient because it provides a happens-before relationship between producer and consumer without over-synchronizing.
- On x86 (TSO): loads are already acquire, stores are already release at the hardware level. The annotations are for the compiler and ARM correctness.

**Benchmark**: Show seq_cst vs acq/rel in results.

## Flat Array vs std::map for Price Ladder

**Decision**: Use `std::vector<PriceLevel>` with binary search, not std::map.

**Why asymptotic complexity is misleading**:
- std::map: O(log n) lookup via pointer chasing
- Flat array: O(log n) lookup via binary search, O(n) insert

But in practice with real order books:
- n = 50-200 active price levels (not 10000+)
- Flat array data fits in L1 cache (~32KB for ~500 price levels)
- Binary search on L1 data: 8 comparisons = ~8ns
- std::map traversal: 8 pointer chases across DRAM = ~480ns (60ns per miss × 8 misses)

**Crossover point**: At n ≈ 500, flat array becomes worse due to insert cost. For real order books (n < 200), flat always wins.

**Benchmark**: Measure both implementations at various n:
- n=10, n=50, n=100, n=200, n=500, n=1000
- Report the crossover point

**Also benchmark**: std::unordered_map (hash-based). Expected: slower due to hash collision chains.

## No Exceptions in Hot Path

**Decision**: `-fno-exceptions` compiler flag. Return codes instead.

**Why**:
- Exception handling code generation adds overhead (landing pads, EH tables)
- Even if an exception is never thrown, the presence of try/catch blocks generates additional machine code
- In latency-sensitive paths, predictability matters more than convenience
- Add_order returns 0 on failure (pool exhausted) instead of throwing

## Price Representation: Integer Ticks

**Decision**: Prices as `int64_t` price_ticks = price_dollars × 10000.

**Why not floating point**:
- Floating-point comparison is non-deterministic (depends on rounding)
- Floating-point arithmetic is slow (~10 cycles on modern CPUs)
- Integer comparison is 1 cycle
- At $0.0001 tick size, int64_t supports up to $922 trillion

**Example**:
- Price $150.25 = 1502500 ticks
- Comparison: `if (new_price_ticks <= ask_price_ticks)` is exact and fast

## Order Pool vs Heap Allocation

**Decision**: Pre-allocate `Order[MAX_ORDERS]` array. Free list is SPSC queue of indices.

**Why**:
- Heap allocation (malloc/new) costs ~100ns and non-deterministic
- Pre-allocation trades memory (~64MB for 1M orders) for latency guarantee
- Free list as SPSC queue: order slots available immediately, no lock-ups
- Downside: Fixed max capacity. Mitigated by tuning MAX_ORDERS = 2^20

## AF_XDP Copy Mode on Commodity Hardware

**Decision**: Support both zero-copy and copy-mode. Default to copy-mode for compatibility.

**Why**:
- Zero-copy requires specific NIC drivers (ixgbe, i40e, igb)
- Most VMs/cloud instances use virtio_net, which doesn't support zero-copy
- Copy-mode still beats regular socket by 2-5x due to:
  - Reduced syscall overhead
  - Packet batching
  - UMEM pre-allocation (no malloc in parsing loop)
- For production on real hardware, zero-copy can be enabled with `force_zero_copy=true`

**Benchmark**: Show copy-mode vs regular socket. Expected: 2-5x improvement.

## FPGA Clock Frequency vs Software Latency

**Decision**: Target 100MHz. Accept ~1.2µs parsing latency vs ~50ns software.

**Why this is still valuable**:
- **Offloads CPU**: While FPGA parses messages, CPU runs matching logic
- **Parallel throughput**: FPGA can handle multiple packet streams simultaneously
- **Determinism**: FPGA pipeline latency is constant, not jittery like software
- **Energy efficiency**: Specialized hardware uses less power per message

**Trade-off explicitly documented**: Software parsing at 50ns is faster for single messages. FPGA wins when you have ≥10 concurrent packet streams competing for CPU cores.

## Verilator Simulation Over Hardware

**Decision**: Ship Verilator testbench, not Vivado bitstream.

**Why**:
- Vivado setup requires license (free for small designs, but non-trivial)
- Verilator provides cycle-accurate simulation, sufficient for verifying correctness
- Allows CI/CD testing without hardware
- Actual synthesis results (timing, utilization) provided in docs/ as reference

**How to move to hardware**: Follow scripts/synth.tcl with Vivado on Artix-7 board.

## Documentation Strategy

**Decision**: All "why" decisions recorded in design_decisions.md. All "how much" numbers in benchmarks.md.

**Why**:
- Separates reasoning (timeless) from measurements (hardware-dependent)
- Makes it easy to verify claims by rerunning benchmarks
- Enables comparing performance across different hardware without ambiguity

---

## Performance Targets Justified

| Metric | Target | How Achieved |
|---|---|---|
| add_order P99 < 200ns | Price ladder is flat array O(log n), n < 200 |
| SPSC P99 < 100ns | Separate cache lines + acq/rel ordering |
| FIX parse > 1M msg/s | Single-pass parser, zero allocation |
| AF_XDP > 2x socket | Reduced syscalls + packet batching |

If benchmarks don't hit targets, investigate:
1. Are we pinned to a core? (use taskset -c N)
2. Is CPU frequency scaling disabled? (cpupower frequency-set -g performance)
3. Are we measuring cold cache? (pre-touch memory first)
4. Is the compiler optimizing aggressively? (check -O3 -march=native)
