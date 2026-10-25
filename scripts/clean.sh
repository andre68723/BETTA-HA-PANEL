#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
#
# Deletes build directories and old packaged releases to free disk space. Each firmware build directory takes about
# 300-500 MB; scripts/build.sh recreates one from config/ in a few minutes.
#
#   scripts/clean.sh <variant>...     delete build/<variant> (panel4 panel10 paneljc paneljc-v2 tab5)
#   scripts/clean.sh --unowned        delete the variants that are only compile-checked here
#                                   (panel10, paneljc-v2); keeps panel4 and paneljc
#   scripts/clean.sh --all            delete every build directory, including build/simulator
#   scripts/clean.sh --release [N]    in release/out/ keep the N newest version folders (default 3), delete the rest
#   scripts/clean.sh --list           show the build directories and the release folders with their sizes
#
# managed_components/ and dependencies.lock* are kept: they are shared by all variants and would
# otherwise be downloaded again on the next build.
set -eu
cd "$(dirname "$0")/.."
variants="panel4 panel10 paneljc paneljc-v2 tab5"

remove() {
  local dir="build/$1"
  if [ -d "$dir" ]; then
    echo "removing $dir ($(du -sh "$dir" | cut -f1))"
    rm -rf "$dir"
  else
    echo "$dir: not present"
  fi
}

# release/out/<version>/ is written by scripts/package_release.sh and never pruned by it; keep the newest N by
# modification time. The C6 adapter firmware in release/ stays.
prune_release() {
  local keep="${1:-3}"
  case "$keep" in ''|*[!0-9]*) echo "--release takes a number of folders to keep"; exit 2 ;; esac
  [ -d release/out ] || { echo "release/out: not present"; return; }
  local old
  old=$(ls -t release/out | tail -n +"$((keep + 1))")
  [ -n "$old" ] || { echo "release/out: nothing beyond the newest $keep"; return; }
  echo "$old" | while read -r name; do
    echo "removing release/out/$name ($(du -sh "release/out/$name" | cut -f1))"
    rm -rf "release/out/$name"
  done
}

[ $# -gt 0 ] || { sed -n 5,17p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
case "$1" in
  --list)
    du -sh build/*/ 2>/dev/null || echo "no build directories"
    du -sh release/out/*/ 2>/dev/null || true
    exit 0 ;;
  --release)
    prune_release "${2:-3}"
    exit 0 ;;
  --unowned)
    set -- panel10 paneljc-v2 ;;
  --all)
    set -- $variants simulator ;;
esac
for name in "$@"; do
  case " $variants simulator " in
    *" $name "*) remove "$name" ;;
    *) echo "unknown variant '$name' ($variants, or simulator)"; exit 2 ;;
  esac
done
