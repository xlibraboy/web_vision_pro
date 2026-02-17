#!/bin/bash

# Must be run as root
if [ "$EUID" -ne 0 ]; then 
  echo "Please run as root"
  exit 1
fi

INTERFACE=$1

if [ -z "$INTERFACE" ]; then
    echo "Usage: $0 <network_interface>"
    echo "Example: $0 eth0"
    exit 1
fi

echo "Optimizing Network Interface: $INTERFACE for Basler GigE"

# 1. MTU 9000 (Jumbo Frames)
ifconfig $INTERFACE mtu 9000 up
echo "[OK] MTU set to 9000"

# 2. Key UDP parameters for Linux
sysctl -w net.core.rmem_max=33554432
sysctl -w net.core.wmem_max=33554432
sysctl -w net.core.rmem_default=33554432
sysctl -w net.core.wmem_default=33554432
echo "[OK] Kernel UDP buffers increased"

# 3. Ring Buffers (requires ethtool)
if command -v ethtool &> /dev/null; then
    ethtool -G $INTERFACE rx 4096 tx 4096
    echo "[OK] Ring buffers input/output set to 4096"
else
    echo "[WARNING] ethtool not found, skipping ring buffer optimization"
fi

# 4. CPU Power Management (Prevent C-states for lower latency)
# This is usually done in GRUB, but purely setting performance governor helps
if [ -d "/sys/devices/system/cpu/cpu0/cpufreq" ]; then
    for CPUFREQ in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
    do
        [ -f $CPUFREQ ] || continue
        echo -n performance > $CPUFREQ
    done
    echo "[OK] CPU Governor set to performance"
fi
