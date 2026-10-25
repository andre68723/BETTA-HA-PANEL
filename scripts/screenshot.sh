#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
#
# Save what a panel currently shows as a PNG file:
#
#   scripts/screenshot.sh <panel-ip> [out.png]
#
# The firmware serves its rendered LVGL framebuffer as a BMP at
# /api/screenshot.bmp (about 3 MB on the 10" panel, streamed slowly, so the
# request gets a generous timeout: a short one silently truncates the image).
# The BMP is converted with macOS `sips`, ImageMagick or Pillow, whichever is
# available; if none is, the BMP is kept.
set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: $0 <panel-ip> [out.png]" >&2
    exit 2
fi
ip=$1
out=${2:-panel-$ip-$(date +%Y%m%d-%H%M%S).png}
tmp=$(mktemp -t panel-shot).bmp
trap 'rm -f "$tmp"' EXIT

auth=()
[ -z "${BETTA_PASSWORD:-}" ] || auth=(-u "admin:$BETTA_PASSWORD")
curl -fsS ${auth[@]+"${auth[@]}"} -m 90 -o "$tmp" "http://$ip/api/screenshot.bmp" || { echo "could not fetch the screenshot from $ip" >&2; exit 1; }

# A truncated download has fewer bytes than the size stored in the BMP header.
python3 - "$tmp" <<'PY' || { echo "the screenshot download was truncated, try again" >&2; exit 1; }
import os, struct, sys
path = sys.argv[1]
with open(path, "rb") as f:
    head = f.read(6)
if head[:2] != b"BM":
    sys.exit("not a BMP file")
if os.path.getsize(path) < struct.unpack("<I", head[2:6])[0]:
    sys.exit("truncated")
PY

if command -v sips >/dev/null 2>&1; then
    sips -s format png "$tmp" --out "$out" >/dev/null
elif command -v convert >/dev/null 2>&1; then
    convert "$tmp" "$out"
elif python3 -c 'import PIL' 2>/dev/null; then
    python3 -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).convert("RGB").save(sys.argv[2])' "$tmp" "$out"
else
    out=${out%.png}.bmp
    cp "$tmp" "$out"
    echo "no converter found (sips, ImageMagick, Pillow), saved the BMP instead" >&2
fi
echo "$out"
