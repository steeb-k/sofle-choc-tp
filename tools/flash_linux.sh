#!/usr/bin/env bash
# Flash both halves on Linux without babysitting: waits for an RP2040 bootloader
# (RPI-RP2) to appear, unmounts it if udisks auto-mounted it, loads the .uf2 with
# picotool and reboots the half, then waits for the next one. Run it, then
# double-tap reset on the left half, wait for it to come back, then the right.
#
#   tools/flash_linux.sh [uf2] [count] [timeout-seconds]
#
# Defaults: out/latest.uf2, 2 halves, 15 minutes per half. Needs picotool and its
# udev rule (Debian: /usr/lib/udev/rules.d/60-picotool.rules, user in plugdev).
set -euo pipefail
cd "$(dirname "$0")/.."
UF2="${1:-out/latest.uf2}"
COUNT="${2:-2}"
TIMEOUT="${3:-900}"
[ -f "$UF2" ] || { echo "ERROR: $UF2 not found (run ./build.sh)" >&2; exit 1; }
command -v picotool >/dev/null || { echo "ERROR: picotool not installed" >&2; exit 1; }

in_bootloader() { picotool info >/dev/null 2>&1; }

for ((n = 1; n <= COUNT; n++)); do
    echo "[$n/$COUNT] waiting for a half in bootloader mode (double-tap reset)..."
    for ((t = 0; t < TIMEOUT; t++)); do in_bootloader && break; sleep 1; done
    in_bootloader || { echo "ERROR: no bootloader seen in ${TIMEOUT}s" >&2; exit 1; }
    # udisks/DMS may have mounted the FAT volume; picotool wants the raw device.
    if [ -e /dev/disk/by-label/RPI-RP2 ]; then
        sleep 1
        udisksctl unmount -b /dev/disk/by-label/RPI-RP2 >/dev/null 2>&1 || true
    fi
    echo "[$n/$COUNT] flashing $UF2"
    picotool load -x "$UF2"
    echo "[$n/$COUNT] done, board rebooting"
    # Wait for the bootloader to go away before looking for the next half.
    for ((t = 0; t < 30; t++)); do in_bootloader || break; sleep 1; done
    sleep 2
done
echo "all $COUNT halves flashed"
