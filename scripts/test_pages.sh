#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
# Build widget test pages, log the run and open the checked overview in the browser.
#   scripts/test_pages.sh [board ...] [--styles flat,...] [--text-steps 0,1,2] [--languages de,en]
# Defaults: all three sizes, four styles, German, text step 0; --no-open only generates files.
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
log_dir="${TMPDIR:-/tmp}/betta-test-pages"
mkdir -p "$log_dir"
printf 'Generating simulator test pages; the browser opens after rendering and checks (--no-open disables it).\nLog: %s\n' "$log_dir/simulator.log"
python3 -u "$script_dir/../tools/test_layouts.py" view "$@" 2>&1 | tee "$log_dir/simulator.log"
