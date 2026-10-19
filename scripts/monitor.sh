#!/usr/bin/env bash
# UART monitor for the ST-Link Virtual COM port on Nucleo F756ZG.
# Uses picocom.  Press Ctrl-A then Ctrl-X to quit.
#
# Usage: ./monitor.sh [port] [baud] [--for SECONDS]
#
#   --for N   read for N seconds then exit (non-interactive, for capture logs)
set -euo pipefail

PORT="${1:-/dev/tty.usbmodem1203}"
BAUD="${2:-921600}"
FOR_SEC=""

# Parse optional --for N argument
if [[ "${3:-}" == "--for" && -n "${4:-}" ]]; then
    FOR_SEC="$4"
fi

# Auto-detect port if configured one doesn't exist.
if [[ ! -e "$PORT" ]]; then
    for candidate in /dev/tty.usbmodem*; do
        if [[ -e "$candidate" ]]; then
            PORT="$candidate"
            break
        fi
    done
fi

if [[ ! -e "$PORT" ]]; then
    echo "ERROR: no serial port found." >&2
    ls /dev/tty.usbmodem* 2>/dev/null || echo "  (nothing under /dev/tty.usbmodem*)" >&2
    exit 1
fi

if ! command -v picocom >/dev/null 2>&1; then
    echo "ERROR: picocom not installed.  Install with: brew install picocom" >&2
    exit 1
fi

if [[ -n "$FOR_SEC" ]]; then
    # Non-interactive mode: read for N seconds and exit. Useful for automation.
    echo ">>> Reading $PORT @ $BAUD for ${FOR_SEC}s..."
    exec picocom -qrb "$BAUD" --imap lfcrlf --exit-after "$((FOR_SEC * 1000))" "$PORT"
fi

echo ">>> Opening $PORT @ $BAUD baud (picocom). Press Ctrl-A then Ctrl-X to exit."
echo ""
exec picocom -b "$BAUD" --imap lfcrlf "$PORT"
