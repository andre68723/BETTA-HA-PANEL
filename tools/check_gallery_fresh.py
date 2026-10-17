#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
"""Warn when inputs of the widget gallery changed after it was last generated (see gallery_freshness.py).

Exit status 0 unless --strict is given: a changed source file often changes no picture, so this is a hint to run
tools/generate_simulator_gallery.py, or `--stamp-only` when the change is not visible.
"""
import argparse
import sys

from gallery_freshness import changed_since_stamp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true", help="exit 1 when the gallery is out of date")
    args = parser.parse_args()
    result = changed_since_stamp()
    if result is None:
        print("gallery: no recorded state (docs/img/gallery/sources.json); run tools/generate_simulator_gallery.py")
        return 1 if args.strict else 0
    changed, added, removed = result
    if not (changed or added or removed):
        print("gallery: up to date with its sources")
        return 0
    names = changed + added + removed
    shown = ", ".join(names[:6]) + (f", and {len(names) - 6} more" if len(names) > 6 else "")
    print(f"WARN gallery: {len(names)} source file(s) changed since it was generated ({shown}); regenerate with "
          "tools/generate_simulator_gallery.py if the change is visible, or --stamp-only if it is not")
    return 1 if args.strict else 0


if __name__ == "__main__":
    sys.exit(main())
