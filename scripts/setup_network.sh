#!/bin/bash
# Setup script for Industrial Vision Networking
# SKILL: industrial-vision-networking

if [ "$EUID" -ne 0 ]; then
  echo "Please run as root"
  exit 1
fi

echo "Applying Network Optimizations..."

# 1. Kernel Tuning (Receive Buffer Max/Default to 25MB)
sysctl -w net.core.rmem_max=26214400
sysctl -w net.core.rmem_default=26214400
sysctl -w net.core.wmem_max=26214400
sysctl -w net.core.wmem_default=26214400

# 2. NIC Configuration (MTU 9000 - Jumbo Frames)
# NOTE: Replace 'eth0' with your actual GigE interface name
INTERFACE="eth0" 
if ip link show $INTERFACE > /dev/null 2>&1; then
    echo "Setting MTU 9000 on $INTERFACE..."
    ip link set dev $INTERFACE mtu 9000
else
    echo "Warning: Interface $INTERFACE not found. Please edit this script with the correct interface name."
fi

# 3. Interrupt Moderation (Driver dependent, example works for Intel e1000/igb)
# output=$(ethtool -C $INTERFACE rx-usecs 10 2>/dev/null)
# if [ $? -eq 0 ]; then
#     echo "Set interrupt moderation to low latency."
# else
#     echo "Could not set interrupt moderation (ethtool not installed or not supported)."
# fi

echo "Network optimization complete."
echo "Verify with: ip addr show $INTERFACE | grep mtu"
