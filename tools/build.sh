#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
#
# Builds one variant with the full define set, so a call can never fall back to the root default
# (which boot-loops on a 16 MB board). ESP-IDF 6.1 must be in the environment (source its export.sh).
#
#   tools/build.sh <variant> [idf.py arguments]      variants: panel4 panel10 paneljc paneljc-v2 tab5
#   tools/build.sh panel4 build
#   tools/build.sh paneljc reconfigure build
#   tools/build.sh paneljc -p /dev/cu.usbmodemXXXX flash
#
# Extra overlay files (for example the betta easter egg) go into EXTRA_DEFAULTS, ';'-separated:
#   EXTRA_DEFAULTS=config/sdkconfig.defaults.easteregg tools/build.sh paneljc reconfigure build
#
# The configuration is generated from config/sdkconfig.defaults* into build/<variant>/sdkconfig:
# delete the build directory to start from a clean configuration.
set -eu
cd "$(dirname "$0")/.."
variant="${1:-}"
target=esp32p4
[ -n "$variant" ] || { sed -n 4,17p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
shift
case "$variant" in
  panel4|panel10)   defaults="config/sdkconfig.defaults;config/sdkconfig.defaults.$variant" ;;
  tab5)             defaults="config/sdkconfig.defaults;config/sdkconfig.defaults.tab5" ;;
  paneljc)          defaults="config/sdkconfig.defaults;config/sdkconfig.defaults.paneljc" ;;
  paneljc-v2)       defaults="config/sdkconfig.defaults;config/sdkconfig.defaults.paneljc;config/sdkconfig.defaults.paneljc.v2" ;;
  *) echo "unknown variant '$variant' (panel4 panel10 paneljc paneljc-v2 tab5)"; exit 2 ;;
esac
[ -z "${EXTRA_DEFAULTS:-}" ] || defaults="$defaults;$EXTRA_DEFAULTS"
command -v idf.py >/dev/null 2>&1 || { echo "idf.py not found: source ESP-IDF's export.sh first"; exit 2; }
rc=0
idf.py -B "build/$variant" -D "IDF_TARGET=$target" \
  -D "SDKCONFIG_DEFAULTS=$defaults" -D "SDKCONFIG=$PWD/build/$variant/sdkconfig" "$@" || rc=$?

# Keep the ELF of every successful build under .local/elf/<variant>/<sha256 prefix>.elf (git-ignored, the newest 12 per variant). A core
# dump (GET /api/coredump) can only be decoded against the ELF of the exact build that crashed -- esp-coredump compares the SHA256 it
# stores, and even a rebuild of the same commit differs (build time, "-dirty", a component version) -- so the next build must not
# destroy the evidence. Decode: esp-coredump --chip esp32p4 info_corefile -t raw -c core.bin .local/elf/<variant>/<prefix>*.elf
if [ "$rc" -eq 0 ]; then
  for elf in "build/$variant"/betta-ha-panel*.elf; do
    [ -f "$elf" ] || continue
    sum=$(shasum -a 256 "$elf" | cut -c1-9)
    mkdir -p ".local/elf/$variant"
    [ -f ".local/elf/$variant/$sum.elf" ] || cp "$elf" ".local/elf/$variant/$sum.elf"
    # shellcheck disable=SC2012
    ls -1t ".local/elf/$variant"/*.elf 2>/dev/null | tail -n +13 | while read -r old; do rm -f "$old"; done
  done
fi
exit "$rc"
