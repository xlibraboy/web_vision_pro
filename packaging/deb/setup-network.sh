#!/bin/bash
# GigE tuning helper shipped with the Debian package: sets MTU 9000 (jumbo
# frames) on the camera network interface. The kernel buffers ship as
# /usr/lib/sysctl.d/99-papervision.conf and need no action here.
#
# Usage: sudo setup-network.sh [interface]
#   Without an argument the interface holding an address in 172.20.0.0/16 is
#   used, falling back to the first non-loopback interface that is up.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run as root (sudo $0 $*)" >&2
    exit 1
fi

INTERFACE="${1:-}"
if [ -z "$INTERFACE" ]; then
    INTERFACE="$(ip -o -4 addr show | awk '$4 ~ /^172\.20\./ {print $2; exit}')"
fi
if [ -z "$INTERFACE" ]; then
    INTERFACE="$(ip -o link show up | awk -F': ' '$2 != "lo" {print $2; exit}')"
fi
if [ -z "$INTERFACE" ] || ! ip link show "$INTERFACE" >/dev/null 2>&1; then
    echo "Could not determine the camera interface; pass it explicitly: sudo $0 <iface>" >&2
    exit 1
fi

echo "Setting MTU 9000 on $INTERFACE ..."
ip link set dev "$INTERFACE" mtu 9000
echo "Verify with: ip addr show $INTERFACE | grep mtu"
