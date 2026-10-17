# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Appearance style IDs for the gallery and layout tools.

The same IDs, order and rules as main/ui/theme/ui_style_registry.c; tools/check_web.py compares them
with the firmware, the editor constants and the settings options.
"""

STYLES = ("flat", "glow", "glass", "matte")
DEFAULT = "glow"
FALLBACK = "flat"
