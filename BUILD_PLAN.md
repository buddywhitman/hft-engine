# HFT Engine: Production Build Plan

**Objective:** Production-grade high-frequency trading engine. Every nanosecond matters. No shortcuts.

---

## Phase 1: SPSC Queue (Vyukov's Wait-Free Algorithm)
- **Commits:** 3-4
  - Scaffold + basic tests
  - Cache-line alignment + false-sharing fix
  - Memory ordering (acquire/release) validation
  - Hazard pointers for safe memory reclamation
- **Deliverables:**
  - Wait-free SPSC queue
  - Benchmark vs naive implementation
  - perf stat profiling

## Phase 2: Order Book (Lock-Free Matching)
- **Commits:** 3-4
  - FlatPriceLadder data structure (O(log n) search)
  - Pre-allocated object pool (no heap in hot path)
  - Add/cancel/match operations
  - Intrusive data structures for cache locality
- **Deliverables:**
  - Fast order book
  - Realistic workload benchmarks
  - Cache-line profiling

## Phase 3: FIX Parser (Zero-Copy)
- **Commits:** 2-3
  - string_view parsing (no allocation)
  - FIX 4.2/4.4 protocol compliance
  - Checksum validation (CRC32)
  - Fragmentation handling (state machine)
- **Deliverables:**
  - Production FIX parser
  - Throughput benchmarks
  - Error handling tests

## Phase 4: AF_XDP Kernel-Bypass (CRITICAL)
- **Commits:** 4-5
  - XSK socket setup + UMEM management
  - BPF program for packet filtering/steering
  - Zero-copy packet reception
  - Benchmark vs socket-based baseline
- **Deliverables:**
  - Sub-microsecond latency advantage
  - Real network integration
  - Profiling data

## Phase 5: FPGA Accelerator (Artix-7)
- **Commits:** 3-4
  - FIX parser OR order book state machine in Verilog/SystemVerilog
  - Synthesis to bitstream
  - Cycle-accurate latency measurement
  - Software comparison
- **Deliverables:**
  - Working FPGA bitstream
  - Hardware latency data
  - Performance analysis

## Phase 6: Integration & Testing
- **Commits:** 2
  - End-to-end test harness
  - Production scenarios
  - Chaos testing (packet loss, jitter)
- **Deliverables:**
  - Integrated system
  - Real-world behavior validation

## Phase 7: Profiling & Optimization
- **Commits:** 2-3
  - perf stat integration (cache misses, branch misses, IPC)
  - Memory bandwidth analysis
  - CPU frequency scaling
  - Real profiling data
- **Deliverables:**
  - Comprehensive profiling
  - Performance justification
  - Optimization opportunities

## Phase 8: Documentation & Deployment
- **Commits:** 2
  - Design rationale
  - Deployment guide
  - Literature comparison (verified only)
  - Performance analysis

---

## Build Environment

**Test Workflow:**
1. Run `./scripts/full_test.sh` on Ubuntu test machine
2. Results saved to `results/[timestamp]/`
3. Git push to remote
4. Pull results here, analyze, update code/docs
5. Iterate

**Result Structure:**
```
results/[timestamp]/
├── benchmarks.json          # Latency, throughput
├── perf_stat_[test].txt     # Cache/branch analysis
├── build_log.txt
├── fpga_synthesis.log
└── system_info.txt          # CPU, kernel, hugepages
```

---

## Performance Targets

| Component | Target | Stretch |
|-----------|--------|---------|
| SPSC Queue P99 | <50ns | <20ns |
| Order Book Match P99 | <100ns | <50ns |
| FIX Parser | <200ns | <100ns |
| AF_XDP E2E | <500ns | <250ns |
| FPGA FIX Parse | <1µs hardware | <500ns |

---

## Production Requirements

- ✅ No heap allocation in hot path
- ✅ No dynamic memory in matching engine
- ✅ Cache-line alignment (64-byte)
- ✅ Intrusive data structures (embedded lists)
- ✅ Hazard pointers or equivalent for lock-free safety
- ✅ Real-time characteristics (predictable latency)
- ✅ Panic-on-error (not graceful degradation in critical path)
- ✅ NUMA-aware (if multi-socket)
- ✅ CPU affinity enforcement
- ✅ Hugepage support
- ✅ Comprehensive error handling
- ✅ Production logging (zero allocation)
- ✅ Real profiling data

---

## Build Timeline

- Phases 1-3 (SPSC, OrderBook, FIX): 1 week
- Phase 4 (AF_XDP): 1-2 weeks (complexity: network, BPF)
- Phase 5 (FPGA): 1-2 weeks (depends on Vivado experience)
- Phases 6-8 (Integration, profiling, docs): 1 week

**Total: 4-6 weeks** to production-ready with full profiling

---

## Success Criteria

- [ ] 50+ commits showing real iteration
- [ ] Comprehensive benchmarks with perf stat data
- [ ] AF_XDP working with latency comparison
- [ ] FPGA bitstream working
- [ ] Zero AI references in code/docs
- [ ] Deployment-ready documentation
- [ ] Ready for Citadel/HRT technical interview

