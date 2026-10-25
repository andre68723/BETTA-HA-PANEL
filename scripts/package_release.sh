#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
#
# Package release images from existing builds:
#
#   scripts/package_release.sh <version> <variant>...      e.g.  scripts/package_release.sh v0.9.0-rc3 panel4 paneljc
#
# For each variant it writes into release/out/<version>/ (git-ignored):
#   betta86-ha-panel-<version>-<variant>.factory.bin   bootloader, partition table, app and C6 firmware for address 0x0 (USB flash;
#                                                       padded across the NVS range, so it erases stored credentials: to keep them
#                                                       write the parts separately, docs/development/build.md "Changing the partition table")
#   betta86-ha-panel-<version>-<variant>.ota.bin       the app binary (OTA upload and the panel's update check)
# and a SHA256SUMS file. The names are what the update check looks for (`betta86-ha-panel-*-<variant>.ota.bin`).
#
# Needs ESP-IDF 6.1 in the environment (esptool) and a finished build of every variant that reports <version>:
#   (edit BETTAOS_RELEASE_VERSION in CMakeLists.txt, or BETTA_VERSION=<version>)  scripts/build.sh <variant> reconfigure build
# The script refuses a build whose version differs, so a stale build cannot ship under a new name.
# It does not publish anything; see docs/development/release.md ("Publishing a release candidate").
set -euo pipefail

if [ $# -lt 2 ]; then
    echo "usage: $0 <version> <variant>..." >&2
    exit 2
fi
version=$1
shift
root=$(cd "$(dirname "$0")/.." && pwd)
out="$root/release/out/$version"
command -v esptool >/dev/null 2>&1 || command -v esptool.py >/dev/null 2>&1 || { echo "esptool not found: source ESP-IDF's export.sh first" >&2; exit 2; }
esptool_cmd=esptool
command -v esptool >/dev/null 2>&1 || esptool_cmd=esptool.py

rm -rf "$out"
mkdir -p "$out"

for variant in "$@"; do
    build="$root/build/$variant"
    [ -f "$build/flash_args" ] || { echo "no build for $variant ($build/flash_args missing): run scripts/build.sh $variant build" >&2; exit 1; }
    built=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['project_version'])" "$build/project_description.json")
    if [ "$built" != "$version" ]; then
        echo "build/$variant reports $built, not $version: rebuild it with the right version first" >&2
        exit 1
    fi
    app=$(tail -n 1 "$build/flash_args" | awk '{print $2}') # the last entry of flash_args is the app image
    chip=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['target'])" "$build/project_description.json")
    base="betta86-ha-panel-$version-$variant"
    (cd "$build" && "$esptool_cmd" --chip "$chip" merge-bin -o "$out/$base.factory.bin" @flash_args >/dev/null)
    cp "$build/$app" "$out/$base.ota.bin"
    echo "$variant: $base.factory.bin and $base.ota.bin"
done

(cd "$out" && shasum -a 256 ./*.bin | sed 's| \./| |' > SHA256SUMS)
ls -l "$out" | awk 'NR>1 {print $5, $9}'
