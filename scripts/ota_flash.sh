#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
#
# Flash a panel over Wi-Fi: POST the app image to the panel's OTA endpoint.
#
#   scripts/ota_flash.sh [--allow-stale] <panel-ip> [image.bin]
#
# The image is the plain app binary from the build directory (for paneljc
# build/paneljc/betta-ha-panel-jc.bin), not the factory image and not
# the *_flashed.bin copies. Without an argument the newest such binary in
# build/*/ is used. The panel checks that the file is an ESP app image whose
# project name matches the running firmware (so a panel4 image is refused by a
# paneljc panel), writes it to the next OTA slot and reboots. Only the app is
# written: bootloader or partition table changes still need USB.
#
# Refuses an image that is older than the firmware sources (main/, components/, config/, the root CMakeLists.txt):
# build first, flash once. --allow-stale flashes it anyway.
set -euo pipefail

allow_stale=0
if [ "${1:-}" = "--allow-stale" ]; then allow_stale=1; shift; fi
if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: $0 [--allow-stale] <panel-ip> [image.bin]" >&2
    exit 2
fi
ip=$1
image=${2:-}
root=$(cd "$(dirname "$0")/.." && pwd)
auth=()
[ -z "${BETTA_PASSWORD:-}" ] || auth=(-u "admin:$BETTA_PASSWORD")   # only when an admin password is set on the panel

if [ -z "$image" ]; then
    image=$(ls -t "$root"/build/*/betta-ha-panel*.bin 2>/dev/null | grep -v '_flashed\.bin$' | head -n 1 || true)
    [ -n "$image" ] || { echo "no image found in build/*/, pass one explicitly" >&2; exit 2; }
fi
[ -f "$image" ] || { echo "image not found: $image" >&2; exit 2; }

# Sources edited after the image was built: the panel would get firmware that does not contain them.
newer=$(find "$root/main" "$root/components" "$root/config" "$root/CMakeLists.txt" -type f -newer "$image" \
    -not -path '*/managed_components/*' -not -path '*/__pycache__/*' -not -name '*.pyc' 2>/dev/null || true)
if [ -n "$newer" ]; then
    count=$(printf '%s\n' "$newer" | wc -l | tr -d ' ')
    echo "image is older than $count source file(s), for example:" >&2
    printf '%s\n' "$newer" | head -n 5 | sed "s|^$root/|  |" >&2
    if [ "$allow_stale" -ne 1 ]; then
        echo "build again (scripts/build.sh <variant> build) or pass --allow-stale" >&2
        exit 3
    fi
    echo "continuing: --allow-stale" >&2
fi

base="http://$ip"
show_version() {
    curl -fsS -m 8 "$base/api/version" 2>/dev/null |
        python3 -c 'import json,sys; d=json.load(sys.stdin); print(d.get("project"), d.get("version"), d.get("git_commit"))' 2>/dev/null || true
}

echo "panel:  $ip ($(show_version))"
echo "image:  $image ($(wc -c < "$image" | tr -d ' ') bytes)"
echo "uploading..."
resp=$(curl -fsS ${auth[@]+"${auth[@]}"} --progress-bar -m 600 -X POST -H 'Content-Type: application/octet-stream' \
    --data-binary @"$image" "$base/api/ota/upload") || { echo "upload failed" >&2; exit 1; }
echo "$resp"
echo "waiting for the panel to reboot..."
sleep 8
for _ in $(seq 1 30); do
    v=$(show_version)
    if [ -n "$v" ]; then
        echo "panel is back: $v"
        exit 0
    fi
    sleep 3
done
echo "panel did not answer within 100 s" >&2
exit 1
