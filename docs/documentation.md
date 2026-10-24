<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Documentation site

The Markdown in `docs/` is the source for both repository browsing and the
[Zensical](https://zensical.org/) site. `zensical.toml` defines navigation and theme.
`docs/README.md` is the site home page. The repository README keeps a short overview
and one selected example; full setup and usage instructions belong here.
The user path is Home → Feature tour → Install and set up → task guides.
Each screenshot has one main home: setup, the relevant task guide or Appearance.
`examples.md` remains a short compatibility index for old links, without a second image collection.

The [release notes](release-notes.md) cover this fork only; the original project's notes (v0.8.2 and earlier) are in
[Upstream release notes](release-notes-upstream.md).

Gallery generators write Markdown and PNGs only. Zensical creates the HTML in
`site/`, including the optional visual comparisons under Development; no separate gallery HTML
is maintained in `docs/`. The CI workflow publishes that generated site to GitHub Pages.

`docs/stylesheets/extra.css` widens the desktop content area and lays out
comparisons responsively. `docs/javascripts/image-viewer.js` provides a local
image viewer (click to open, Close/Escape/backdrop to dismiss) and turns the widget
gallery tables into side-by-side style comparisons. Widget guides show one style,
with additional images only for distinct functional variants or popups. Small tiles
use 80% of their native pixel width; previews wider than 400 pixels get two style
columns. The images retain their aspect ratio and never exceed the available space.

Display device guides in `docs/hardware/` share the same order: an image at the beginning,
specifications, firmware/revisions, ports, validation, a link to the technical notes, manufacturer
references and related guides. The pin and schematic notes themselves live in
`docs/hardware/device-notes.md` (Development). CPU/memory, storage and peripheral evidence have
their own reference pages; display settings are in `docs/display.md`.
The overview contains one display-device comparison. Connector illustrations on
the device pages identify where to attach the flashing cable; a short sentence
uses the same label. Image sources and generation prompts are recorded in
`docs/img/hardware/README.md`.
Appearance choices and screenshots are in `docs/appearance.md`; detailed rendering
rules and readability measurements are in `docs/appearance-implementation.md`.
Editor alerts, graphical MQTT setup and sound stay in `docs/notifications.md`;
optional YAML/HTTP setup is in `docs/http-notifications.md`; event ownership,
rendering and audio internals are in `docs/notifications-implementation.md`.

## Writing rules

Keep setup and behavior in user guides; source paths, wiring and implementation details belong under Development.
Headings and table cells name their subject: no leading “The”, “Look at”, “What it does”, “Where to read”,
“Good to know”, “At a glance”, “I want to” or “Do you”. Avoid marketing, filler, card grids and long prose tables.
Describe current behavior and the upstream v0.8.2 comparison, not intermediate release-candidate chronology.
Keep prose near 250 lines; `tools/check_source_sizes.py --docs` fails above 300 unless explicitly exempt.
Upstream release history and the generated Widget library index retain their existing exceptions.
Match page titles and navigation labels. Existing README/index, widget-type and device filenames are stable-path
exceptions: do not rename their .html URLs solely to match a human label. Check navigation before moving a page;
`hardware/peripherals.md` belongs under Development. Device comparisons are ordered by display size.

Capture at the fixture's device geometry with the demonstrated extras enabled; compact tiles can hide an extra
although its entity is set. Preserve aspect ratios and relative pixel scale. Mark simulator examples separately
from dated device captures. Compare claims with upstream `420df764`; JC/Tab5 have no upstream hardware baseline.
Measurements state device, date and scope; verify versions against manifests, locks and build setup.

## Preview and build with uv

Install [uv](https://docs.astral.sh/uv/getting-started/installation/), then run from
the repository root:

```bash
uv run --locked --only-group docs zensical serve
```

Open `http://localhost:8000`. For the same validation as CI:

```bash
uv run --locked --only-group docs zensical build --clean --strict
```

The built pages use direct `.html` links (`use_directory_urls = false`), so
opening `site/index.html` from the filesystem navigates to the actual page files
instead of showing a directory listing. Search spans all documentation pages and
sections, including individual widget and display device guides. It also works over
`file://`: Zensical's supported offline mode bundles the index and uses the local
MIT-licensed `iframe-worker` 1.0.4 shim in `docs/javascripts/vendor/`. No CDN is
needed at runtime. Use the header's **Search** control (or Cmd/Ctrl+K); a result
opens its page/section and highlights matching text. `zensical serve` remains the
recommended preview for HTTP-dependent features such as repository statistics.

The `docs` dependency group pins Zensical in `pyproject.toml`; `uv.lock` also fixes
its transitive dependencies. No ESP-IDF environment is needed. `site/`, `.venv/`
and `.cache/` are local build output and are ignored by Git.

Use relative links between documentation pages so they work on GitHub and Pages.
Link files outside `docs/` to the fork's GitHub source URL. Markdown plans for open
future work live in `.local/plans/`, named `plan-<topic>-MM-DD.md` (without a year),
and reusable project skills in `.local/skills/`.
Put task logs, captures, helper scripts and temporary reports under
`/private/tmp/betta-<task>/`; remove completed plans and clean up task artifacts.
The whole `.local/` directory is ignored and sits outside the documentation source.
Hardware working notes, vendor metadata and capture metadata are excluded from the site. Brand attribution remains published because Contributing links to it.

## GitHub Pages CI

The [Documentation workflow](https://github.com/andre68723/BETTA-HA-PANEL/blob/main/.github/workflows/docs.yml)
builds with `uv` and runs Zensical's strict link validation on pull requests and
pushes to `main` that change documentation or its configuration. Manual runs are
also available. It uses the checked-in PNGs; screenshot tools and firmware builds
are not dependencies of the documentation job.

The build job has read-only repository permissions. A separate deploy job has
`pages: write` and `id-token: write`, publishes the Pages artifact and uses the
`github-pages` environment. Deployment runs only on `main` in
`andre68723/BETTA-HA-PANEL`, never on pull requests or in other forks.

For the first deployment, select **Settings → Pages → Build and deployment →
Source → GitHub Actions** in the fork. This is the prerequisite for the
[Zensical Pages workflow](https://zensical.org/docs/publish-your-site/).
After pushing the workflow and documentation, the site will be available at
[andre68723.github.io/BETTA-HA-PANEL](https://andre68723.github.io/BETTA-HA-PANEL/).
The local build does not publish the site.

## Refresh the example images

Install the optional screenshot group and Playwright's Chromium once:

```bash
uv run --locked --group screenshots playwright install chromium
```

Then render and capture the examples:

```bash
uv run --locked --group screenshots python tools/generate_docs_examples.py
```

The script builds the LVGL/SDL simulator (see [setup](simulator.md#setup)), checks
its rendered widget rectangles, writes PNGs to `docs/img/examples/`, and captures
the current editor at 1536 × 864 against a local synthetic API. It refuses
unexpected API requests and external browser requests. The fixtures reuse the
public gallery entities and the firmware's Dark palette. Review every image
before committing it.

Use `--first-light-only` for the matched single-light panel/editor tutorial.
Use `--panels-only`, `--editor-only`, `--appearance-only`, `--setup-only` or
`--topbar-only` to regenerate one set. Use `--weather-report-only` for the
Weather forecast guide's four 10-inch previews with synthetic English report text,
cropped from the real widget at 680 × 520 pixels;
use `--history-only` for the 24-hour temperature Graph (Recorder mode) and the
Binary sensor motion-activity popup. Graph fixtures can set `history_step_minutes`
(default 15) to span their selected window with synthetic samples.
Use `--inspector-only` for the inspector images of the widget guides (`INSPECTOR_KINDS`): the editor with the gallery
tile of each guide's main preview selected, at twice the pixel density.
Top-bar captures crop the real LVGL frame
to 60 pixels high at each display geometry, using the same Date + Stock demo
and additional Date + Weather + Stock examples for 5- and 10-inch displays.
The setup capture uses the firmware boot splash and current WebUI provisioning
screens with public demo credentials. It never resets or reconfigures a device.
The Appearance comparisons use the same gallery demo page, data and palette in
all four styles and pass the simulator's rectangle checks. An installed Chromium
browser can be selected with `--browser-executable <path>` instead of downloading
Playwright's browser. Simulator and browser examples are not hardware verification.

The optional Gallery compares all four styles; it is a Development reference, not a second widget manual.
It is generated separately:

```bash
python3 tools/generate_simulator_gallery.py
```

Only rerender it when its source freshness check reports a visual change. For gallery prose/layout changes
use `python3 tools/generate_simulator_gallery.py --markdown-only` (also with `--variant tab5|paneljc`):
this rewrites Markdown from existing images and leaves image hashes and the freshness stamp alone.
The individual widget guides take their text from `tools/widget_docs_content.py`, their images from the
gallery and selected documentation examples, and sizes and domains from the firmware's table. Every guide has
the same sections: summary (naming the domains) and preview, Entities, Requirements, Options, On the panel,
Variants, Size, Related guides.
Regenerate their Markdown without
a device or image rendering:

```bash
python3 tools/generate_widget_docs.py
```

When adding a type, add its entry to `tools/widget_docs_content.py` (the script fails while one is missing
or its summary does not name a domain), and the Widgets navigation group in `zensical.toml`.
For actual device captures, use the documented [capture workflow](development/testing.md#device-page-captures)
and keep the images and metadata in ignored local storage. Published examples use public
synthetic data and owned or licensed artwork; household names, presence, device values and
unlicensed album covers must not be imported into the documentation. Simulator examples
do not establish hardware behaviour.
