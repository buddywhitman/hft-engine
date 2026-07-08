#!/bin/bash
set -e

# HFT Engine: Comprehensive Test & Profiling Suite
# Run on Ubuntu with CAP_BPF/CAP_NET_ADMIN for AF_XDP tests

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RESULTS_DIR="${PROJECT_ROOT}/results/$(date +%Y%m%d_%H%M%S)"

# Detect if running on real Linux or WSL2
if grep -qi microsoft /proc/version; then
    echo "[WARN] Running in WSL2. AF_XDP tests will be skipped (requires CAP_BPF on native Linux)."
    WSL2_MODE=1
else
    WSL2_MODE=0
    echo "[INFO] Running on native Linux. AF_XDP tests enabled."
fi

# Check perf availability upfront so we don't spam the same permission
# error once per benchmark. perf_event_paranoid > 1 blocks CPU event
# access for non-root users; > 2 also blocks kernel profiling.
PERF_USABLE=1
if command -v perf &> /dev/null; then
    PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "unknown")
    if [ "$PARANOID" != "unknown" ] && [ "$PARANOID" -gt 1 ] 2>/dev/null; then
        # Quick real check: paranoid level alone doesn't guarantee failure
        # (capabilities can override it), so actually try a trivial perf run.
        if ! taskset -c 0 perf stat -e cycles -- true &> /dev/null; then
            PERF_USABLE=0
            echo "[WARN] perf_event_paranoid=$PARANOID blocks unprivileged perf access."
            echo "[WARN] Benchmarks will run WITHOUT perf profiling (raw latency numbers only)."
            echo "[WARN] To fix: sudo sysctl -w kernel.perf_event_paranoid=1"
            echo "[WARN]   (or run this script with sudo, or add CAP_PERFMON to the binary)"
        fi
    fi
else
    PERF_USABLE=0
    echo "[WARN] perf not installed. Install: sudo apt-get install linux-tools-common linux-tools-\$(uname -r)"
fi

# Create results directory
mkdir -p "$RESULTS_DIR"

echo "==============================================="
echo "HFT Engine: Full Test & Profiling Suite"
echo "==============================================="
echo "Results: $RESULTS_DIR"
echo "Start time: $(date)"
echo "perf profiling: $([ "$PERF_USABLE" -eq 1 ] && echo enabled || echo DISABLED)"
echo ""

# Capture system info
{
    echo "=== System Info ==="
    uname -a
    echo ""
    echo "=== CPU Info ==="
    lscpu | grep -E "^Model|^Thread|^Core|MHz"
    echo ""
    echo "=== Memory Info ==="
    free -h
    echo ""
    echo "=== Hugepages ==="
    cat /proc/meminfo | grep -i huge
    echo ""
    echo "=== Governor ==="
    cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "N/A"
    if [ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ] && \
       [ "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)" != "performance" ]; then
        echo "[WARN] Governor is not 'performance'. Benchmark numbers will be noisier."
        echo "[WARN] Fix: sudo cpupower frequency-set -g performance"
    fi
    echo ""
    echo "=== perf availability ==="
    echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 'N/A')"
    echo "perf usable without sudo: $([ "$PERF_USABLE" -eq 1 ] && echo yes || echo no)"
} | tee "$RESULTS_DIR/system_info.txt"

# Step 1: Build
echo ""
echo "=== [1/5] Building ==="
cd "$PROJECT_ROOT"
rm -rf build
mkdir -p build
cd build

# Capture both stdout AND stderr — CMake warnings (e.g. missing libbpf)
# and compiler errors go to stderr and were previously lost from the log.
set +e
{
    cmake -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=native -funroll-loops -DNDEBUG" \
        .. 2>&1
    ninja 2>&1
} | tee "$RESULTS_DIR/build_log.txt"
BUILD_STATUS=${PIPESTATUS[0]}
set -e

if [ "$BUILD_STATUS" -ne 0 ] || grep -q "FAILED:" "$RESULTS_DIR/build_log.txt"; then
    echo "[ERROR] Build failed. See $RESULTS_DIR/build_log.txt"
    echo "[ERROR] Continuing with whatever binaries did build, so partial results are still useful."
fi

if grep -q "libbpf not found" "$RESULTS_DIR/build_log.txt"; then
    echo "[INFO] AF_XDP disabled (libbpf-dev not installed). Install with:"
    echo "[INFO]   sudo apt-get install libbpf-dev libxdp-dev"
fi

echo "[OK] Build step complete"

# Step 2: Run Unit Tests
echo ""
echo "=== [2/5] Unit Tests ==="
{
    run_test() {
        local name=$1
        local binary=$2
        echo ""
        echo "=== $name ==="
        if [ ! -f "$binary" ]; then
            echo "⊘ $name binary not built (see build_log.txt)"
            return
        fi
        if "$binary"; then
            echo "✓ $name passed"
        else
            echo "✗ $name failed"
        fi
    }

    run_test "SPSC Queue Tests" "./1_orderbook/test_spsc"
    run_test "MPSC Queue Tests" "./1_orderbook/test_mpsc"
    run_test "Order Book Tests" "./1_orderbook/test_orderbook"
    run_test "FIX Parser Tests" "./2_network/test_fix_parser"

    if [ "$WSL2_MODE" -eq 0 ]; then
        run_test "AF_XDP Tests (Linux only)" "./2_network/test_xsk"
    fi
} | tee "$RESULTS_DIR/unit_tests.txt"

# Step 3: Run Benchmarks with perf profiling (if available)
echo ""
echo "=== [3/5] Benchmarks $([ "$PERF_USABLE" -eq 1 ] && echo '(with perf profiling)' || echo '(perf unavailable, raw latency only)') ==="

# Helper: run benchmark, with perf stat wrapping only if usable
run_benchmark() {
    local bench_name=$1
    local bench_binary=$2
    local perf_output="$RESULTS_DIR/perf_stat_${bench_name}.txt"

    echo ""
    echo "--- $bench_name ---"

    if [ ! -f "$bench_binary" ]; then
        echo "[SKIP] $bench_binary not found (not built)"
        return
    fi

    {
        echo "=== Benchmark: $bench_name ==="
        echo "Binary: $bench_binary"
        echo "Date: $(date)"
        echo ""

        if [ "$PERF_USABLE" -eq 1 ]; then
            # Single perf stat run with full (non-metric-only) output:
            # includes raw counts AND derived metrics like IPC in one pass.
            taskset -c 0 perf stat -e \
                cycles,instructions,cache-references,cache-misses,LLC-loads,LLC-load-misses,branches,branch-misses,context-switches,cpu-migrations \
                -- "$bench_binary" 2>&1 | tee -a "$perf_output"
        else
            echo "[perf unavailable, running without profiling]"
            taskset -c 0 "$bench_binary" 2>&1 | tee -a "$perf_output"
        fi
    } | tee -a "$RESULTS_DIR/benchmarks_log.txt"

    echo "[OK] $bench_name complete. Data: $perf_output"
}

# Map each benchmark to its actual build directory (not a cross-product guess)
run_benchmark "bench_spsc" "./1_orderbook/bench_spsc"
run_benchmark "bench_mpsc" "./1_orderbook/bench_mpsc"
run_benchmark "bench_orderbook" "./1_orderbook/bench_orderbook"
run_benchmark "bench_realistic" "./1_orderbook/bench_realistic"
run_benchmark "bench_profiling" "./1_orderbook/bench_profiling"
run_benchmark "bench_fix_pipeline" "./2_network/bench_fix_pipeline"
run_benchmark "bench_af_xdp" "./2_network/bench_af_xdp"

# Step 4: AF_XDP live network profiling notice
echo ""
echo "=== [4/5] AF_XDP Live Network Test ==="
echo "[INFO] bench_af_xdp above uses socket fallback + synthetic comparisons."
echo "[INFO] Live AF_XDP zero-copy testing requires: real NIC traffic, CAP_BPF,"
echo "[INFO] and load_bpf_program() called against an actual interface."
echo "[INFO] Not automated here — see DEPLOYMENT.md for manual XDP attach steps."

# Step 5: Memory & Cache Analysis
echo ""
echo "=== [5/5] Memory & Cache Analysis ==="
if command -v valgrind &> /dev/null && [ -f ./1_orderbook/bench_orderbook ]; then
    {
        echo "=== Memory Usage (Valgrind) ==="
        taskset -c 0 valgrind --tool=massif --massif-out-file="$RESULTS_DIR/massif.out" \
            ./1_orderbook/bench_orderbook --benchmark_filter="realistic_deep_ladder" 2>&1 | tail -20
    } | tee "$RESULTS_DIR/memory_analysis.txt"
else
    echo "[SKIP] Valgrind not installed or bench_orderbook not built"
fi

# Summary
echo ""
echo "==============================================="
echo "Test Run Complete"
echo "==============================================="
echo "Results saved to: $RESULTS_DIR"
echo "End time: $(date)"
echo ""
echo "Next steps:"
echo "1. Review results: cat $RESULTS_DIR/perf_stat_*.txt"
echo "2. Push to git: git add results/ && git commit -m 'Add profiling results'"
echo "3. Pull on main machine and analyze"
echo ""
