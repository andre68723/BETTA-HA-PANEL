#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
#
# Checks that need no device. Quiet by default: one line per step, the full output of every step goes to a log
# file, and only a failing step prints its (deduplicated) failure lines.
#   tools/check.sh            editor JavaScript and tests, HTML and translations, file sizes, simulator
#                             regressions, widget layout at the standard text size on every variant, host tests.
#                             A slow step whose inputs are unchanged since its last pass is skipped.
#   tools/check.sh --full     also the widget layout at the Larger and Largest text steps (CI always does)
#   tools/check.sh --build    also builds all five variants (needs ESP-IDF 6.1 in the environment)
#   tools/check.sh --force    ignore the stamps and run everything (CI always does)
#   tools/check.sh --verbose  print every step's full output as it runs
#   tools/check.sh --group G  only one group of steps (CI runs the groups as parallel jobs): web, host, sim-panel4,
#                             sim-paneljc or sim-tab5; the simulator groups hold a variant's regressions and layout
# Logs: ${TMPDIR:-/tmp}/betta-check/<step>.log (BETTA_CHECK_LOG_DIR overrides). Stamps: build/check-stamps/ (delete the folder, or pass --force, to rerun everything).
set -u
cd "$(dirname "$0")/.."

build=0; full=0; force=0; verbose=0; group=""
while [ $# -gt 0 ]; do
  arg="$1"; shift
  case "$arg" in
    --group) group="${1:-}"; shift || true ;;
    --build) build=1 ;;
    --full) full=1 ;;
    --force) force=1 ;;
    --verbose) verbose=1 ;;
    *) echo "usage: tools/check.sh [--build] [--full] [--force] [--verbose] [--group web|host|sim-panel4|sim-paneljc|sim-tab5]" >&2; exit 2 ;;
  esac
done
case "$group" in ""|web|host|sim-panel4|sim-paneljc|sim-tab5) ;; *) echo "unknown group '$group'" >&2; exit 2 ;; esac
# A step group runs when no group is selected or when it is the selected one.
in_group() { [ -z "$group" ] || [ "$group" = "$1" ]; }
# CI must never skip a step because of a stamp, and runs the whole text-step matrix.
if [ -n "${CI:-}" ]; then force=1; full=1; fi

status=0
skipped=""
logdir="${BETTA_CHECK_LOG_DIR:-${TMPDIR:-/tmp}/betta-check}"
stampdir="build/check-stamps"
mkdir -p "$logdir" "$stampdir"

if command -v shasum >/dev/null 2>&1; then sha() { shasum -a 256 "$@"; }; else sha() { sha256sum "$@"; }; fi
rm -f "$logdir"/.files-*

# A check that cannot run is listed in the summary. Under CI (the CI variable is set) it fails instead,
# so a broken runner image cannot turn a check into a silent pass.
skip() {
  echo "skip  $2 ($1)"
  skipped="$skipped
  - $2"
  if [ -n "${CI:-}" ]; then echo "FAIL  $2 is required when CI is set"; status=1; fi
}

# Fingerprint of a step: its command and the content of every input file. The file hashes of one input list are
# computed once per run: most slow steps share the same inputs.
fingerprint() {
  local cmd="$1" inputs="$2" key cache p
  key="$(printf '%s' "$inputs" | sha | cut -c1-16)"
  cache="$logdir/.files-$key"
  if [ ! -f "$cache" ]; then
    for p in $inputs; do
      if [ -d "$p" ]; then find "$p" -type f -not -name '*.pyc' -not -path '*/__pycache__/*' -print0
      elif [ -f "$p" ]; then printf '%s\0' "$p"; fi
    done | LC_ALL=C sort -z | xargs -0 shasum -a 256 2>/dev/null >"$cache"
  fi
  { echo "$cmd"; cat "$cache"; } | sha | cut -c1-24
}

# step <name> <inputs> <command...>
#   inputs: space separated files or directories whose content decides whether the step may be skipped;
#           empty = the step is cheap and always runs. A passing step with inputs writes a stamp.
#   SKIP_RC / SKIP_WHY (in the environment of the call): an exit code that means "could not run, not failed".
step() {
  local name="$1" inputs="$2"; shift 2
  local slug log stamp fp start rc end warn note
  slug="$(printf '%s' "$name" | tr -c 'A-Za-z0-9\n' '-' | tr -s '-')"
  log="$logdir/$slug.log"
  stamp="$stampdir/$slug"
  fp=""
  if [ -n "$inputs" ]; then
    fp="$(fingerprint "$*" "$inputs")"
    if [ "$force" -eq 0 ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$fp" ]; then
      echo "skip  $name (unchanged since its last pass)"
      return 0
    fi
  fi
  start=$(date +%s)
  if [ "$verbose" -eq 1 ]; then
    echo "== $name"
    "$@" 2>&1 | tee "$log"; rc=${PIPESTATUS[0]}
  else
    "$@" >"$log" 2>&1; rc=$?
  fi
  end=$(date +%s)
  if [ -n "${SKIP_RC:-}" ] && [ "$rc" -eq "$SKIP_RC" ]; then
    skip "${SKIP_WHY:-could not run}" "$name"
    return 0
  fi
  if [ "$rc" -eq 0 ]; then
    [ -n "$fp" ] && printf '%s' "$fp" >"$stamp"
    warn=$(grep -c '^WARN' "$log" 2>/dev/null); warn=${warn:-0}
    note=""; [ "$warn" -gt 0 ] && note=", $warn warnings in the log"
    printf 'ok    %s (%ss%s)\n' "$name" "$((end - start))" "$note"
  else
    rm -f "$stamp"
    echo "FAIL  $name (exit $rc, ${end}s, log: $log)"
    # The failure lines, once each: most steps repeat every problem in a summary.
    grep -v -E '^(--|\[[ 0-9]+%\]|make|gmake)|PASS|checks passed|Using configuration|PkgConfig|^$' "$log" \
      | awk '!seen[$0]++' | tail -n 40 | sed 's/^/      /'
    status=1
  fi
}

# The simulator needs no window: SDL's dummy driver also works on a CI runner without a display.
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}"

have_node=0; command -v node >/dev/null 2>&1 && have_node=1
if ! in_group web; then :
elif [ "$have_node" -eq 1 ]; then
  step "editor JavaScript syntax" "" bash -c 'for f in components/webui/www/js/*.js; do node --check "$f" || exit 1; done'
  # Files, not the directory: Node 22 (CI) treats a directory argument as a module to load.
  step "editor unit tests" "" bash -c 'node --test tests/webui/*.test.mjs'
else
  skip "node not found" "editor JavaScript syntax"
  skip "node not found" "editor unit tests"
fi

if in_group web; then
  step "HTML and translation tables" "" python3 tools/check_web.py
  SKIP_RC=2 SKIP_WHY="fontTools not installed" step "font subsets" \
    "tools/generate_widget_fonts.py main/ui/fonts" python3 tools/generate_widget_fonts.py --check
  step "source file sizes" "" python3 tools/check_source_sizes.py
  step "documentation page sizes" "" python3 tools/check_docs_size.py
  step "simulator layout fields" "" python3 tools/check_simulator_fields.py

  # A hint, never a failure: show its one line when it has something to say.
  python3 tools/check_gallery_fresh.py 2>&1 | grep '^WARN' | cut -c1-200
fi

# Everything the simulator renders depends on the firmware sources and the fixtures.
sim_inputs="main tests/simulator tools/simulator.sh tools/check_widget_layout.py tools/check_readability.py tools/ui_styles.py tools/generate_simulator_gallery.py tools/gallery_content.py"
if [ -n "$group" ] && [ "${group#sim-}" = "$group" ]; then
  : # no simulator group selected
elif command -v cmake >/dev/null 2>&1 && command -v sdl2-config >/dev/null 2>&1; then
  # Text grows with the user's Larger and Largest steps and tiles cap it; nothing may overlap. The standard step
  # runs on every check, the other two with --full (and always in CI).
  steps="0"; [ "$full" -eq 1 ] && steps="0 1 2"
  for variant in panel4 paneljc tab5; do
    in_group "sim-$variant" || continue
    step "simulator regressions $variant" "$sim_inputs" bash -c '
      for r in controls wallbox bambu history energy fonts notification price parqet; do
        SDL_VIDEODRIVER=dummy tools/simulator.sh "$0" --regression "$r" || exit 1
      done' "$variant"
    for s in $steps; do
      case "$variant" in
        panel4) step "widget layout panel4 step $s" "$sim_inputs" python3 tools/check_widget_layout.py --text-step "$s" ;;
        *) step "widget layout $variant step $s" "$sim_inputs" python3 tools/check_widget_layout.py --variant "$variant" --text-step "$s" ;;
      esac
    done
  done
else
  skip "cmake or SDL2 not found (macOS: brew install cmake sdl2; Debian: apt install cmake libsdl2-dev)" "widget layout (SDL simulator)"
fi

if in_group host; then
  for variant in panel4 paneljc; do
    step "host tests $variant" "main tests/host tools/test_host.sh" env VARIANT="$variant" tools/test_host.sh
  done
fi

if [ "$build" -eq 1 ] && [ -z "$group" ]; then
  # The component patches (tools/apply_component_patches.py) are applied automatically by the root
  # CMakeLists.txt on every configure, not called here separately any more.
  # tools/build.sh passes the full define set (see docs/development/build.md), one build directory per variant.
  for variant in panel4 panel10 paneljc paneljc-v2 tab5; do
    step "build $variant" "" tools/build.sh "$variant" build
    case "$variant" in panel4|panel10) want=32MB ;; *) want=16MB ;; esac
    if ! head -1 "build/$variant/flash_args" | grep -q -- "--flash-size $want"; then
      echo "FAIL  build/$variant: flash size in flash_args is not $want"
      status=1
    fi
  done
fi

[ -z "$skipped" ] || printf 'not run:%s\n' "$skipped"
if [ $status -eq 0 ]; then echo "all checks passed (logs: $logdir)"; else echo "CHECKS FAILED (logs: $logdir)"; fi
exit $status
