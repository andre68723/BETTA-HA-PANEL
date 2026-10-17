<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Components and patches

## Vendored camera component and the P4 silicon cap

`components/esp_cam_sensor` is Espressif's core plus the OV02C10 driver (JC8012P4A1) and the SC202CS/SC2356 drivers
(Tab5), cut down to those sensors and the DW9714 motor. `tools/vendor_cam_sensor.py <version>` stages a new registry core
and keeps both sensor directories and `project_include.cmake`; `esp_video` pins the matching line (2.3.x wants 2.3.*).
After a version change update the `esp_cam_sensor`, `esp_video`, `esp_ipa` and `esp_h264` entries in the variant lock
files, rebuild both camera variants and check each camera on its panel (`POST /api/camera/test`, the
`ambient_cam: luma ...` log line). Tab5 awaits its first device test.

Versions stop at `esp_video` 2.3.0, `esp_ipa` 2.2.0~1 and `esp_cam_sensor` 2.3.0 on purpose: the prebuilt `libesp_ipa.a`
of esp_ipa 2.3.0+ contains RISC-V Zba/Zbb instructions (`sh2add`, ...; `objdump` counts 0 up to 2.2.0, 163 in 2.3.0, 193
in 2.4.0) that P4 rev v1.x silicon does not execute. A trial with esp_video 2.5.0 crashed the camera task with an illegal
instruction 20 s after boot (`create_ipa_array`; the rollback bootloader restored the previous image). ESP-IDF treats P4
rev < 3.0 and ≥ 3.0 as mutually exclusive builds (`ESP32P4_SELECTS_REV_LESS_V3`), so the newer line needs a separate build
for v3.x boards. `MAX_LINE` in `tools/check_component_updates.py` holds the caps; a prebuilt library cannot be told to
avoid the instructions.

## Shared LVGL adapter

Every panel registers display and touch with `esp_lvgl_adapter` 0.7.3 through
`main/drivers/display_port.c`. Board files initialize hardware only. The shared
adapter task uses priority 20, core 1 and the existing internal-RAM stack; LVGL
allocations, two partial draw buffers and rotation scratch use PSRAM first.

The adapter's registration rotation stays zero. Its custom bitmap callback uses
the current `lv_display_get_rotation()` for both boot and runtime orientation,
keeping Tab5 automatic rotation and LVGL's native touch mapping together. Native
DPI framebuffer counts remain unchanged; no extra full-screen buffer is required.
`APP_DISPLAY_PPA` selects bounded PPA rotation or LVGL CPU rotation (original JC).
A PPA completion has a one-second deadline and retains crash evidence; uncertain
output is never submitted or reused after a timeout. The adapter's sleep handling
is disabled because BETTA owns brightness, wake and inactivity policy.

Some BSPs still depend on `esp_lvgl_port` for their own convenience functions.
It may appear in the resolved component list, but BETTA does not initialize its
task or register displays/touch through it. Panel10 and JC V2 share the new source
path but have no hardware qualification in this fork.

Device acceptance on 2026-10-13, source `650fd258`: panel4, original paneljc and
one ST7121/v1.3 Tab5 report adapter 0.7.3, valid OTA, live Home Assistant connection
and unchanged layouts/settings. The owner confirmed physical image orientation
and page navigation by touch on all three. Tab5 was mounted at 270 degrees;
this check does not qualify physical touch mapping in every automatic orientation.

## Waveshare LCD-4B BSP override

Panel4 uses BSP 3.0.1 from `third_party/waveshare__esp32_p4_wifi6_touch_lcd_4b`.
The official source and Apache-2.0 license are preserved; `BETTA.md` records the
registry archive hash and repository commit. Only the adapter manifest constraint
changes from `~0.6` to `0.7.3`, matching the shared runtime and LVGL 9.6.
Dependency solving runs before the configure patch hook, so this override belongs
in the manifest rather than a manual edit of `managed_components/`.

The profile explicitly selects 720 × 720 and two DPI framebuffers. The ST7703
constructor already enables IDF 6's DMA2D copy hook; do not enable it again in
the board initialization. BETTA retains GPIO23 touch reset with the 0x5D/0x14 probe;
the new BSP declares the reset pin NC. Display timing and the separate ES8311
chime path remain board-specific.

## Patches to fetched components

`managed_components/` is git-ignored and overwritten whenever the component manager refetches (manifest change, or a
build at a commit whose manifest differs from the lock). The small edits needed there live in
`tools/apply_component_patches.py`, never by hand:

- two esp_hosted SDIO fixes (byte-mode TX workaround, bounded `esp_hosted_tx()` queue wait instead of an infinite one),
- compatibility fixes for the older fetched Waveshare BSPs, including panel10,
- a bounded wait for the PPA screen rotation in `esp_lvgl_port` (see
  [UI stall watchdog](learnings.md#watchdogs-and-crash-evidence)).

The root `CMakeLists.txt` runs the script on every configure, right after `project()`. Check or apply outside a build:

```bash
tools/apply_component_patches.py --check   # what is applied, what is missing (touches nothing)
tools/apply_component_patches.py           # apply without a build
```

- A build fails with `FATAL_ERROR` naming the script when a patch's place is not found: a component version moved it;
  adjust the `PATCHES` list.
- **Do not build an old commit in this checkout.** `PATCHES` matches the current component layout; an old
  `dependencies.lock` can pull older versions and the hook then reports a false `MISSING`. Use a clone or worktree.
- **Cover requests** keep a separate cancellation ticket until their final LVGL callback completes
  (`ha/cover_request_lifetime.c`); destroying a widget cancels all its tickets and a new widget at the same heap address
  gets fresh ones. This fixed the Tab5 rotation trial of 2026-10-05: rebuilt media widgets hit a late callback into a
  deleted context (`tlsf_free` assertion in `mp_release_cover`, OTA rollback after 20 s; the panic PC read "Illegal
  instruction", the decoded core dump showed the double free).

### Shared lock files

`dependencies.lock` (root) is shared by `panel4` and `panel10`; `paneljc`/`paneljc-v2` use `dependencies.lock.paneljc`,
`tab5` uses `dependencies.lock.tab5` (`idf_build_set_property(DEPENDENCIES_LOCK ...)` in the root `CMakeLists.txt`).
Building one of the shared pair overwrites the root file with that variant's resolution and swaps `managed_components/`
(their manifests pull different Waveshare BSP packages). `lvgl`, `esp_lcd_touch`, `littlefs`, `esp_websocket_client`,
`mqtt`, `cjson`, `esp_lvgl_adapter` and `esp_lcd_touch_gt911` are pinned to the same version in every manifest, so only the BSP differs.

To get the current one back afterwards, back up first: `cp -R managed_components <backup>` and `cp dependencies.lock
<backup>/`; restore with `rsync -a --delete <backup>/ managed_components/` and copy the lock back.
`git checkout dependencies.lock` is safe only without uncommitted changes of its own (a manifest bump in progress).

## Keeping components current

`python3 tools/check_component_updates.py` lists each managed component with locked version, constraint and newest
registry version (see `tools/README.md`). Run it before touching `dependencies.lock*` or the manifests.
