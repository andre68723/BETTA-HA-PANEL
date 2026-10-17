#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Warn on oversized handwritten sources; exclude generated font arrays."""
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIRS = ("main", "components/webui", "tools", "tests/host", "tests/webui")
SOURCE_SUFFIXES = {".c", ".h", ".js", ".mjs", ".py", ".css", ".html"}
WARN_LINES = 800
MAX_LINES = 2999
# Existing oversized files need cohesive refactors, not a mechanical split in
# an unrelated change. Their current size is frozen; every other source must
# stay below 3,000 lines. Generated fonts and vendored touch code are excluded.
LEGACY_CEILINGS = {
    "main/ha/ha_client.c": 7799,
}


def main() -> int:
    largest = (0, "")
    oversized = []
    warnings = []
    files = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", *SOURCE_DIRS],
        cwd=ROOT, capture_output=True, check=True,
    ).stdout.split(b"\0")
    for raw_name in files:
        if not raw_name:
            continue
        name = raw_name.decode("utf-8")
        path = ROOT / name
        if name.startswith(("main/ui/fonts/", "main/drivers/gsl3680/")):
            continue  # generated bitmap arrays or vendored touch code
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        lines = len(path.read_bytes().splitlines())
        if lines > largest[0]:
            largest = (lines, name)
        ceiling = LEGACY_CEILINGS.get(name, MAX_LINES)
        if lines > ceiling:
            oversized.append((lines, ceiling, name))
        elif lines > WARN_LINES:
            warnings.append((lines, name))
    for lines, ceiling, name in sorted(oversized, reverse=True):
        print(f"FAIL {name}: {lines} lines (ceiling {ceiling})")
    for lines, name in sorted(warnings, reverse=True)[:5]:
        print(f"WARN {name}: {lines} lines; consider a responsibility-based split")
    print(f"source size: {'FAILED' if oversized else 'ok'} "
          f"({len(warnings)} over {WARN_LINES} lines; largest {largest[1]}: {largest[0]} lines)")
    return 1 if oversized else 0


if __name__ == "__main__":
    raise SystemExit(main())
