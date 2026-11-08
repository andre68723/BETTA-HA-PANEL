#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
#
# Save what a panel currently shows as a lossless PNG file (GET /api/screenshot.png, about 0.2 MB, 1-2 s on the 10" panel):
#
#   scripts/panel_screenshot.sh <panel-ip> [out.png]
set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: $0 <panel-ip> [out.png]" >&2
    exit 2
fi
ip=$1
out=${2:-panel-$ip-$(date +%Y%m%d-%H%M%S).png}
trap 'rm -f "$out.part"' EXIT

auth=()
[ -z "${BETTA_PASSWORD:-}" ] || auth=(-u "admin:$BETTA_PASSWORD")
curl -fsS ${auth[@]+"${auth[@]}"} -m 90 -o "$out.part" "http://$ip/api/screenshot.png" || { echo "could not fetch the screenshot from $ip" >&2; exit 1; }
[ "$(head -c 4 "$out.part" | tail -c 3)" = "PNG" ] || { echo "the panel did not return a PNG" >&2; exit 1; }
mv "$out.part" "$out"
echo "$out"
