#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Keep the documentation pages short enough to read.

A page that grows into an endless wall of text is split into sub-pages (see docs/documentation.md). This check warns
above WARN_LINES and fails above FAIL_LINES, with frozen ceilings for the few pages that are long on purpose. The
generated widget guides and the image galleries are exempt: they are long because of their pictures.
"""
import sys
from pathlib import Path

DOCS = Path(__file__).resolve().parent.parent / "docs"
WARN_LINES = 250
FAIL_LINES = 300
# Long on purpose; the ceiling only moves up after a deliberate decision.
CEILINGS = {
    "release-notes.md": 800,           # the fork's changes, grouped by topic
    "release-notes-upstream.md": 340,  # the original project's notes, kept unchanged
}
EXEMPT_DIRS = {"widgets", "img", "javascripts", "stylesheets"}


def pages():
    for path in sorted(DOCS.rglob("*.md")):
        rel = path.relative_to(DOCS)
        if rel.parts[0] in EXEMPT_DIRS or rel.name.startswith("gallery"):
            continue
        yield rel.as_posix(), len(path.read_text(encoding="utf-8").splitlines())


def main():
    failed = False
    count = 0
    for rel, lines in pages():
        count += 1
        ceiling = CEILINGS.get(rel, FAIL_LINES)
        if lines > ceiling:
            print(f"FAIL docs/{rel}: {lines} lines (ceiling {ceiling}); split it into sub-pages")
            failed = True
        elif lines > WARN_LINES and rel not in CEILINGS:
            print(f"WARN docs/{rel}: {lines} lines; consider splitting it")
    print(f"docs size: {'FAILED' if failed else 'ok'} ({count} pages)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
