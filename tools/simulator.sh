#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
# Build and run the standalone LVGL/SDL widget preview.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
variant="${1:-panel4}"
if (($# > 0)); then shift; fi
case "$variant" in
    panel4|panel10|paneljc|paneljc-v2|tab5) ;;
    *) echo "Usage: tools/simulator.sh {panel4|panel10|paneljc|paneljc-v2|tab5} [simulator arguments]" >&2; exit 2 ;;
esac

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake is required (macOS: brew install cmake; Debian: apt install cmake)" >&2
    exit 2
fi
if ! command -v sdl2-config >/dev/null 2>&1; then
    echo "SDL2 development files are required (macOS: brew install sdl2; Debian: apt install libsdl2-dev)" >&2
    exit 2
fi

build_dir="$repo_root/build/simulator/$variant"
cmake -S "$repo_root/tests/simulator" -B "$build_dir" -DSIM_VARIANT="$variant" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir" --parallel
exec "$build_dir/betta-widget-sim" "$@"
