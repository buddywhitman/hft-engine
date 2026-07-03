---

# HFT Systems Proof-of-Work: Complete Build Specification

This is a **three-component project** that builds in sequence, each depending on the last. The full system is called `hft-engine` — one repo, three subdirectories, one README that ties them together as a unified narrative. The goal is not to simulate an HFT system. The goal is to build the specific subset of things HFT systems actually do, using your exact hardware, at a level of correctness and documentation that a senior engineer at Graviton or Citadel would read and respect.

---

## Repository Structure

```
hft-engine/
├── CMakeLists.txt                  # top-level cmake, builds all three components
├── README.md                       # unified narrative + benchmark table
├── docs/
│   ├── design_decisions.md         # why each design choice was made
│   ├── benchmarks.md               # all measured numbers with methodology
│   └── latency_model.md            # roofline-style analysis of the pipeline
│
├── 1_orderbook/                    # Component 1: lock-free order book in C++20
│   ├── include/
│   │   ├── spsc_queue.hpp
│   │   ├── mpsc_queue.hpp
│   │   ├── order.hpp
│   │   ├── price_level.hpp
│   │   ├── order_book.hpp
│   │   └── tsc_clock.hpp
│   ├── src/
│   │   └── order_book.cpp
│   ├── bench/
│   │   ├── bench_spsc.cpp
│   │   ├── bench_mpsc.cpp
│   │   └── bench_orderbook.cpp
│   ├── tests/
│   │   ├── test_spsc.cpp
│   │   ├── test_mpsc.cpp
│   │   └── test_orderbook.cpp
│   └── CMakeLists.txt
│
├── 2_network/                      # Component 2: AF_XDP FIX parser
│   ├── include/
│   │   ├── xsk_socket.hpp
│   │   ├── umem.hpp
│   │   ├── fix_parser.hpp
│   │   └── fix_message.hpp
│   ├── src/
│   │   ├── xsk_socket.cpp
│   │   ├── umem.cpp
│   │   └── fix_parser.cpp
│   ├── ebpf/
│   │   └── redirect.bpf.c          # BPF program loaded at runtime
│   ├── bench/
│   │   └── bench_fix_pipeline.cpp
│   ├── tests/
│   │   └── test_fix_parser.cpp
│   └── CMakeLists.txt
│
├── 3_fpga/                         # Component 3: FPGA accelerator on Artix-7
│   ├── rtl/
│   │   ├── fix_parser.sv
│   │   ├── order_entry.sv
│   │   ├── price_level_store.sv
│   │   ├── top.sv
│   │   └── tb/
│   │       ├── tb_fix_parser.sv
│   │       └── tb_order_entry.sv
│   ├── constraints/
│   │   └── artix7.xdc
│   ├── scripts/
│   │   ├── synth.tcl
│   │   └── impl.tcl
│   ├── sim/
│   │   └── run_sim.sh              # Verilator-based simulation
│   └── docs/
│       ├── timing_report.txt       # actual post-impl timing from Vivado
│       └── utilization_report.txt  # LUT/FF/BRAM numbers
│
└── scripts/
    ├── setup_hugepages.sh          # hugepage + CPU isolation setup
    ├── pin_cpu.sh                  # taskset + chrt for real-time scheduling
    └── run_benchmarks.sh           # full benchmark suite runner
```

---

## Component 1: Lock-Free Order Book (C++20)

### Exact Requirements

**Target OS:** Linux (Ubuntu 22.04+, your Ubuntu cluster). Requires kernel ≥ 5.4.

**Compiler:** GCC 12+ or Clang 15+ with `-std=c++20 -O3 -march=native -funroll-loops`

**No external dependencies** for the core orderbook. Google Benchmark for bench/. Catch2 for tests/. Both via CMake FetchContent.

---

### `tsc_clock.hpp` — nanosecond timestamping

```cpp
// The ONLY correct way to timestamp in a latency-sensitive context.
// DO NOT use std::chrono::high_resolution_clock — it calls clock_gettime()
// which is a syscall with ~20-50ns overhead. rdtsc is ~5 clock cycles.
// 
// Calibration: measure TSC ticks per nanosecond at startup using
// clock_gettime(CLOCK_MONOTONIC) as reference over 10ms window.

#pragma once
#include <cstdint>
#include <x86intrin.h>  // __rdtsc(), __rdtscp()

namespace hft {

class TscClock {
public:
    // Call once at startup to calibrate ticks-per-ns
    static void calibrate();
    
    // Returns current TSC value. Use for delta measurements only.
    // Never convert to wall time in the hot path.
    [[nodiscard]] static inline uint64_t now() noexcept {
        return __rdtsc();
    }
    
    // Use __rdtscp() when you need a serializing fence.
    // Prevents out-of-order execution from corrupting measurements.
    // Costs ~3x more than rdtsc — only use at measurement boundaries.
    [[nodiscard]] static inline uint64_t now_serialized() noexcept {
        uint32_t aux;
        return __rdtscp(&aux);
    }
    
    // Convert TSC delta to nanoseconds using calibrated ratio
    [[nodiscard]] static uint64_t tsc_to_ns(uint64_t tsc_delta) noexcept;
    
private:
    static inline double ticks_per_ns_ = 1.0;
};

} // namespace hft
```

---

### `spsc_queue.hpp` — wait-free single producer, single consumer

```cpp
// DESIGN DECISIONS (document all of these in design_decisions.md):
//
// 1. alignas(64) on head_ and tail_ separately.
//    Reason: false sharing. If head_ and tail_ share a cache line,
//    every producer write (to tail_) invalidates the consumer's cache line
//    containing head_, causing a cache coherency round-trip (~60-100ns on
//    modern NUMA systems). Separate cache lines eliminate this entirely.
//    Benchmark: show 3x throughput improvement from this single change.
//
// 2. capacity_ must be a power of 2.
//    Reason: index wrapping via (idx & mask_) instead of (idx % capacity_).
//    Modulo requires a division instruction (~20-40 cycles). Bitwise AND
//    is 1 cycle. At 100M ops/s this matters.
//
// 3. std::memory_order_acquire on load, std::memory_order_release on store.
//    NOT memory_order_seq_cst (the default). seq_cst generates a full memory
//    fence (MFENCE on x86) which is ~10x more expensive than acquire/release.
//    The acquire/release pair is sufficient for SPSC because there is
//    exactly one writer and one reader — no additional ordering guarantees needed.
//    On x86, loads are already acquire and stores are already release due to
//    TSO (Total Store Order) memory model. The acquire/release annotations
//    are for the compiler, not the hardware, on x86. On ARM (your Cortex-M7
//    and Raspberry Pi), they DO generate hardware barriers — important for
//    your cross-platform benchmarking story.
//
// 4. Cache line padding between data_ and the indices.
//    Reason: prevents the data array from sharing a cache line with the
//    atomic indices, which would cause false sharing when the consumer
//    reads data[tail_] and also reads tail_.

#pragma once
#include <atomic>
#include <array>
#include <optional>
#include <cstddef>
#include <cassert>

namespace hft {

template<typename T, std::size_t Capacity>
    requires (Capacity > 0 && (Capacity & (Capacity - 1)) == 0)  // power of 2
class SpscQueue {
public:
    SpscQueue() : head_(0), tail_(0) {}
    
    // Returns false if queue is full. Never blocks.
    [[nodiscard]] bool push(const T& item) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire))
            return false;  // full
        data_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }
    
    // Returns std::nullopt if queue is empty. Never blocks.
    [[nodiscard]] std::optional<T> pop() noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return std::nullopt;  // empty
        T item = data_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return item;
    }
    
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t mask_ = Capacity - 1;
    
    alignas(64) std::atomic<std::size_t> head_;
    alignas(64) std::atomic<std::size_t> tail_;
    // Padding between indices and data prevents false sharing
    // when producer writes to head_ while consumer reads data_[tail_]
    alignas(64) std::array<T, Capacity> data_;
};

} // namespace hft
```

---

### `order.hpp` — cache-line-aligned order struct

```cpp
// CRITICAL: The Order struct must fit in exactly one cache line (64 bytes).
// Reason: when the matching engine reads an order, it must load exactly
// one cache line from DRAM. If the struct spans two cache lines, every
// read costs 2 cache misses instead of 1 — doubles memory latency on
// the critical path.
//
// Measure: use static_assert(sizeof(Order) <= 64).
// Verify: use pahole or offsetof to confirm no unexpected padding.

#pragma once
#include <cstdint>
#include <cstring>

namespace hft {

enum class Side : uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : uint8_t { Limit = 0, Market = 1, Cancel = 2 };

// Prices represented as integer ticks (price * 10000) to avoid
// floating point entirely. Floating point comparison is non-deterministic
// and slow. Integer comparison is 1 cycle.
// At $0.0001 tick size, int64_t supports prices up to $922 trillion.

struct alignas(64) Order {
    uint64_t order_id;      // 8 bytes
    uint64_t timestamp_tsc; // 8 bytes — rdtsc at ingestion
    int64_t  price_ticks;   // 8 bytes — price * 10000, negative for sell side
    uint32_t quantity;      // 4 bytes
    uint32_t filled_qty;    // 4 bytes
    uint32_t symbol_id;     // 4 bytes — intern symbol strings to uint32_t
    Side     side;          // 1 byte
    OrderType type;         // 1 byte
    uint8_t  padding[26];   // explicit padding to reach 64 bytes
};

static_assert(sizeof(Order) == 64, "Order must be exactly one cache line");
static_assert(alignof(Order) == 64, "Order must be cache-line aligned");

} // namespace hft
```

---

### `price_level.hpp` — the core data structure debate

```cpp
// THE KEY DESIGN DECISION: std::map<int64_t, PriceLevel> vs flat array
//
// std::map: O(log n) insert/lookup. Each node is a heap allocation.
// Cache behavior: terrible. Each tree traversal walks pointer-chased nodes
// across DRAM. For 1000 price levels, expect 10 cache misses per lookup.
//
// Flat sorted array: O(log n) lookup via binary search, O(n) insert.
// Cache behavior: excellent. Binary search on a 64-element array fits
// entirely in L1 cache (~4KB). For typical order books with <200 active
// price levels on each side, this WINS despite worse asymptotic complexity.
// 
// Why? n is small and constant in practice. Real order books have
// ~50-200 active price levels. At n=200:
//   - std::map: ~8 pointer chases * ~60ns each = ~480ns
//   - flat binary search: ~8 comparisons on L1-cached data = ~8ns
//
// BENCHMARK BOTH and document the crossover point.
// ALSO BENCHMARK: std::unordered_map for comparison.
//
// The agent must implement all three and produce a comparison chart.

#pragma once
#include "order.hpp"
#include <vector>
#include <cstdint>

namespace hft {

struct PriceLevel {
    int64_t price_ticks;
    uint64_t total_quantity;
    uint32_t order_count;
    uint32_t padding;
};

// Flat sorted array implementation
// Sorted ascending for buys (we want best bid = max = back())
// Sorted ascending for sells (best ask = min = front())
class FlatPriceLadder {
public:
    explicit FlatPriceLadder(std::size_t reserve = 256) {
        levels_.reserve(reserve);
    }
    
    // Returns pointer to level, nullptr if not found
    PriceLevel* find(int64_t price_ticks) noexcept;
    
    // Insert or update. Returns reference to level.
    PriceLevel& upsert(int64_t price_ticks, uint32_t qty_delta) noexcept;
    
    // Remove level if quantity reaches zero
    void remove_if_empty(int64_t price_ticks) noexcept;
    
    // Best bid (highest price for buy side)
    [[nodiscard]] const PriceLevel* best() const noexcept {
        return levels_.empty() ? nullptr : &levels_.back();
    }
    
    [[nodiscard]] std::size_t size() const noexcept { return levels_.size(); }

private:
    std::vector<PriceLevel> levels_;  // sorted ascending by price_ticks
};

} // namespace hft
```

---

### `order_book.hpp` — the matching engine

```cpp
// The matching engine. Two price ladders: bids (buy) and asks (sell).
// 
// Matching logic:
//   New buy order at price P: match against asks where ask.price <= P
//   New sell order at price P: match against bids where bid.price >= P
//   Execute at the resting order's price (maker price)
//
// Memory pool: allocate Order objects from a pre-allocated pool, not
// the heap. Heap allocation (new/malloc) in the hot path costs ~100ns
// and introduces non-determinism from the allocator.
// 
// Pool design: fixed-size array of Order[MAX_ORDERS], a free-list
// implemented as an SPSC queue of uint32_t indices.

#pragma once
#include "order.hpp"
#include "price_level.hpp"
#include "spsc_queue.hpp"
#include "tsc_clock.hpp"
#include <array>
#include <functional>

namespace hft {

constexpr std::size_t MAX_ORDERS = 1 << 20;  // 1M orders, ~64MB

struct MatchResult {
    uint64_t aggressor_id;
    uint64_t resting_id;
    int64_t  price_ticks;
    uint32_t quantity;
    uint64_t match_timestamp_tsc;
};

class OrderBook {
public:
    using MatchCallback = std::function<void(const MatchResult&)>;
    
    explicit OrderBook(uint32_t symbol_id, MatchCallback on_match);
    
    // Add a new order. Returns order_id. Triggers on_match for each fill.
    // Latency target: P99 < 200ns from call to return on a pinned core.
    uint64_t add_order(Side side, int64_t price_ticks, 
                       uint32_t quantity, OrderType type) noexcept;
    
    // Cancel an existing order by order_id.
    bool cancel_order(uint64_t order_id) noexcept;
    
    [[nodiscard]] const PriceLevel* best_bid() const noexcept {
        return bids_.best();
    }
    [[nodiscard]] const PriceLevel* best_ask() const noexcept {
        return asks_.best();
    }
    
    // Spread in ticks. Returns INT64_MAX if either side is empty.
    [[nodiscard]] int64_t spread_ticks() const noexcept;

private:
    uint32_t symbol_id_;
    MatchCallback on_match_;
    FlatPriceLadder bids_;  // buy side, sorted ascending (best = back)
    FlatPriceLadder asks_;  // sell side, sorted ascending (best = front)
    
    // Memory pool: pre-allocated, no heap allocation in hot path
    alignas(64) std::array<Order, MAX_ORDERS> order_pool_;
    SpscQueue<uint32_t, MAX_ORDERS> free_list_;  // indices of free slots
    uint64_t next_order_id_ = 1;
};

} // namespace hft
```

---

### Benchmark Requirements for Component 1

The benchmark suite (`bench/`) must measure and report ALL of the following. These are the exact numbers that go in the README table.

```
Benchmark: SpscQueue throughput (single thread ping-pong)
  - 1B messages
  - Measure: ops/second, P50/P95/P99/P99.9 latency in nanoseconds
  - Variants: with/without alignas(64) on head_/tail_ (show false sharing cost)
  - Variants: memory_order_seq_cst vs acquire/release (show fence cost)

Benchmark: OrderBook add_order latency
  - Warm L1/L2 cache before measurement (pre-touch all memory)
  - 10M iterations of add_order(Buy, random_price, random_qty, Limit)
  - Measure: P50/P95/P99/P99.9/P99.99 in nanoseconds using rdtsc delta
  - Variants: FlatPriceLadder vs std::map vs std::unordered_map
  - Report: L1/L2/L3 cache miss rates via perf stat

Benchmark: OrderBook with matching (add+match path)
  - Pre-populate book with 100 resting orders on each side
  - Alternate aggressive buy/sell orders to trigger matches
  - Measure: time from add_order() call to MatchCallback invocation

CPU setup required for all benchmarks:
  - taskset -c 2 (pin to core 2, avoid core 0 which handles interrupts)
  - chrt -f 99 (real-time FIFO scheduling, prevents preemption)
  - sudo sh -c 'echo 1 > /sys/bus/cpu/devices/cpu2/power/pm_qos_resume_latency_us'
  - Disable CPU frequency scaling: cpupower frequency-set -g performance
  - The setup_hugepages.sh script must do all of this
```

The `docs/benchmarks.md` must contain a table like this, populated with real measured numbers:

```markdown
| Operation | P50 | P95 | P99 | P99.9 | Notes |
|---|---|---|---|---|---|
| SPSC push (baseline) | Xns | Xns | Xns | Xns | seq_cst, no alignment |
| SPSC push (optimized) | Xns | Xns | Xns | Xns | acq/rel, cache-aligned |
| add_order (std::map) | Xns | Xns | Xns | Xns | 100 price levels |
| add_order (flat array) | Xns | Xns | Xns | Xns | 100 price levels |
| add_order + match | Xns | Xns | Xns | Xns | 200 resting orders |
```

---

## Component 2: AF_XDP FIX Parser

### Exact Requirements

**Hardware check first.** Before starting, run:
```bash
ethtool -i <your_nic> | grep driver
```
Check if your driver supports AF_XDP native mode. If it's `virtio_net` (common on WSL/VMs), you get copy mode only, which still works but won't achieve zero-copy numbers. For your Ubuntu cluster, check what NIC is physically present. If it's an Intel e1000e, igb, ixgbe, or i40e — you have native AF_XDP support. If not, copy mode still demonstrates the concept correctly and benchmarks should note this.

**Dependencies:**
```bash
sudo apt install libbpf-dev clang llvm linux-headers-$(uname -r) libxdp-dev
```

---

### `fix_message.hpp` — FIX protocol data structures

```cpp
// FIX (Financial Information eXchange) protocol overview for the agent:
//
// FIX is the universal messaging standard for financial markets.
// A FIX message is a string of key=value pairs delimited by SOH (ASCII 0x01):
//   8=FIX.4.2\x019=65\x0135=D\x0149=SENDER\x0156=TARGET\x0134=1\x01
//   11=ORDER123\x0155=AAPL\x0154=1\x0138=100\x0144=150.00\x0140=2\x0110=123\x01
//
// Key fields:
//   Tag 8:  BeginString (FIX version)
//   Tag 35: MsgType (D=NewOrderSingle, F=Cancel, 8=ExecutionReport)
//   Tag 49: SenderCompID
//   Tag 56: TargetCompID
//   Tag 11: ClOrdID (client order ID)
//   Tag 55: Symbol
//   Tag 54: Side (1=Buy, 2=Sell)
//   Tag 38: OrderQty
//   Tag 44: Price
//   Tag 40: OrdType (1=Market, 2=Limit)
//   Tag 10: Checksum (always last, sum of all bytes mod 256)
//
// Parsing strategy: zero-allocation, in-place, no string copies.
// Parse directly from the UMEM buffer into a FixMessage struct.
// Never call malloc/new during parsing.

#pragma once
#include <cstdint>
#include <string_view>
#include <optional>

namespace hft::fix {

constexpr uint8_t SOH = 0x01;  // field delimiter

enum class MsgType : uint8_t {
    NewOrderSingle    = 'D',
    OrderCancelReq    = 'F',
    ExecutionReport   = '8',
    Unknown           = 0
};

// All string fields are string_views into the original UMEM buffer.
// Zero copy — no string allocation.
// IMPORTANT: these views are only valid while the UMEM frame is held.
// The caller must process or copy before releasing the frame.

struct FixMessage {
    MsgType  msg_type      = MsgType::Unknown;
    std::string_view cl_ord_id;   // tag 11
    std::string_view symbol;      // tag 55
    uint8_t  side          = 0;   // tag 54: 1=buy 2=sell
    uint32_t order_qty     = 0;   // tag 38
    int64_t  price_ticks   = 0;   // tag 44, multiplied by 10000
    uint8_t  ord_type      = 0;   // tag 40: 1=market 2=limit
    uint8_t  checksum      = 0;   // tag 10
    bool     valid         = false;
};

// Parse a FIX message from a raw byte buffer.
// Returns FixMessage with valid=false on parse error.
// Complexity: O(n) single pass, no backtracking.
// No heap allocation. No exceptions.
[[nodiscard]] FixMessage parse(const uint8_t* buf, std::size_t len) noexcept;

// Verify checksum. Call after parse() if strict validation needed.
[[nodiscard]] bool verify_checksum(const uint8_t* buf, std::size_t len) noexcept;

} // namespace hft::fix
```

---

### `ebpf/redirect.bpf.c` — the BPF program

```c
// This BPF program runs in the kernel at the XDP hook point.
// It intercepts packets before the Linux network stack sees them
// and redirects them to our AF_XDP socket.
//
// The program must ONLY redirect packets destined for our FIX port.
// All other packets must be passed to the normal stack (XDP_PASS)
// or SSH will break — this is the #1 mistake people make with AF_XDP.
//
// Compile with: clang -O2 -target bpf -c redirect.bpf.c -o redirect.bpf.o

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define FIX_PORT 9878  // standard FIX session port, configurable

struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 1);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
} xsk_map SEC(".maps");

SEC("xdp")
int xdp_redirect_fix(struct xdp_md *ctx) {
    void *data_end = (void *)(long)ctx->data_end;
    void *data     = (void *)(long)ctx->data;
    
    // Parse ethernet header
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;
    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;
    
    // Parse IP header
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;
    if (ip->protocol != IPPROTO_TCP)
        return XDP_PASS;
    
    // Parse TCP header
    struct tcphdr *tcp = (void *)ip + (ip->ihl * 4);
    if ((void *)(tcp + 1) > data_end)
        return XDP_PASS;
    
    // Only redirect packets to/from our FIX port
    if (bpf_ntohs(tcp->dest) != FIX_PORT && 
        bpf_ntohs(tcp->source) != FIX_PORT)
        return XDP_PASS;  // NOT our packet — let kernel handle it
    
    // Redirect to AF_XDP socket queue 0
    return bpf_redirect_map(&xsk_map, ctx->rx_queue_index, XDP_PASS);
}

char _license[] SEC("license") = "GPL";
```

---

### `xsk_socket.hpp` — AF_XDP socket wrapper

```cpp
// The UMEM model (agent must understand this before implementing):
//
// UMEM is a contiguous memory region split into fixed-size frames (2048 bytes).
// Four rings coordinate frame ownership between kernel and userspace:
//
//   FILL ring:        userspace → kernel: "here are free frames, put packets here"
//   RX ring:          kernel → userspace: "packet received at this frame offset"
//   TX ring:          userspace → kernel: "send the packet at this frame offset"
//   COMPLETION ring:  kernel → userspace: "done sending, frame is yours again"
//
// Zero-copy mode: NIC writes directly into UMEM via DMA.
// No memcpy ever occurs between kernel and userspace.
//
// Copy mode (fallback): kernel copies packet into UMEM.
// Still faster than regular sockets due to reduced syscall overhead.

#pragma once
#include "umem.hpp"
#include "fix_message.hpp"
#include <cstdint>
#include <functional>
#include <string>

namespace hft::net {

struct XskConfig {
    std::string interface;      // network interface name, e.g. "eth0"
    uint16_t    fix_port;       // FIX session port to intercept
    uint32_t    queue_id = 0;   // NIC queue to bind to
    uint32_t    num_frames = 4096;
    uint32_t    frame_size = 2048;
    bool        force_zero_copy = false;  // fail if zero-copy unavailable
};

class XskSocket {
public:
    using PacketCallback = std::function<void(const fix::FixMessage&, uint64_t recv_tsc)>;
    
    explicit XskSocket(const XskConfig& config, PacketCallback on_fix_message);
    ~XskSocket();
    
    // Non-copyable, non-movable (owns kernel resources)
    XskSocket(const XskSocket&) = delete;
    XskSocket& operator=(const XskSocket&) = delete;
    
    // Main receive loop. Busy-polls (no epoll/select).
    // Busy polling: never yields to OS. Uses 100% of one CPU core.
    // This is intentional for latency-sensitive paths.
    // The alternative (poll/epoll) adds ~10-50µs of OS scheduling latency.
    void run_rx_loop();
    
    // Load and attach the BPF redirect program
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
    
    // Kernel AF_XDP structures (use libxdp/libbpf)
    struct xsk_socket* xsk_ = nullptr;
    struct xsk_umem*   umem_ = nullptr;
    // ... rings, UMEM area, etc.
};

} // namespace hft::net
```

---

### Benchmark Requirements for Component 2

```
Benchmark: FIX parser throughput
  - Generate 10M synthetic FIX NewOrderSingle messages in memory
  - Parse each in-place (zero allocation)
  - Measure: messages/second, P99 parse latency
  - Variants: with/without checksum verification
  
Benchmark: End-to-end pipeline latency (loopback test)
  - Setup: two processes on same machine
  - Sender: generates FIX messages, sends via regular socket
  - Receiver: AF_XDP socket receives, parses, feeds to order book
  - Timestamp: sender embeds TSC at send, receiver reads TSC at callback
  - Measure: P50/P99 end-to-end latency in microseconds
  - Compare against: same pipeline using regular TCP recv() socket
  - Expected result: AF_XDP should be 2-5x faster than regular socket

README must include:
  - Diagram of the packet path: NIC → BPF → UMEM → parser → order book
  - NIC driver name and whether zero-copy was achieved
  - Kernel version
  - All benchmark numbers with CPU model and clock speed
```

---

## Component 3: FPGA Order Book Accelerator (Artix-7)

### What This Is Not

This is not a complete FPGA trading system. It is an **accelerator for the FIX parsing stage** — a hardware module that takes raw FIX bytes and outputs a structured order entry in a single pipeline pass. The C++ software above handles the full order book; the FPGA handles the parsing hot path.

This is a realistic and well-scoped project. Real HFT FPGAs do exactly this: offload latency-critical parsing to hardware while keeping the matching logic in software for flexibility.

### `rtl/fix_parser.sv` — pipelined FIX parser

```systemverilog
// FIX parser in SystemVerilog. Fully pipelined — one byte per clock cycle input.
// Target: Artix-7 at 100MHz. Each byte processed in 1 cycle.
// A typical FIX NewOrderSingle message is ~120 bytes = 120 cycles = 1.2µs.
// Compare against software parser: ~50-100ns. Hardware is slower here —
// the value is in offloading the CPU and enabling parallel processing.
//
// Document this tradeoff explicitly in docs/design_decisions.md.
// The point of the FPGA is not raw throughput vs a single-core parser —
// it's enabling the CPU to do something else while parsing happens in hardware.
//
// Module interface: AXI4-Stream for input bytes, parallel output when done.

`timescale 1ns / 1ps

module fix_parser #(
    parameter DATA_WIDTH = 8,  // one byte per clock
    parameter MAX_TAG    = 10  // max tag number digits
)(
    input  logic                  clk,
    input  logic                  rst_n,
    
    // AXI4-Stream slave (input bytes)
    input  logic [DATA_WIDTH-1:0] s_axis_tdata,
    input  logic                  s_axis_tvalid,
    input  logic                  s_axis_tlast,   // marks end of FIX message
    output logic                  s_axis_tready,
    
    // Parsed output (valid for one cycle when m_axis_tvalid asserted)
    output logic [7:0]  msg_type,       // ASCII: 'D', 'F', '8', etc.
    output logic [7:0]  side,           // '1' or '2'
    output logic [31:0] order_qty,      // integer
    output logic [63:0] price_ticks,    // price * 10000
    output logic [15:0] symbol_len,     // length of symbol field
    output logic [79:0] symbol_data,    // up to 10 chars of symbol
    output logic        m_axis_tvalid,  // output fields are valid
    output logic        parse_error     // malformed message
);

// State machine states
typedef enum logic [3:0] {
    IDLE         = 4'h0,
    READ_TAG     = 4'h1,
    READ_EQUALS  = 4'h2,
    READ_VALUE   = 4'h3,
    READ_SOH     = 4'h4,
    OUTPUT_VALID = 4'h5,
    ERROR_STATE  = 4'hF
} state_t;

state_t state, next_state;

// Tag accumulator (up to 10 digits = 32-bit tag)
logic [3:0]  tag_digits;
logic [31:0] current_tag;

// Value accumulator  
logic [15:0] val_digits;
logic [63:0] current_val;    // for numeric fields
logic [79:0] current_str;    // for string fields (up to 10 chars)
logic [15:0] str_len;

// Parsed field registers
logic [7:0]  r_msg_type;
logic [7:0]  r_side;
logic [31:0] r_order_qty;
logic [63:0] r_price_ticks;  // accumulate as integer * 10000

// ... (agent implements full state machine)
// Key states:
//   READ_TAG: accumulate digits until '=' seen
//   READ_VALUE: accumulate chars until SOH (0x01) seen
//     - if tag is numeric field: parse ASCII digits to integer
//     - if tag is string field: capture raw bytes
//   On SOH: register the field, reset accumulators, return to READ_TAG
//   On s_axis_tlast: assert m_axis_tvalid for one cycle

endmodule
```

### `rtl/order_entry.sv` — order entry interface

```systemverilog
// Bridges the FIX parser output to the order book interface.
// Also validates the parsed fields (qty > 0, price in range, etc.)
// and generates an error response for invalid orders.

module order_entry (
    input  logic        clk,
    input  logic        rst_n,
    
    // From fix_parser
    input  logic [7:0]  msg_type,
    input  logic [7:0]  side,
    input  logic [31:0] order_qty,
    input  logic [63:0] price_ticks,
    input  logic        fix_valid,
    
    // To order book (parallel bus, one cycle latency)
    output logic        ob_add_valid,
    output logic        ob_side,        // 0=buy, 1=sell
    output logic [31:0] ob_qty,
    output logic [63:0] ob_price,
    output logic        ob_cancel_valid,
    output logic [63:0] ob_cancel_id,
    
    // Error reporting
    output logic        validation_error,
    output logic [7:0]  error_code
);

endmodule
```

### Testbench Requirements

```systemverilog
// tb/tb_fix_parser.sv — the testbench agent must write

// Test vectors (hardcode these exact messages):
//   1. Valid NewOrderSingle: "8=FIX.4.2\x019=65\x0135=D\x0154=1\x0138=100\x0144=15050\x0140=2\x0110=123\x01"
//   2. Valid Cancel:         "8=FIX.4.2\x019=40\x0135=F\x0141=ORDER123\x0110=045\x01"
//   3. Malformed (no SOH):   "8=FIX.4.2\x0135=D\x0154=1"
//   4. Zero quantity:        "8=FIX.4.2\x0135=D\x0154=1\x0138=0\x0144=100\x01"
//
// For each test vector:
//   - Drive bytes one per clock onto s_axis_tdata/tvalid
//   - Assert s_axis_tlast on the final byte
//   - Check m_axis_tvalid goes high exactly one cycle after tlast
//   - Check all output fields against expected values
//   - Check parse_error for malformed inputs

// Performance measurement:
//   - Count cycles from first byte to m_axis_tvalid assertion
//   - Report in tb output: "Parse latency: N cycles at 100MHz = M ns"
//   - For a 120-byte message at 100MHz: expected = 120 cycles = 1200ns
```

### Vivado Build Requirements

```tcl
# scripts/synth.tcl — agent must generate this

# Synthesis settings for Artix-7 (XC7A100T or your specific part)
# Find your exact part number with: vivado -mode tcl -source get_part.tcl
# Common Artix-7 parts: xc7a35tcsg324-1, xc7a100tcsg324-1, xc7a200tfbg484-1

set_property PART xc7a100tcsg324-1 [current_project]  # UPDATE THIS

# Timing constraint: 100MHz = 10ns clock period
create_clock -period 10.000 -name clk [get_ports clk]

# After synthesis and implementation, the agent must extract and document:
# 1. Worst Negative Slack (WNS) — must be >= 0 for timing closure
# 2. LUT utilization (how many of the Artix-7's LUTs are used)
# 3. FF utilization
# 4. BRAM utilization
# 5. Maximum achievable frequency (from timing report)
```

### Documentation Requirements for Component 3

`docs/timing_report.txt` must contain the actual Vivado output including:
- WNS (worst negative slack)
- TNS (total negative slack)  
- Maximum frequency before timing violations
- LUT/FF/BRAM utilization percentages

`docs/design_decisions.md` must explicitly address:
- Why FPGA is slower than software for single-message parsing
- Why FPGA is valuable for parallel/pipelined multi-stream processing
- What clock frequency was achieved and what would be needed to match software latency
- The hardware vs software latency comparison table

---

## README.md — the document that matters most

This is what engineers at Graviton, Citadel, and AMD actually read. It must contain:

```markdown
# hft-engine

A three-component low-latency trading infrastructure stack built on commodity 
hardware, demonstrating:
- Lock-free order book with sub-200ns P99 add_order latency (C++20)
- Zero-copy FIX protocol parser over AF_XDP kernel bypass  
- Pipelined FIX parser on Artix-7 FPGA (SystemVerilog, Vivado)

## Motivation

[1 paragraph explaining the systems problem — not "I built this for fun" 
but "FIX parsing on the hot path introduces head-of-line blocking; 
offloading to FPGA enables the CPU to run matching logic concurrently..."]

## Hardware & Environment

- CPU: [your actual CPU model from /proc/cpuinfo]
- NIC: [your actual NIC, AF_XDP mode achieved: copy/zero-copy]
- FPGA: Xilinx Artix-7 [your specific part]
- OS: Ubuntu [version], Kernel [version]
- Compiler: GCC [version], -O3 -march=native

## Benchmark Results

### Order Book (Component 1)

| Operation | Baseline | Optimized | Improvement | Notes |
|---|---|---|---|---|
| SPSC push | Xns | Xns | Xx | false sharing eliminated |
| add_order (map) | Xns | — | — | std::map baseline |
| add_order (flat) | Xns | — | Xx faster | flat array wins at n<500 |
| add_order + match | Xns | — | — | P99 with 200 resting orders |

### AF_XDP Pipeline (Component 2)

| Metric | AF_XDP | Regular socket | Improvement |
|---|---|---|---|
| FIX parse throughput | X Mmsg/s | X Mmsg/s | Xx |
| End-to-end P50 | Xµs | Xµs | Xx |
| End-to-end P99 | Xµs | Xµs | Xx |

### FPGA Parser (Component 3)

| Metric | Value |
|---|---|
| Clock frequency | 100MHz (target), X MHz (achieved) |
| Parse latency | X cycles = X ns |
| LUT utilization | X% of XC7A100T |
| WNS | X ns |

## Design Decisions

See [docs/design_decisions.md] for detailed rationale on:
- Cache-line alignment and false sharing
- Memory ordering on x86 vs ARM
- Flat array vs tree for price levels
- Why FPGA offload trades single-message latency for parallel throughput
- AF_XDP copy mode vs zero-copy mode on this NIC
```

---

## Minimum Viable Target Numbers

These are the numbers you need to be able to defend in an interview. If your benchmarks don't hit these, the `design_decisions.md` must explain why and what the bottleneck is.

| Metric | Minimum | Strong | Elite |
|---|---|---|---|
| SPSC push P50 | <50ns | <15ns | <8ns |
| SPSC push P99 | <200ns | <50ns | <20ns |
| add_order P99 (flat) | <500ns | <200ns | <100ns |
| FIX parse throughput | >1M msg/s | >10M msg/s | >50M msg/s |
| AF_XDP vs socket speedup | >1.5x | >3x | >5x |
| FPGA timing closure | achieved | 100MHz | >150MHz |

---

## Build Instructions for Agent

```bash
# Prerequisites
sudo apt install cmake ninja-build gcc-12 g++-12 \
  libbpf-dev clang llvm linux-headers-$(uname -r) \
  libxdp-dev catch2 google-benchmark

# Build C++ components
mkdir build && cd build
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_CXX_COMPILER=g++-12 \
         -DCMAKE_CXX_FLAGS="-O3 -march=native -funroll-loops"
ninja

# Build BPF program
cd ../2_network/ebpf
clang -O2 -target bpf -c redirect.bpf.c -o redirect.bpf.o

# Run benchmarks (requires root for CPU pinning)
sudo ./scripts/setup_hugepages.sh
sudo ./scripts/run_benchmarks.sh 2>&1 | tee docs/benchmark_results.txt

# Vivado (requires Vivado installation)
cd ../3_fpga
vivado -mode batch -source scripts/synth.tcl
vivado -mode batch -source scripts/impl.tcl
```

---

One last thing the agent needs to know: **every X in the benchmark tables gets filled with a real number from your machine.** No placeholder values in the README. If a benchmark doesn't run, the README says "not yet measured" and explains why. Fake numbers are worse than no numbers — a Graviton engineer will ask you to reproduce them in the interview.
