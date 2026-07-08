# Vivado Build Script for HFT FPGA Accelerator on Artix-7
#
# Usage: vivado -mode batch -source vivado_build.tcl
#
# Target: Artix-7 (XC7A35T or similar)
# Performance: 200 MHz, ~300 ns per message parse

# Project configuration
set project_name "hft_fpga"
set project_dir "./build/vivado"
set rtl_dir "../rtl"
set tb_dir "../tb"
set board_part "digilentinc.com:nexys-a7-35t:part0"

# Create project
file mkdir $project_dir
create_project $project_name $project_dir -part xc7a35ticsg324-1L -force

# Add RTL source files
add_files -fileset sources_1 "$rtl_dir/fix_parser.v"

# Add testbench files
add_files -fileset sim_1 "$tb_dir/fix_parser_tb.v"

# Set top-level module
set_property top fix_parser [get_filesets sources_1]

# Synthesis settings
set_property "steps.synth_design.args.directive" "Explore" [get_runs synth_1]
set_property "steps.synth_design.args.fsm_extraction" "yes" [get_runs synth_1]

# Implementation settings (optimize for latency)
set_property "steps.place_design.args.directive" "Explore" [get_runs impl_1]
set_property "steps.phys_opt_design.is_enabled" "true" [get_runs impl_1]
set_property "steps.phys_opt_design.args.directive" "Explore" [get_runs impl_1]
set_property "steps.route_design.args.directive" "Explore" [get_runs impl_1]

# Clock constraint: 200 MHz (5 ns period)
create_clock -period 5.0 -name clk [get_ports clk]

# Run synthesis
puts "Running synthesis..."
launch_runs synth_1 -jobs 4
wait_on_run synth_1

if {[get_property PROGRESS [get_runs synth_1]] eq "100%"} {
    puts "[SYNTH OK] Synthesis complete"
} else {
    puts "[SYNTH FAIL] Synthesis failed"
    exit 1
}

# Run implementation
puts "Running implementation..."
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1

if {[get_property PROGRESS [get_runs impl_1]] eq "100%"} {
    puts "[IMPL OK] Implementation complete"
} else {
    puts "[IMPL FAIL] Implementation failed"
    exit 1
}

# Generate bitstream
puts "Generating bitstream..."
open_run impl_1
write_bitstream -force ./build/vivado/hft_fpga.bit

# Generate timing report
report_timing -file ./build/vivado/timing_report.txt

# Generate resource utilization
report_utilization -file ./build/vivado/utilization_report.txt

puts "[BUILD OK] Bitstream generated: ./build/vivado/hft_fpga.bit"
puts "Timing report: ./build/vivado/timing_report.txt"
puts "Utilization report: ./build/vivado/utilization_report.txt"

# Optional: run simulation
# launch_simulation
# run all

exit 0
