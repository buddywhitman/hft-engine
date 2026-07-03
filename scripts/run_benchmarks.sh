#!/bin/bash
# Run the full benchmark suite

set -e

BUILD_DIR="${1:-.}/build"

if [[ ! -f "$BUILD_DIR/1_orderbook/bench_orderbook" ]]; then
    echo "Build artifacts not found. Run: mkdir build && cd build && cmake .. -GNinja && ninja"
    exit 1
fi

echo "=== HFT Engine Benchmarks ==="
echo

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE=2  # Pin to core 2

echo "SPSC Queue Benchmark"
taskset -c $CORE chrt -f 99 "$BUILD_DIR/1_orderbook/bench_spsc" --benchmark_format=csv | tee /tmp/bench_spsc.csv
echo

echo "MPSC Queue Benchmark"
taskset -c $CORE chrt -f 99 "$BUILD_DIR/1_orderbook/bench_mpsc" --benchmark_format=csv | tee /tmp/bench_mpsc.csv
echo

echo "Order Book Benchmark"
taskset -c $CORE chrt -f 99 "$BUILD_DIR/1_orderbook/bench_orderbook" --benchmark_format=csv | tee /tmp/bench_orderbook.csv
echo

echo "FIX Pipeline Benchmark"
taskset -c $CORE chrt -f 99 "$BUILD_DIR/2_network/bench_fix_pipeline" --benchmark_format=csv | tee /tmp/bench_fix_pipeline.csv
echo

echo "=== Benchmark Summary ==="
echo "Results saved to /tmp/bench_*.csv"
