# HFT FPGA Accelerator - Artix-7

Hardware-accelerated FIX message parser for ultra-low-latency trading. Targets Artix-7 (Nexys A7-35T) with performance comparable to software implementations but with deterministic latency.

## Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Frequency | 200 MHz | Artix-7 timing closure |
| Cycles per message | 60-80 | ~300-400 ns per message |
| Latency | 300-500 ns | Deterministic (no OS jitter) |
| Messages/sec | 2-3 Mmsg/s | Theoretical throughput |

## Architecture

### Core Module: `fix_parser.v`

State machine-based FIX message parser:
- Parses FIX format: `tag=value|tag=value|...`
- Extracts key fields:
  - BeginString (tag 8)
  - MsgType (tag 35)
  - SenderCompID (tag 49)
  - TargetCompID (tag 56)
  - Sequence (tag 34)
- Input: Byte-streamed FIX message
- Output: Parsed fields + valid signal
- Implementable in ~200-300 LUTs

## Build Instructions

### Prerequisites

- Vivado 2023.x or later
- Xilinx Artix-7 board (Nexys A7-35T recommended)
- Linux environment with Vivado installed

### Synthesis & Implementation

```bash
cd 3_fpga
mkdir -p build/vivado
vivado -mode batch -source vivado_build.tcl

# Outputs:
# - build/vivado/hft_fpga.bit      (Bitstream for programming)
# - build/vivado/timing_report.txt (Timing analysis)
# - build/vivado/utilization_report.txt (Resource usage)
```

### Simulation

```bash
vivado -mode batch
  launch_simulation
  run all
  exit
```

## Integration with Software

The FPGA accelerator can be integrated via:

1. **Direct Memory Access (DMA)**: Kernel driver for high-throughput parsing
2. **Ethernet interface**: Custom protocol to offload parsing
3. **Comparison benchmark**: Run against software FIX parser

### Example Integration (pseudo-code)

```cpp
// Software interface to FPGA
class FPGAFixParser {
    uint64_t parse_latency_ns;  // Cycle counter from FPGA

    FixMessage parse(const uint8_t* packet, size_t len) {
        // Write packet to FPGA memory
        memcpy(fpga_mem, packet, len);

        // Trigger parsing
        fpga_control->start = 1;
        while (!fpga_control->done);

        // Read results
        FixMessage msg;
        msg.begin_string = fpga_regs->begin_string;
        msg.msg_type = fpga_regs->msg_type;
        parse_latency_ns = fpga_regs->cycles * 5;  // 5 ns @ 200 MHz
        return msg;
    }
};
```

## Comparison vs Software

### Software FIX Parser (C++)
- Latency: 50-200 ns (P99 varies with message size)
- Context switches: Yes (OS jitter +100-1000 ns)
- Determinism: Poor (cache misses, branch mispredicts)
- Power: ~10 W (full CPU)

### FPGA Parser (Artix-7)
- Latency: 300-500 ns (deterministic)
- Context switches: No (dedicated hardware)
- Determinism: Excellent (fixed pipeline)
- Power: ~2-5 W (just FPGA)

### Latency Tradeoff
- FPGA is slower for small messages (<300 ns)
- FPGA is better for consistency and power
- Hybrid: Use software for typical cases, FPGA as fallback

## Future Enhancements

1. **Parallel lanes**: Multiple FIX parsers for 5-10 Mmsg/s throughput
2. **Order book accelerator**: Verilog state machine for matching
3. **Integration with AF_XDP**: Hardware filtering + FPGA parsing
4. **Hardened IP**: Use Xilinx DSP48 blocks for checksum calculation
5. **Higher-end FPGA**: Ultrascale+ for 500 MHz + better resources

## References

- Vivado Design Suite: https://www.xilinx.com/products/design-tools/vivado.html
- Artix-7 User Guide: https://www.xilinx.com/support/documentation/user_guides/ug473_7Series_CLB.pdf
- FIX Protocol: https://www.fixtrading.org/standards/
