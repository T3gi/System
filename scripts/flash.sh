#!/usr/bin/env bash
# Flash STM32 via ST-Link. Strategy:
#   1) st-flash (SWD, always works)  -- primary method
#   2) mass-storage drag-and-drop    -- fallback if st-flash is unavailable
#
# Usage: ./flash.sh path/to/firmware.bin [mount_path]
set -euo pipefail

BIN="${1:-}"
MOUNT_HINT="${2:-/Volumes/NODE_F756ZG}"
FLASH_ADDR="${FLASH_ADDR:-0x08000000}"

if [[ -z "$BIN" ]]; then
    echo "Usage: $0 <firmware.bin> [mount_path]" >&2
    exit 1
fi

if [[ ! -f "$BIN" ]]; then
    echo "ERROR: firmware not found: $BIN" >&2
    exit 1
fi

SIZE=$(stat -f%z "$BIN" 2>/dev/null || stat -c%s "$BIN")

# --- Method 1: st-flash (SWD via libusb) ---
if command -v st-flash >/dev/null 2>&1; then
    echo ">>> Flashing $BIN ($SIZE bytes) via st-flash @ $FLASH_ADDR"
    # `--reset` performs a hardware reset after programming.
    if st-flash --reset write "$BIN" "$FLASH_ADDR"; then
        echo ">>> Flash OK. MCU has been reset and is now running the new firmware."
        exit 0
    fi
    echo "!!! st-flash failed, trying mass-storage fallback..." >&2
fi

# --- Method 2: mass-storage drag-and-drop ---
MOUNT="$MOUNT_HINT"
if [[ ! -d "$MOUNT" ]]; then
    for candidate in /Volumes/NODE_* /Volumes/DIS_* /Volumes/NUCLEO*; do
        if [[ -d "$candidate" ]]; then
            MOUNT="$candidate"
            break
        fi
    done
fi

if [[ ! -d "$MOUNT" ]]; then
    echo "ERROR: neither st-flash succeeded nor NUCLEO mass-storage volume was found." >&2
    echo "  Install st-flash:  brew install stlink" >&2
    echo "  Or make sure the board is connected via CN1 (ST-Link USB)." >&2
    exit 1
fi

echo ">>> Flashing $BIN ($SIZE bytes) via mass-storage: $MOUNT"
cp "$BIN" "$MOUNT/"
sync
echo ">>> OK. ST-Link is programming the MCU (LED blinks briefly)."
