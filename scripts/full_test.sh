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

# Create results directory
mkdir -p "$RESULTS_DIR"

echo "==============================================="
echo "HFT Engine: Full Test & Profiling Suite"
echo "==============================================="
echo "Results: $RESULTS_DIR"
echo "Start time: $(date)"
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
} | tee "$RESULTS_DIR/system_info.txt"

# Step 1: Build
echo ""
echo "=== [1/5] Building ==="
cd "$PROJECT_ROOT"
rm -rf build
mkdir -p build
cd build

{
    cmake -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=native -funroll-loops -DNDEBUG" \
        ..
    ninja
} | tee "$RESULTS_DIR/build_log.txt"

if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed. See $RESULTS_DIR/build_log.txt"
    exit 1
fi

echo "[OK] Build successful"

# Step 2: Run Unit Tests
echo ""
echo "=== [2/5] Unit Tests ==="
{
    echo "=== SPSC Queue Tests ==="
    ./1_orderbook/test_spsc && echo "✓ SPSC tests passed" || echo "✗ SPSC tests failed"

    echo ""
    echo "=== Order Book Tests ==="
    ./1_orderbook/test_orderbook && echo "✓ Order book tests passed" || echo "✗ Order book tests failed"

    echo ""
    echo "=== FIX Parser Tests ==="
    ./2_network/test_fix_parser && echo "✓ FIX parser tests passed" || echo "✗ FIX parser tests failed"

    if [ "$WSL2_MODE" -eq 0 ]; then
        echo ""
        echo "=== AF_XDP Tests (Linux only) ==="
        if [ -f ./2_network/test_xsk ]; then
            ./2_network/test_xsk && echo "✓ AF_XDP tests passed" || echo "✗ AF_XDP tests failed"
        else
            echo "⊘ AF_XDP test binary not built (requires CAP_BPF)"
        fi
    fi
} | tee "$RESULTS_DIR/unit_tests.txt"

# Step 3: Run Benchmarks with perf profiling
echo ""
echo "=== [3/5] Benchmarks (with perf profiling) ==="

# Helper: run benchmark with perf stat
run_benchmark_with_perf() {
    local bench_name=$1
    local bench_binary=$2
    local perf_output="$RESULTS_DIR/perf_stat_${bench_name}.txt"

    echo ""
    echo "--- $bench_name ---"

    if [ ! -f "$bench_binary" ]; then
        echo "[SKIP] $bench_binary not found"
        return
    fi

    # Run with perf stat: measure cache misses, branch misses, context switches, IPC
    {
        echo "=== Benchmark: $bench_name ==="
        echo "Binary: $bench_binary"
        echo "Date: $(date)"
        echo ""

        # Pin to CPU 0 to avoid scheduler noise
        taskset -c 0 perf stat -e \
            cycles,instructions,cache-references,cache-misses,LLC-loads,LLC-load-misses,branches,branch-misses,context-switches,cpu-migrations \
            --metric-only \
            "$bench_binary" 2>&1 | tee -a "$perf_output"

        echo ""
        echo "Full perf output:"
        taskset -c 0 perf stat "$bench_binary" 2>&1 | tee -a "$perf_output"
    } | tee -a "$RESULTS_DIR/benchmarks_log.txt"

    echo "[OK] $bench_name complete. Perf data: $perf_output"
}

# Run each benchmark
for bench in bench_spsc bench_orderbook bench_realistic bench_fix_pipeline; do
    run_benchmark_with_perf "$bench" "./1_orderbook/$bench"
    run_benchmark_with_perf "$bench" "./2_network/$bench"
done

# Step 4: AF_XDP Profiling (if available)
if [ "$WSL2_MODE" -eq 0 ] && [ -f "./2_network/bench_af_xdp" ]; then
    echo ""
    echo "=== [4/5] AF_XDP End-to-End Profiling ==="

    {
        echo "=== AF_XDP Benchmark ==="
        echo "Running socket vs AF_XDP comparison..."
        taskset -c 0 perf stat -e cycles,instructions,cache-misses,context-switches \
            ./2_network/bench_af_xdp 2>&1
    } | tee "$RESULTS_DIR/perf_stat_af_xdp.txt"
else
    echo ""
    echo "=== [4/5] AF_XDP Tests ==="
    echo "[SKIP] AF_XDP benchmarks (requires CAP_BPF on native Linux)"
fi

# Step 5: Memory & Cache Analysis
echo ""
echo "=== [5/5] Memory & Cache Analysis ==="
if command -v valgrind &> /dev/null; then
    {
        echo "=== Memory Usage (Valgrind) ==="
        taskset -c 0 valgrind --tool=massif --massif-out-file="$RESULTS_DIR/massif.out" \
            ./1_orderbook/bench_orderbook --benchmark_filter="realistic_deep_ladder" 2>&1 | tail -20
    } | tee "$RESULTS_DIR/memory_analysis.txt"
else
    echo "[SKIP] Valgrind not installed"
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
