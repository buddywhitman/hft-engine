#!/bin/bash
# Build BPF programs for AF_XDP

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BPF_DIR="$SCRIPT_DIR/bpf"
OUTPUT_DIR="$SCRIPT_DIR/bpf/build"

mkdir -p "$OUTPUT_DIR"

echo "Building BPF programs..."

# Check for clang
if ! command -v clang &> /dev/null; then
    echo "[ERROR] clang not found. Install: sudo apt-get install clang llvm"
    exit 1
fi

# Build xdp_fix_redirect
echo "[1/1] Building xdp_fix_redirect.o..."
clang -O2 -target bpf \
    -D__KERNEL__ -D__BPF_TRACING__ \
    -c "$BPF_DIR/xdp_fix_redirect.c" \
    -o "$OUTPUT_DIR/xdp_fix_redirect.o"

if [ $? -eq 0 ]; then
    echo "[OK] BPF programs built successfully"
    echo "Output: $OUTPUT_DIR/xdp_fix_redirect.o"
else
    echo "[ERROR] BPF compilation failed"
    exit 1
fi
