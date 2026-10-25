#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
# One input list per step drives both stamps and --changed; usage: --help.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
usage() {
  echo 'usage: scripts/check.sh [--quick|--full] [--changed] [--list] [--force] [--verbose] [--keep-going] [--build] [--group web|docs|host|sim-panel4|sim-paneljc|sim-tab5]'
}
build=0; full=0; quick=0; changed=0; list=0; force=0; verbose=0; keep=0; group=""
while [ $# -gt 0 ]; do
  case "$1" in
    --help|-h) usage; exit 0 ;;
    --group) [ $# -gt 1 ] && [ -n "$2" ] || { usage >&2; exit 2; }; group="$2"; shift ;;
    --build) build=1 ;; --full) full=1 ;; --quick) quick=1 ;; --changed) changed=1 ;;
    --list) list=1 ;; --force) force=1 ;; --verbose) verbose=1 ;; --keep-going) keep=1 ;;
    *) usage >&2; exit 2 ;;
  esac
  shift
done
case "$group" in ""|web|docs|host|sim-panel4|sim-paneljc|sim-tab5) ;; *) usage >&2; exit 2 ;; esac
if ((quick && (full || build))) || { ((build)) && [ -n "$group" ]; } || { [ -n "${CI:-}" ] && ((quick || changed)); }; then usage >&2; exit 2; fi
[ -z "${CI:-}" ] || { force=1; full=1; }
in_group() { [ -z "$group" ] || [ "$group" = "$1" ]; }
status=0; passed=0; cached=0; missing=0; warned=0; selected=0; fallback=0; planning=1; all_inputs=""; started=$SECONDS
logdir="${BETTA_CHECK_LOG_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/betta-check.XXXXXX")}"
stampdir="build/check-stamps"; mkdir -p "$logdir" "$stampdir"
rm -f "$logdir"/.files-*
common="scripts/check.sh pyproject.toml uv.lock .gitignore"
sha() { shasum -a 256 "$@"; }
# Include tool identities as well as locked inputs; CI never reuses local stamps.
runtime=$( {
  python3 -c 'import os,sys; sys.path.insert(0,"tools"); from generate_simulator_gallery import FIXED_NOW,use_fixed_clock; use_fixed_clock(os.environ.get("BETTA_SIM_NOW",FIXED_NOW)); print("TZ=%s BETTA_SIM_NOW=%s" % (os.environ.get("TZ","system"),os.environ["BETTA_SIM_NOW"]))'
  uv --version; uname -sr; pkg-config --modversion libcjson;
  python3 -c "import importlib.metadata as m; print(sorted((d.metadata['Name'],d.version) for d in m.distributions() if d.metadata.get('Name','').lower() in ('pillow','fonttools','brotli')))";
  python3 --version; node --version; cc --version | head -1;
  cmake --version | head -1; sdl2-config --version;
} 2>/dev/null)
summary() {
  local rc=$?
  ((list)) || printf 'check: exit=%s duration=%ss passed=%s cached=%s unavailable=%s warnings=%s selected=%s logs=%s\n' "$rc" "$((SECONDS-started))" "$passed" "$cached" "$missing" "$warned" "$selected" "$logdir"
}
printf '%s\n' "$runtime" >"$logdir/tool-versions.log"
trap summary EXIT
matches() {
  local file="$1" input
  for input in $2; do case "$file" in "$input"|"$input"/*) return 0 ;; esac; done
  return 1
}
fingerprint() {
  local inputs="$1" key cache p
  key=$(printf '%s' "$inputs" | sha | cut -c1-16); cache="$logdir/.files-$key"
  if [ ! -f "$cache" ]; then
    for p in $inputs; do
      if [ -d "$p" ]; then find "$p" -type f -not -name '*.pyc' -not -path '*/__pycache__/*' -print0
      elif [ -f "$p" ]; then printf '%s\0' "$p"; fi
    done | LC_ALL=C sort -z | xargs -0 shasum -a 256 >"$cache"
  fi
  { printf '%s\n' "$runtime" "$*"; cat "$cache"; } | sha | cut -c1-24
}
step() {
  local name="$1" inputs="$2 $common"; shift 2
  all_inputs="$all_inputs $inputs"
  ((planning)) && return 0
  local file hit=0 dep slug log stamp fp="" rc start=$SECONDS
  if ((changed && !fallback)); then
    for file in "${changed_files[@]}"; do matches "$file" "$inputs" && hit=1; done
    ((hit)) || return 0
  fi
  selected=$((selected+1))
  if ((list)); then printf '%s <- %s\n' "$name" "$inputs"; return 0; fi
  for dep in ${REQUIRE:-$1}; do
    if ! command -v "$dep" >/dev/null 2>&1; then
      missing=$((missing+1)); echo "unavailable: $name requires $dep" >&2
      if [ -n "${CI:-}" ]; then status=1; ((keep)) || exit 1; fi
      return 0
    fi
  done
  slug=$(printf '%s' "$name" | tr -cs 'A-Za-z0-9\n' '-'); log="$logdir/$slug.log"; stamp="$stampdir/$slug"
  if [ "${NO_CACHE:-0}" = 0 ]; then
    fp=$(fingerprint "$inputs" "$@")
    if ((!force)) && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$fp" ]; then
      cached=$((cached+1))
      if [ -f "$stamp.log" ]; then cp "$stamp.log" "$log"; warned=$((warned+$(grep -c '^WARN' "$log"))); fi
      return 0
    fi
  fi
  if ((verbose)); then echo "== $name"; "$@" 2>&1 | tee "$log"; rc=${PIPESTATUS[0]}
  else "$@" >"$log" 2>&1; rc=$?; fi
  if [ -n "${SKIP_RC:-}" ] && [ "$rc" -eq "$SKIP_RC" ]; then
    missing=$((missing+1)); echo "unavailable: $name (${SKIP_WHY:-prerequisite unavailable}); log: $log" >&2
    if [ -n "${CI:-}" ]; then status=1; ((keep)) || exit 1; fi
    return 0
  fi
  if [ "$rc" -eq 0 ]; then
    passed=$((passed+1)); warned=$((warned+$(grep -c '^WARN' "$log")))
    if [ -n "$fp" ]; then cp "$log" "$stamp.log"; printf '%s' "$fp" >"$stamp"; fi
  else
    rm -f "$stamp"; status=1
    printf 'FAIL %s (exit %s, %ss); log: %s\nRepeat: ' "$name" "$rc" "$((SECONDS-start))" "$log" >&2
    [ "$1" = uv ] || printf 'uv run --locked --only-group checks ' >&2
    printf '%q ' "$@" >&2; echo >&2
    awk '!seen[$0]++' "$log" | tail -20 >&2
    ((keep)) || exit "$rc"
  fi
}
checks() {
  if in_group web; then
    REQUIRE=node step 'editor JavaScript syntax' 'components/webui/www/js' bash -c 'for f in components/webui/www/js/*.js; do node --check "$f" || exit 1; done'
    REQUIRE=node step 'editor unit tests' 'components/webui tests/webui main tools/editor_local CMakeLists.txt docs/widgets' bash -c 'node --test tests/webui/*.test.mjs'
    step 'tooling tests' 'tools scripts tests/tools components/webui tests/simulator main/layout' python3 -m unittest discover -s tests/tools -p 'test_*.py'
    SKIP_RC=2 SKIP_WHY='fontTools not installed' step 'font subsets' 'tools/generate_widget_fonts.py main/ui/fonts components/webui/www/styles.css' python3 tools/generate_widget_fonts.py --check
    step 'source file sizes' 'main components/webui tools scripts tests/host tests/webui tests/tools' python3 tools/check_source_sizes.py
    step 'simulator layout fields' 'main tests/simulator tools/check_simulator_fields.py' python3 tools/check_simulator_fields.py
    step 'test page cases' 'main/layout tools tests/simulator' python3 tools/test_layouts.py check
    step 'gallery freshness' "$sim_inputs docs/img/gallery tools/gallery_freshness.py" python3 tools/gallery_freshness.py
  fi
  if in_group web || in_group docs; then
    step 'HTML and translation tables' 'components/webui main tools scripts docs README.md AGENTS.md CLAUDE.md .github/workflows' python3 tools/check_web.py
    step 'documentation page sizes' 'docs tools/check_source_sizes.py' python3 tools/check_source_sizes.py --docs
  fi
  if ((!quick)); then
    if { [ -z "$group" ] && ((changed)); } || [ "$group" = docs ]; then
      NO_CACHE=1 step 'documentation build' 'docs zensical.toml README.md' uv run --locked --only-group docs zensical build --clean --strict
    fi
    local variant s steps='0'
    ((full)) && steps='0 1 2'
    for variant in panel4 paneljc tab5; do
      in_group "sim-$variant" || continue
      REQUIRE='cmake sdl2-config' step "simulator regressions $variant" "$sim_inputs" bash -c '
        for r in controls wallbox bambu history energy fonts notification price parqet; do
          scripts/simulator.sh "$0" --regression "$r" || exit 1
        done' "$variant"
      for s in $steps; do
        REQUIRE='cmake sdl2-config' step "widget layout $variant step $s" "$sim_inputs" python3 tools/check_widget_layout.py --variant "$variant" --text-step "$s"
      done
    done
    if in_group host; then
      for variant in panel4 paneljc; do
        REQUIRE=cc step "host tests $variant" 'main tests/host tools/test_host.sh managed_components/espressif__cjson' env VARIANT="$variant" tools/test_host.sh
      done
    fi
  fi
  if ((build)) && [ -z "$group" ]; then
    local variant
    for variant in panel4 panel10 paneljc paneljc-v2 tab5; do
      REQUIRE=idf.py NO_CACHE=1 step "build $variant" 'main components config third_party CMakeLists.txt dependencies.lock dependencies.lock.panel10 dependencies.lock.paneljc dependencies.lock.tab5 scripts/build.sh tools/apply_component_patches.py' bash -ec 'scripts/build.sh "$0" build; case "$0" in panel4|panel10) want=32MB;; *) want=16MB;; esac; head -1 "build/$0/flash_args" | grep -q -- "--flash-size $want"' "$variant"
    done
  fi
}
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}"
sim_inputs='main tests/simulator scripts/simulator.sh tools/check_widget_layout.py tools/check_readability.py tools/ui_styles.py tools/generate_simulator_gallery.py tools/gallery_content.py tools/widget_cases.py tools/test_layout_cases.json tools/test_layout_entities_demo.json'
changed_files=()
# Register inputs first without executing checks; reuse these exact lists for selection.
checks
planning=0
if ((changed)); then
  git diff --name-only --no-renames -z HEAD >"$logdir/changed-files" || exit 2
  git ls-files --others --exclude-standard -z >>"$logdir/changed-files" || exit 2
  while IFS= read -r -d '' file; do
    changed_files+=("$file")
    if ! matches "$file" "$all_inputs"; then fallback=1; force=1; fi
  done <"$logdir/changed-files"
fi
checks
exit "$status"
