#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
# Start the local editor with synthetic data; all arguments go to the Python server.
# Once ready, press Enter in the terminal to open the default browser.
# Wide boards start with all widget types; --demo compact selects the small example.
#   scripts/editor_local.sh [--variant panel4|panel10|paneljc|paneljc-v2|tab5] [--ui classic|spike]
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$repo_root/tools/editor_local.py" "$@"
