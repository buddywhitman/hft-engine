#!/bin/bash
set -e

echo "=== HFT Engine System Setup ==="

# Check if running as root
if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root"
    exit 1
fi

# Enable hugepages
echo "Configuring hugepages..."
echo 1024 > /proc/sys/vm/nr_hugepages

# Disable CPU frequency scaling for determinism
echo "Disabling CPU frequency scaling..."
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance > "$cpu" 2>/dev/null || true
done

# Isolate core 2 from scheduling
echo "Configuring CPU isolation..."
systemctl set-default multi-user.target 2>/dev/null || true

# Lower latency budget for core 2
echo 0 > /sys/bus/cpu/devices/cpu2/power/pm_qos_resume_latency_us

# Lock CPU frequency to max
echo "Setting CPU frequency to maximum..."
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_max_freq; do
    if [[ -f "$cpu" ]]; then
        max_freq=$(cat "$cpu")
        echo "$max_freq" > "$(dirname $cpu)/scaling_setspeed" 2>/dev/null || true
    fi
done

echo "=== Setup Complete ==="
echo "Run benchmarks with: taskset -c 2 chrt -f 99 ./bench_orderbook"
