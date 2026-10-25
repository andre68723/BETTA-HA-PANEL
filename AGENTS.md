<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Agent guide

BETTA HA Panel is ESP-IDF firmware for ESP32-P4 panels with a runtime Home Assistant dashboard/editor.
The fork baseline is upstream `420df764` (v0.8.2); `CMakeLists.txt` defines the firmware version.
Read only the references needed for the task. Documentation source and navigation: `docs/`, `zensical.toml`.

## Commands

Direct entry points live in `scripts/`; internal checks, generators and shared modules live in `tools/`.

| Task | Command |
| --- | --- |
| Daily checks | `uv run --locked --only-group checks scripts/check.sh --changed` |
| Explain selection / fast stage / complete check level | Add `--list` / `--quick` / `--full --force` |
| Widget overview, all sizes with logs and browser | `scripts/test_pages.sh panel4 tab5 paneljc` |
| Larger text layout | `python3 tools/check_widget_layout.py --variant tab5 --text-step 2` |
| One SDL render | `scripts/simulator.sh panel4 --fixture <file> --page <id> --style <style> --screenshot <file>` |
| Gallery / widget guides | `python3 tools/generate_simulator_gallery.py` / `python3 tools/generate_widget_docs.py` |
| Strict docs | `uv run --locked --only-group docs zensical build --clean --strict` |
| Documentation examples | `uv run --locked --group screenshots python tools/generate_docs_examples.py` |
| One firmware variant | `scripts/build.sh <variant> build` with ESP-IDF 6.1 active |
| Explicit all-five build gate | `scripts/check.sh --build` with ESP-IDF 6.1 active |

Only the existing check.sh stamp declarations map changed inputs to checks. Unmapped changes force the selected
level. Logs and warnings remain available on cached runs. No JSON interface or additional dispatcher. SDL checks use the shared fixed test clock;
`BETTA_SIM_NOW=real` opts out. See `docs/development/tools.md` and `docs/development/testing.md` for scope and limits.

## Read by task

| Change | Read first |
| --- | --- |
| Widget/layout/font/gallery | `docs/appearance.md`, `docs/appearance-implementation.md`, `docs/widget-anatomy.md`, `docs/simulator.md`, `docs/development/testing.md` |
| Editor/preview | `docs/editor-source.md`, `docs/appearance.md` |
| Board/memory/build | `docs/choose-display.md`, matching `docs/hardware/` guide, `docs/development/build.md`, `docs/development/components.md` |
| HA/MQTT/API | `docs/home-assistant.md`, relevant `main/ha/` or `main/api/` code; `docs/notifications.md` when relevant |
| User behavior | Matching user guide and current version in `docs/release-notes.md` |
| Docs/screenshots/Pages | `docs/documentation.md`, matching topic, `zensical.toml` |
| Device failures | `docs/coredump.md`, `docs/development/learnings.md`, board guide |

Subsystem ownership: `docs/development.md` and `docs/development/components.md`.
Measured device evidence and failure history: `docs/development/learnings.md`; do not treat old measurements as
current device qualification. Variants: `panel4`, `panel10`, `paneljc`, `paneljc-v2`, `tab5`.
Panel10 and paneljc-v2 are compile-checked only; host/SDL success does not establish hardware behavior.

## Change map

- New widget: [New widget types](docs/development/conventions.md#new-widget-types). The current-type consistency
  check covers factory lifecycle, editor creation/preview/sizes/names and guide content/group membership;
  registration presence does not prove numeric, CMake, service or visual semantics.
- Rendering: affected SDL layout checks and Gallery must pass; preserve render hashes before/after refactoring.
  The standard runner covers panel4/tab5/paneljc at text step 0; --full adds steps 1/2.
- Widget cases: `tools/test_layout_cases.json` is the shared case catalog. Gallery tags select previews;
  `gallery_only` cases do not enlarge packs. New types/validated enum values need cases; check.sh checks coverage.
  test_pages checks placement/content on all three sizes. Never load a test layout on a household panel as fallback:
  loading can delete panel-only graph history; see the test-layout guide.
- Text growth: calibrate with `tools/calibrate_text_caps.py --write` (Tab5 adaptive profile; --variant paneljc for
  wide boards). Keep the measured table in `main/ui/text_scale.c`. Nested `ui_fonts_tile_begin/end` contexts must
  remain nested; timer/event-driven relayout opens its own tile context.
- Fonts: semantic roles through `ui_font_role()`/`ui_text_set()`, line-height-based layout, APP_SYMBOL/APP_FONT_SYMBOL.
  No pixel-size call sites, bitmap font generation or LV_SYMBOL_* in widgets. Font/subset/emoji rules: `main/ui/fonts/README.md`.
- Controls: `ui_style_action_button()` and semantic colors; active/selected carries accent. Keep previewControlClass,
  state transitions and unavailable behavior in step; see Control semantics in the appearance implementation guide.
- Native dialogs: use `ui_popup_card_create()` and semantic `ui_popup_label_create()` roles; special colours remain explicit.
  Exercise opening/closing, content, theme contrast and bounds in SDL; see Popup contracts in the testing guide.
- Preview: keep tilePreviewMarkup and LVGL elements/order/geometry aligned. The preview-parity skill is a deliberate
  audit outside check.sh, not a pixel-equality or hardware gate.
- Editor source: update ordered WEBUI_JS_SOURCES/WEBUI_CSS_SOURCES and source index.html together; check_web compares
  them. Node tests derive that order and omit only main.js bootstrap. Text needs matching English/German keys.
- Inspector help: standalone bold headings with a blank line and explanation; section info buttons own long help.
  Warnings/status remain visible (warn/no-shorten); settings retain sentence summaries. Generated guide summaries equal English Inspector help; update German with them.
- Navigation: `ui/nav_layout.c`, APP_MAX_PAGES, editor maxPages/navIconIssue and /api/version limits stay in sync.
- User-visible changes: update the matching guide and the current release delta. Tool/CI cleanup belongs in developer docs.

## Implementation rules

- Only the UI task, or code holding the display lock, calls LVGL. Never wait indefinitely for hardware/locks under
  that lock. ESP-Hosted Wi-Fi RPCs use wifi_mgr serialization. Deliberate esp_restart calls mark the reboot cause first.
- Internal RAM and task stacks are constrained. Keep large buffers in PSRAM, large structs off main task stacks;
  measure stack low-water after exercising the workload. Only tasks that never touch flash may use PSRAM stacks
  (xTaskCreateWithCaps); end them with vTaskDeleteWithCaps(NULL). LVGL allocates from PSRAM first on every variant.
  Keep the HTTP task at 14 KiB for screenshots. Follow the memory/watchdog rules in the learnings reference.
- Preserve board-specific invariants in the hardware/component references, including GPIO23/reset and GT911 probe,
  single ST7703 DMA2D enable, current LVGL rotation/touch pairing and CPU/PPA profile distinctions.
- Build through scripts/build.sh, never bare idf.py. Variant manifests/locks are separate; managed_components and
  generated main/idf_component.yml are not primary sources. Build variants serially because dependencies are shared.
- Source modules should stay focused; review above 800 lines. 2999 is the hard ceiling except existing frozen
  legacy limits in check_source_sizes.py. Generated build-only bundles are not handwritten modules.
- New controls need event -> model -> saved JSON -> reload tests, including disable/re-enable and retained values.
  The event-registration test alone is not semantic coverage. Keep editor Reset aligned with firmware defaults.
- Group settings by use/inactivity/night, one complete reset per page, headings instead of nested boxes; hide dependent controls without losing values.
  Automatic brightness needs an explicit preview. Transient device tests expire device-side even if the browser dies;
  slider edits must not extend deadlines. Test start/update/stop separately from real settings saves; label simulations.

## Documentation

Follow `docs/documentation.md` for writing, audience separation, navigation, device-guide structure and captures.
Keep instructions terse and current; no marketing/filler or release chronology in user/developer references.
Match titles/navigation; retain stable index/type/device paths as exceptions. Keep prose around 250 lines;
check_source_sizes.py --docs fails above 300 except historical/generated exemptions.
`site/` is output. Preserve direct .html navigation and offline search; test changed pages over HTTP and file://,
wide and narrow, including links/images/zoom/overflow. Preserve image aspect ratio and relative pixel scale.

Generated widget guides change through widget_docs_content.py/generate_widget_docs.py only; Gallery and captures use
real LVGL/SDL or the current editor with public synthetic data, matching geometry and enabled extras. Label simulator
examples separately from dated device captures. Hardware image sources/prompts live in docs/img/hardware/README.md.
Maintain affected generated guides/PNGs when refreshing assets. Compare fork claims with upstream `420df764`;
JC/Tab5 have no upstream hardware baseline. Label measurements by device/date/scope and verify versions in manifests/locks.

## Authorized work and checks

Read-only device diagnostics, matching-ELF decoding, local checks, firmware/documentation builds, generated local
artifacts and Safari checks with required cache/network access are standing owner-authorized. This includes local
file:// and localhost interactions, sizing and screenshots. Do not ask again for those operations. Run Zensical directly through locked uv without shell log redirection.
This does not authorize flashing, device resets, push, retained crash deletion or HA changes.
Task-specific restrictions and requested device scope take precedence; one-device work does not authorize other builds.

Start with the smallest meaningful affected check. Preserve green results; repeat only for changed inputs, failures
or unresolved concerns. Editor JS: `node --test tests/webui/*.test.mjs`, `python3 tools/check_web.py`; docs-only changes need no firmware build. Batch edits before
build/flash, reconfigure for changed config/dependencies/release metadata, package finished images without rebuilding.
If Python changes, reuse the existing IDF environment and verify idf.py --version before installing another one.
Search focused sections/logs; batch independent reads/checks. Record exits, totals, relevant failure context and image source stamp; task logs go under /private/tmp/betta-<task>/.

Safari is preferred for interactive checks. Use text extraction/batched actions; screenshots for visual appearance.
Set the visible window to 90% of the available screen and verify its actual size separately from emulated viewport.
Check controls against their own group bounds with optional settings enabled, not just document overflow.

## Device evidence

Identify the board before USB/serial; opening serial can reset it. If HTTP works, collect version/tasks, log,
reboot-log and each retained coredump by SHA256 ID before resetting. Match elf_sha256 to .local/elf/<variant>,
not the current build. Decode deliberate abort cause/backtrace before inferring corruption. Preserve retained evidence.
The raw coredump partition is erased only after verified archive import; keep store tests, per-report editor actions
and display warning counts aligned. RTC diagnostic limitations and the two-report archive are in docs/coredump.md.
Before risky layout tests back up GET /api/layout to ignored .claude/layout-backups/; device API limits/recovery are
in docs/development/testing.md and learnings.md. Recovery/reset/restore requires the corresponding owner instruction.

After an explicitly authorized flash, verify running source stamp, OTA validation, connection and changed behavior;
reachability/upload alone can be a rollback. /api/screenshot.bmp is an LVGL snapshot, not physical framebuffer/touch proof.
Missing startup text in a bounded log is not proof of failed initialization. Backlight software values are not optical measurements.
Do not add routine stress tests; the owner declined the 10-inch stress test. A hardware claim needs board-specific evidence.

## Repository rules

Read git status before edits; preserve unrelated tracked/untracked work. Keep local plans/build artifacts out of commits.
Retain SPDX headers/third-party attribution (LICENSE section12). Code/commits/comments/docs are English; maintainer conversation German.
Use andre68723 for fork authorship; one logical change per commit; when commits are separately authorized, use Conventional Commits and an English rationale.
With shared checkouts, coordinate path/index ownership and prefer separate worktrees; explicit git add paths, inspect staged diff, git commit --only owned paths.
Never unstage or rewrite others' work. Another agent can take staged files, so verify the resulting commit's file list.

Only open Markdown plans belong in .local/plans, named plan-<topic>-MM-DD.md (no year); remove completed plans
with owner approval. Clean task logs/captures/helpers/reports from task tmp when finished. Preserve the owner's
linked Studio studies until that design decision. Read the relevant skill before peer/hardware/publication/plan work;
use .local/skills/{peer-project-review,hardware-diagrams,publication-review,plan-maintenance}/SKILL.md.
Reusable workflows live there, not in plan prompts. Local data is ignored and is not a cleanup deletion target.

Do not push or flash without owner instruction; never modify backup* branches. origin is the owner's repository,
betta-backup is private CI; do not treat a firmware build as deploy permission. Use the configured ha-mcp only for HA;
keep access read-only unless asked to change it, and never print/store its token. Review narrowly; do not infer extra scope.
