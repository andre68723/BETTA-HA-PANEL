<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Offline search dependency

`iframe-worker-shim.js` is the unmodified `shim/index.js` from
[`iframe-worker` 1.0.4](https://github.com/squidfunk/iframe-worker), by Martin Donath,
distributed under the [MIT license](iframe-worker-LICENSE.txt).

Source: `https://unpkg.com/iframe-worker@1.0.4/shim/index.js`.
Zensical's supported offline mode uses this worker shim to search pages opened
through `file://`. It is bundled locally to avoid a runtime CDN request.
