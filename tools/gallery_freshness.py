# SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
"""Which source files the widget gallery depends on, and whether they changed since it was generated.

The gallery (docs/gallery.md, docs/img/gallery/) is rendered by the SDL simulator from the widgets' production sources.
generate_simulator_gallery.py records a hash of every input in docs/img/gallery/sources.json; gallery_freshness.py
(part of scripts/check.sh) lists the inputs that changed since. A changed input does not always change a picture, so this
only warns; `generate_simulator_gallery.py --stamp-only` records the new state without rendering.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCES_JSON = REPO / "docs/img/gallery/sources.json"


def source_files():
    """Repository-relative paths of everything the gallery is rendered from."""
    files = set()
    cmake = (REPO / "tests/simulator/CMakeLists.txt").read_text(encoding="utf-8")
    for match in re.finditer(r"\$\{REPO_ROOT\}/([^\s\"\)]+\.(?:c|h|cpp))", cmake):
        files.add(match.group(1))
    for pattern in ("main/ui/**/*.c", "main/ui/**/*.h", "tests/simulator/*.c", "tests/simulator/*.h",
                    "tests/simulator/CMakeLists.txt", "tests/simulator/stubs/**/*"):
        files.update(p.relative_to(REPO).as_posix() for p in REPO.glob(pattern) if p.is_file())
    files.update(["main/ha/ha_evcc_sessions.h", "main/layout/evcc_slots.h", "tests/simulator/gallery_data.json", "tests/simulator/wallbox_disconnected.json", "tests/simulator/wallbox_sessions.json", "tools/gallery_content.py",
                  "tools/generate_simulator_gallery.py", "tools/widget_cases.py", "tools/test_layout_cases.json", "tools/ui_styles.py", "main/layout/widget_types.c",
                  "main/settings/i18n_store.c"])
    return sorted(f for f in files if (REPO / f).is_file())


def source_hashes():
    return {f: hashlib.sha256((REPO / f).read_bytes()).hexdigest() for f in source_files()}


def write_stamp():
    SOURCES_JSON.parent.mkdir(parents=True, exist_ok=True)
    SOURCES_JSON.write_text(json.dumps(source_hashes(), indent=1, sort_keys=True) + "\n", encoding="utf-8")


def changed_since_stamp():
    """(changed, added, removed) file lists against the recorded state; None when nothing was recorded yet."""
    if not SOURCES_JSON.is_file():
        return None
    recorded = json.loads(SOURCES_JSON.read_text(encoding="utf-8"))
    current = source_hashes()
    changed = sorted(f for f in current if f in recorded and current[f] != recorded[f])
    added = sorted(f for f in current if f not in recorded)
    removed = sorted(f for f in recorded if f not in current)
    return changed, added, removed


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
