<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Working practices

Lessons from real devices.

## Devices and flashing

- Flash **one device at a time**; parallel OTA uploads or builds can hang a panel.
- Pass the image to `tools/ota_flash.sh` explicitly. Without one it takes the newest `build/*/` binary, which is the
  wrong variant right after another build (the panel refuses it, nothing is written).
- Opening the USB serial port can reset the board (`idf.py monitor`, pyserial). Read `GET /api/log` (last 32 KB) and
  `GET /api/version` (memory, `cpu.load_percent`, `uptime_s`) over HTTP first.
- Home Assistant shows the ESP32-C6's Wi-Fi MAC, not the P4 base MAC that `esptool` prints.
- A new value in `config/sdkconfig.defaults*` is not picked up by an existing `build/<variant>/sdkconfig`: delete the
  build directory once. `reconfigure build` embeds the real git hash; a plain `build` reports `<hash>-dirty`.
- After three crash resets in a row, each before 60 s of uptime, the panel boots once with the built-in layout
  (`safe_mode` in `GET /api/version`, banner in the editor); the stored layout is kept.
- The main task builds the layout and carries the LVGL event chain: keep large structs off its stack
  (`ui_widget_def_t` is 2.7 KB; `CONFIG_ESP_MAIN_TASK_STACK_SIZE` 8 KB, paneljc 10 KB). A "Stack protection fault in
  task main" at boot is this; decode with
  `riscv32-esp-elf-addr2line -pfiaC -e build/<variant>/betta-ha-panel.elf <addresses>`.

## Testing on a device

Host tests cover pure logic only; check UI and hardware on the device.

- `GET /api/screenshot.bmp` (`tools/screenshot.sh`) returns the framebuffer, popups included.
- `POST /api/navigate {"page":"<id>"}` switches the page without touch.
- `POST /api/ui/tap {"widget":"<id>"}` or `{"close":true}` opens and closes a read-only popup (weather, energy
  timeline, room, solar, car, graph; sensor, binary and contact with history). It needs the admin login and cannot
  switch or unlock anything.
- `GET /api/log` and `GET /api/version` give log, memory, task stacks (`?tasks=1`) and watchdog state.

- Page-switch timings are logged (`page <id> shown, refresh <n> ms`, `... frame done <n> ms after the switch`); compare
  them before and after a UI performance change.
- The editor autosaves to the connected panel. Back up `GET /api/layout` before an experiment and `PUT` it back.
- macOS has no `timeout`; scripts talking to a device need their own.

## Admin password

Optional (Settings → Backup & security). With it set, every request needs HTTP Basic auth (user `admin`) except
`GET /api/version`; tools read `BETTA_PASSWORD`. There is no reset short of erasing the settings storage over USB.
POST/PUT bodies must be `application/json` (`application/octet-stream` for `/api/ota/upload`). See
[Security](../SECURITY.md).

## Memory and heap

- C6 memory diagnostics (`net/c6_memory*`) configure ESP-Hosted's memory monitor from a low-priority worker
  under the Wi-Fi API mutex, after station setup or setup AP startup. `/api/version` only copies the cache.
  In ESP-Hosted 2.12.13 the config reply's `curr_total_heap_size` means **free** heap; only periodic events
  contain the C6 boot minimum. Internal 8-bit and DMA capabilities overlap. Reset generations invalidate old
  samples and in-flight replies; transport loss or an age over 180 seconds marks cached values stale.
  Incident hooks copy the latest sample; the worker retains it under the separate versioned NVS key
  `wifi_diag/c6_last`, at most once per five seconds, without changing the legacy Wi-Fi diagnostic blob.
  Before a network-watchdog restart, `c6_memory_retain_for_restart()` tags a fresh sample with the reboot cause
  (record version 2) and wakes the worker to write it at once; the watchdog runs on a PSRAM stack and must not write
  flash itself, so it waits at most one second for the worker.
  Ordinary minute reports never write flash. Runtime C6 support and reconnect behaviour need device trials;
  host tests and firmware builds alone do not establish them.

Internal RAM is scarce (Wi-Fi/SDIO buffers, TLS, task stacks); keep large buffers in PSRAM.

- **LVGL allocates from PSRAM first** (`ui/lvgl_alloc.c`, `CONFIG_APP_LVGL_ALLOC_PSRAM_FIRST`, default for every
  variant). Without it LVGL held 146-174 KB of internal RAM. Measured on paneljc: free internal heap after boot
  129 → 321 KB, largest block 35 → 164 KB, minimum under `tools/stress_test.py` 76 → 184 KB; panel4 165 → 329 KB.
  `panel10` has no own measurement: on its first run read `lvgl_mem` and the frame rate of a busy page.
- **Discovery and other user-triggered Home Assistant work** is gated by free and largest-block thresholds
  (`ha_client.c`, per variant in `app_config.h`). A threshold above what a fragmented heap offers makes the room
  import look hung. Check against the `int_free`/`int_largest` of the "Entity discovery waiting" log lines on the real
  board (paneljc: about 100 KB, largest block exactly 32 KB).
- **Size task stacks from measurements:** `GET /api/version?tasks=1` lists each task's stack (priority, internal or
  not, size, least ever free). Exercise the panel first (page switches, popups, editor, discovery); the low-water mark
  only covers what ran since boot. IDF-created tasks report `stack_size` 0.
- **Stacks that live in PSRAM** (`xTaskCreateWithCaps`, must end with `vTaskDeleteWithCaps(NULL)`): `ha_cover`,
  `ha_energy_tl`, `ha_history` (10 KB: a 5,000-value day left only 2 KB at 8 KB) and the MQTT client
  (`CONFIG_MQTT_TASK_STACK_ON_EXTERNAL_MEMORY`). Current sizes: `ui_runtime` 16 KB, `taskLVGL` 20 KB,
  `websocket_task` 8 KB (TLS margin), `httpd` 14 KB; `GET /api/screenshot.bmp` with a popup open needs 11 KB of it.
  The websocket client has no PSRAM stack option.
- **Hidden pages stay built** and receive every entity update. Work that only matters on screen belongs in the
  widget's visibility hook, not in a refresh on page switch.
  Bambu/EVCC cards and Sonos favorites therefore skip the deferred state replay on show. On the original
  paneljc on 2026-10-16, the previous replay took 297–301 ms on a page with one
  printer and two wallboxes (first finished frame: 547–566 ms, two switches).
  EVCC still refreshes its session history on show and closes popups on hide;
  hiding it does not render the card again. The simulator exercises hidden
  sensor changes and reopening through the production widget factory.
- **Layout limits:** 32 KB JSON (`APP_LAYOUT_MAX_JSON_LEN`, `layout_max_bytes` in `/api/version`) and 224 widgets
  (`APP_MAX_WIDGETS_TOTAL`) on every board. Build time is linear in the widget count (4-5 ms each on paneljc; 224
  widgets in 3.6 s) because `ui_runtime_apply_all_states()` makes one pass per widget; the earlier per-entity pass was
  quadratic and took 25 s, which tripped the 60 s task watchdog. RAM was never the limit; the page limit comes from the
  tab width (see [Checks and device tests](testing.md#bottom-navigation-full-and-compact-tabs)).

## Before building a feature

- **Budget first.** Name the resources the feature uses (largest free internal block, task stack, heavy-request gate,
  display lock, time per UI cycle) and read the free amount from the device, not from the documentation.
- **Measure in the running state.** Boot values are optimistic: the largest internal block drops to about 32-36 KB
  after hours with the camera, discovery and popups. Use `int_free`, `int_largest`, `int_min` and
  `GET /api/version?tasks=1` after exercising the panel.
- **Put the riskiest assumption on hardware first.** If the risk is memory, timing or the display lock, a thin
  throwaway slice on the device comes before a long specification.
- **No unbounded wait under a lock.** Every new wait gets a deadline and a cancel path (see the PPA hang below).
- **Stacks from measurements,** in PSRAM where the task never touches flash. mbedTLS allocates from PSRAM
  (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`), so a TLS client costs little internal RAM, but its cost per session is not measured.
- **Check the existing convention** before leaving it, for example "hidden pages stay built" below.

## Watchdogs and crash evidence

- **Network watchdog** (`util/net_watchdog.c`): a task polls every 30 s and pings the gateway every 60 s. The pure
  decision function restarts after `NET_WD_LINK_DOWN_S` without link or `NET_WD_PING_DEAD_S` of silent gateway (only if
  it answered once and Home Assistant is not connected), never within `NET_WD_BOOT_GRACE_S` of boot, at most
  `NET_WD_MAX_RESTARTS` in a row (RTC counter, cleared after 10 minutes of working network). The restart shows as
  `net_watchdog` in the reboot history.
- **Reboot history** (`util/reboot_log.c`): after an unexplained restart read `GET /api/reboot-log` (or `last_reboot`
  in `/api/version`) before touching USB: it says whether the previous run ended in a panic, a watchdog, a power cut or a
  deliberate restart, how long it ran and how low its internal heap went. Every deliberate `esp_restart()` calls
  `reboot_log_mark(<cause>)` first (append-only causes in `util/reboot_log.h`).
- **UI stall watchdog** (`util/ui_watchdog.c`): the task watchdog cannot see a UI task that is blocked on a lock or
  driver (paneljc, 2026-10-04: frozen screen, working network, `could not lock display`, `Event queue busy`). Two
  heartbeats (an LVGL timer, the `ui_runtime` loop); silent for 90 s → restart-history entry `UI watchdog` and
  `abort()`, so the core dump holds every task's stack. Never within 120 s of boot, at most three in a row;
  `ui_watchdog` in `/api/version` shows the heartbeat ages. It heals the hang and keeps the evidence, not the cause.
  If a panel freezes before this runs, save `/api/log`, `/api/version?tasks=1` and `/api/state` first (the log holds
  only the last minutes), then recover with `esptool flash-id`.
- **Known cause (paneljc, 2026-10-05):** `taskLVGL` blocked in `ppa_do_operation` (90° rotation flush) waiting with
  `portMAX_DELAY` for a PPA interrupt, holding the display lock. The `esp_lvgl_port` patch in
  `tools/apply_component_patches.py` runs the rotation non-blocking and waits 1 s on a semaphore; on a timeout
  `lvgl_port_ppa_timeout_hook()` writes a core dump and restarts (at most three in a row). Why the PPA sometimes does
  not finish is not established (a candidate is the SRM tail-block calculation in `ppa_srm.c`, not yet shown on the
  device). The `paneljc` configuration therefore rotates on the CPU (`CONFIG_APP_DISPLAY_PPA` unset in
  `config/sdkconfig.defaults.paneljc`); `paneljc-v2` keeps the PPA.
  The shared adapter callback in `drivers/display_port.c` retains the same bounded transaction and crash hook.
- **Reading a core dump:** a heartbeat timeout aborts in `ui_watchdog`; read the other tasks' stacks
  (`lvgl`/`taskLVGL`, `ui_runtime`, `httpd`, `ha_client`, history and cover tasks). A PPA timeout aborts in `lvgl` (`taskLVGL` in older images);
  an illegal instruction at `panic_abort` can be deliberate. Save the report metadata from `/api/version` and
  `GET /api/coredump?id=<id>`, then
  `python -m esp_coredump info_corefile -t raw -c core.bin build/<variant>/<app>.elf` with ESP-IDF active, against
  the ELF of the firmware that ran. `tools/build.sh` keeps each successful build's ELF as
  `.local/elf/<variant>/<first 9 hex digits of its SHA256>.elf` (newest 12 per variant); the decoder's error names the
  SHA the dump needs. A rebuild of the same commit does not match, so flash only images built through `tools/build.sh`.
  The two-report archive, PPA operation metadata and RTC marker limitations are in [Crash dumps](../coredump.md).

- **Wallbox session timestamp (paneljc, 2026-10-12):** the saved dump matched ELF `51428e179` and showed
  `ha_evcc_sessions_result → finish → time → s_adjtime_lock → abort` in `ha_client`, after 119,990 s uptime.
  `finish()` read the clock inside `taskENTER_CRITICAL`; if another task held the libc time mutex, IDF could not yield
  and aborted. Internal RAM still had a 210 KiB minimum and a 168 KiB largest block. Read wall-clock timestamps before
  entering the spinlock and pass them into state updates, including request, completion, failure and cancel paths.
  The host test tracks critical-section depth and rejects clock reads while locked; this is independent of heap pressure.

## Wi-Fi and SDIO (paneljc, findings in order)

Reading the serial log can reset the board even with `dtr`/`rts` low; use `GET /api/log`, `GET /api/version` and
`tools/stress_test.py`.

- **Packet drops under load:** rapid page navigation plus Room/House discovery right after boot overflowed the
  ESP-Hosted TX/RX queues (`H_SDIO_DRV: mempool OOM`, `STA TX: mempool_alloc failed, dropping pkt`). Raising
  `CONFIG_ESP_HOSTED_SDIO_TX_Q_SIZE`/`_RX_Q_SIZE` from 20 to 26 (about 19 KB internal RAM,
  `config/sdkconfig.defaults.paneljc`) gave zero drops in a 200 s run. `paneljc` only; reproduce on `panel4`/`panel10`
  with `tools/stress_test.py` before copying.
- **Wedged link:** one paneljc ended up responsive on screen but without Wi-Fi for 5+ minutes, past every recovery
  step in `wifi_mgr.c`; only `esptool flash-id` helped. Hence `wifi_mgr_get_diag()`, `net/wifi_diag_store.c`, the Wi-Fi
  popup rows and `wifi_diag` in `/api/version` (disconnect reason, reconnect and hard-recover counts; the NVS copy
  `wifi_diag.prev_boot` survives a reset).
- **Editor-open freeze:** several concurrent connections for the editor's CSS/JS froze the panel with `wifi_diag` at zero.
  The bounded `esp_hosted_tx()` wait fixed that reproduction; a later freeze showed `mempool_alloc failed` with about
  21 KB internal RAM low-water (a 1,536-byte DMA allocation, transport-buffer pressure).
- **Resolved (2026-10-01):** LVGL in PSRAM, right-sized stacks and the SDIO clock removed the pressure; no recurrence.
  The network and HTTP watchdogs stay as a safety net.
- **SDIO clock 20 → 40 MHz** (2026-09-26): the 20 MHz came from ESPHome's profile, not a measurement. Lowest internal
  free under 90 s of combined load 29 → 41 KB, and 5.6 → 18 KB with two editors at once
  (`--no-navigate --no-discovery --webui-copies 2 --webui-concurrency 8`, 60 s); zero failed requests and zero
  `mempool_alloc failed`.

## Recorder statistics

Measured on Home Assistant 2026.9.4 (2026-10-07) and checked again on 2026.10.0 (2026-10-10): metadata and hourly
reply shapes, field names and the `units` conversion were unchanged. Re-measure after a major Home Assistant update with
raw WebSocket probes (`recorder/*`); the `ha_*` tools do not expose them, use the ha-mcp Python client
(`uv run --project ~/Projects/ha-mcp python <script>`, read-only commands).

- `recorder/get_statistics_metadata` takes a list of statistic IDs and answers `result` as a **list** of objects
  (`statistic_id`, `has_mean`, `mean_type`, `has_sum`, `statistics_unit_of_measurement`, `unit_class`). An ID without
  statistics is simply missing. A measurement sensor has `has_mean` and `mean_type` 1, a counter `has_sum` and no mean;
  `%` has the unit class `unitless`, so it never converts.
- `recorder/statistics_during_period` with `period: hour` returns `result` as a map by ID, rows with `start` and `end` in epoch
  milliseconds. A 168-hour window gives 167 rows (the current hour has none). Whole reply: mean, min and max 17.6 KB, mean
  only 11.4 KB, `change` only 10.6 KB. The `units` parameter converts (`{"energy": "Wh"}`).
- Consumption needs `change`: a counter's `sum` and `state` drift apart after resets (a gas meter showed `sum` 13817 against
  `state` 40). `period: day` starts at Home Assistant's midnight, not the panel's, so the panel sums hourly changes into its
  own local days.
- One reply is parsed completely before the callback (cJSON in PSRAM). The worker takes the shared heavy-request gate one
  operation at a time; the 10-inch internal heap minimum fell about 24 KB during Energy detail requests and recovered after each.
- The panel API cannot press popup buttons or Energy nodes (`POST /api/ui/tap` opens only tile popups): a device test of those
  needs a finger.
- The Energy dashboard's statistic IDs are already parsed by `ha_client.c` (`energy_stat_refs`, including the grid
  `flow_from`/`flow_to` arrays); `ha_client_energy_source_ids()` exposes them.

## Entity names from the registry

Checked on Home Assistant 2026.10.0 (2026-10-10, 3,402 display-registry entries, 583 devices): 2026.10 adds `np`
(`next_name_part`: `area`, `device` or absent) to `entity_registry/list_for_display` and `next_name_part` to the device
registry. The panel name (`ha_registry_name_parse()`/`ha_registry_name_resolve()`: device name plus entity name when
`hn` is set, `np` is `device` or the entity has no name) equals the state's `friendly_name` for 3,195 of 3,357 entities
with a state. The rest is explained: 52 entities whose name repeats the device name (Home Assistant shows "X X", the panel
strips the duplicate prefix on purpose), and 109 of one custom integration (`evcc_intg`) that publishes a `friendly_name`
without the device prefix its registry entry asks for. Entities with their own area (`np` = `area`) match.
This instance has no child devices (`parent_device_id`); `via_device_id` (115 devices) is not an area parent in Home Assistant,
and the panel does not follow it. A single device can no longer belong to several integrations (0 devices with more than one
config entry), so the 2026.8 split produced no duplicates in the editor's device list here.

## Display flashes

The 10-inch paneljc shows short white/cyan flashes without touch, every few minutes, sometimes three in a row (open, no underrun message captured).
A flash write disables the cache, and the DSI refresh interrupt is not cache-safe (`CONFIG_LCD_DSI_ISR_CACHE_SAFE` is off); HomeTiles traced blue flashes on the
same panel to exactly that. Every flash write logs `flash_trace: flash write: <what> <bytes> B, <ms> ms` (`util/flash_trace.h`: settings, layout, theme, translations, graph
history, Wi-Fi diagnostics). Match the wall-clock time of a visible flash against `GET /api/log`; keep the trace until the cause is known. The cache-safe option does not
work as is: the DSI driver then requires IRAM callbacks and an internal-RAM context, and `esp_lvgl_port` registers flash-resident callbacks that touch a PSRAM
display object, so `display_init()` fails on the first boot (tried 2026-10-07, rolled back).

## Touch contacts

Taps on the 10-inch paneljc sometimes needed a second try. Counters in `GET /api/version` ("touch", `drivers/touch_track.c`) separate the stages; read them after
a fixed series of taps (all values since boot): `contacts`/`confirmed`/`lost` (tracker), `frames`, `flagged` (weak-signal bit 0x4000 in the Y word),
`reads`/`empty`/`i2c_errors`/`late`/`max_gap_ms` (controller polls), and `ui_pressed`/`ui_released`/`ui_clicked`/`ui_scrolled`/`ui_long`/`ui_lost` (what LVGL made of them).

Findings (2026-10-18, one unit, taps on the bottom bar and the top chips):

- With the adapter's interrupt mode the controller was read only after an INT edge (about 150 reads in 90 s); 6 of 21 quick taps caused no read at all.
  Reading on every LVGL timer tick (no INT pin for the adapter) was worse, because the LVGL task wakes only as often as its timers ask (about 18 reads/s).
- The Y word carries a weak-signal bit (0x4000, for example raw Y 16408 = 0x4018 = 24); comparing the whole word with the raw range dropped such points. About
  2 to 4 % of the frames carry it.
- A scan task (5 ms) plus a queue (`touch_latch.c`) gives every tap to LVGL, press and release in order. Merging taps into one pending press (first attempt)
  lost 8 of 30 contacts when LVGL was busy with a page build (30 contacts, 22 presses); with the queue 20 contacts gave 18 presses in a short check.
- Every press LVGL saw became a click (`ui_clicked` equals `ui_pressed`, `ui_scrolled` 0), so scroll detection is not the cause.
- Not established: whether the missing 2 of 20 were finished taps dropped as older than 600 ms (UI busy) or very short contacts.

## JC8012P4A1 reference sources

ESPHome `mipi_dsi/models/guition.py` (init tables and timing), the `gsl3670` component and the
`esphome-libs/gsl3670-firmware` blob (touch), `jtenniswood/espcontrol` (V2 tables, V3 notes) and the BETTA-kruzio fork
(OV02C10 camera, `esp_cam_sensor`). "V3" means ESP32-P4 v3.x silicon, not another panel. Credit what you use in the file
header and `LICENSE` section 12.
