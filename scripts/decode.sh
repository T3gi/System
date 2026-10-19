#!/usr/bin/env bash
# Decode a base64-encoded UYVY frame from F401 + OV7670 into a PNG.
# UYVY byte order confirmed via TSLB=0x0D on the sensor.
#
# Usage:
#   ./scripts/decode.sh capture.txt              -> capture.png
#   pbpaste | ./scripts/decode.sh - shot.png     -> from clipboard
set -euo pipefail

IN="${1:-}"
OUT="${2:-}"
if [[ -z "$IN" ]]; then
    echo "Usage: $0 <input.txt|-> [output.png]" >&2
    exit 1
fi
if [[ -z "$OUT" ]]; then
    if [[ "$IN" == "-" ]]; then OUT="shot.png"; else OUT="${IN%.*}.png"; fi
fi

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT

if [[ "$IN" == "-" ]]; then cat; else cat "$IN"; fi \
    | sed -n '/^---YUV422-BEGIN/,/^---YUV422-END/{//!p;}' \
    | base64 -d > "$TMP"

SIZE=$(stat -f%z "$TMP" 2>/dev/null || stat -c%s "$TMP")
echo ">>> Decoded $SIZE bytes"

case "$SIZE" in
    25344)  W=176; H=72; UPSCALE=2  ;;   # QCIF-half — decoder pixel-doubles vertically
    50688)  W=176; H=144; UPSCALE=1 ;;
    38400)  W=160; H=120; UPSCALE=1 ;;
    153600) W=320; H=240; UPSCALE=1 ;;
    *) echo "ERROR: unexpected byte count $SIZE" >&2; exit 2 ;;
esac

echo ">>> Frame: ${W}x${H} UYVY (vertical x${UPSCALE} on output)"

python3 - "$TMP" "$OUT" "$W" "$H" "$UPSCALE" <<'PY'
import struct, zlib, sys
in_path, out_path, W, H, UPSCALE = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
data = open(in_path,'rb').read()

def yuv2rgb(Y, U, V):
    Y = Y - 16 if Y >= 16 else 0
    U -= 128; V -= 128
    R = (298*Y + 409*V + 128) >> 8
    G = (298*Y - 100*U - 208*V + 128) >> 8
    B = (298*Y + 516*U + 128) >> 8
    return max(0,min(255,R)), max(0,min(255,G)), max(0,min(255,B))

# UYVY byte order: U Y0 V Y1
rgb = bytearray()
for i in range(0, len(data), 4):
    U, Y0, V, Y1 = data[i], data[i+1], data[i+2], data[i+3]
    r,g,b = yuv2rgb(Y0,U,V); rgb += bytes([r,g,b])
    r,g,b = yuv2rgb(Y1,U,V); rgb += bytes([r,g,b])

# Nearest-neighbour vertical upscale so the aspect ratio ends up 4:3-ish.
H_out = H * UPSCALE
def chunk(t,d):
    return struct.pack('>I',len(d))+t+d+struct.pack('>I', zlib.crc32(t+d)&0xffffffff)
raw = b''
for y in range(H_out):
    src_y = y // UPSCALE
    raw += b'\x00' + bytes(rgb[src_y*W*3:(src_y+1)*W*3])
png  = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', W, H_out, 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(raw, 9))
png += chunk(b'IEND', b'')
open(out_path,'wb').write(png)
print(f'>>> Wrote {out_path} ({len(png)} bytes, {W}x{H_out} RGB888 PNG)')
PY

if command -v open >/dev/null 2>&1; then open "$OUT"; fi
