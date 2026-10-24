#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
# Render the widget test pages in the simulator and open the overview in the browser.
#   tools/test_pages.sh [board ...] [--styles flat,...] [--text-steps 0,1,2] [--languages de,en]
# Options as for `tools/test_layouts.py view`; needs cmake and SDL2 like the simulator.
exec python3 "$(dirname "$0")/test_layouts.py" view "$@"
