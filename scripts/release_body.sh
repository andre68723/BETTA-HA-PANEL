#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
#
# Prints the body of a GitHub release: what the images are, which hardware was tried and how to install. The changes of
# the version are in docs/release-notes.md (linked); the workflow appends GitHub's generated list of merged work.
#
#   scripts/release_body.sh <version> <commit>      e.g.  scripts/release_body.sh v0.9.2 1a2b3c4
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <version> <commit>" >&2
    exit 2
fi
version=$1
commit=$2
site=https://andre68723.github.io/BETTA-HA-PANEL

cat <<BODY
# BETTA HA Panel $version

The changes are listed in the [release notes]($site/release-notes.html); installation is described in
[Installation and first-run setup]($site/getting-started.html).

## Images

| Variant | Hardware | Status |
| --- | --- | --- |
| \`panel4\` | Waveshare ESP32-P4-WIFI6-Touch-LCD-4B | maintainer device |
| \`paneljc\` | Guition JC8012P4A1 "V1" | maintainer device |
| \`tab5\` | M5Stack Tab5 | maintainer device |
| \`panel10\` | Waveshare 10.1-inch | **compile-checked only, testers wanted** |
| \`paneljc-v2\` | Guition JC8012P4A1 V2 | **compile-checked only, testers wanted** |

Use \`*.factory.bin\` for a first USB installation and \`*.ota.bin\` for an update from the panel. Verify downloads with
\`SHA256SUMS\`.

All images were built by the release workflow from commit \`$commit\` with ESP-IDF 6.1.
BODY
