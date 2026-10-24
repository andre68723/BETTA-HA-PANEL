<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk | Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->

# Release notes

Changes of **this fork**, compared with upstream v0.8.2. The release notes of the original project (v0.8.2 and earlier, by Cpt_Kirk) are kept unchanged in [Upstream release notes](release-notes-upstream.md).

## v0.9.1-rc1

Release candidate, not published. Changes since [v0.9.0](#v090).

### Widgets

| Widget | Status | What is new or changed |
| --- | --- | --- |
| [Energy price forecast](widgets/energy_price_forecast.md) | <span class="release-status release-status--new">New</span> | A day of electricity prices as bars (hourly or 15-minute) from the list attribute of a price sensor (EPEX Spot directly; Nord Pool and Tibber through a helper that stores the list as `data`): the price now, Today/Tomorrow, the day's minimum and maximum and the next cheap window. Raw EUR/cent units are converted before display; missing intervals stay gaps and all upcoming cheap windows remain available. Design inspired by the community's price timeline cards. |
| [Parqet portfolio](widgets/parqet_portfolio.md) | <span class="release-status release-status--new">New</span> | A Parqet portfolio from the Parqet Companion sensors: total value, gain, the unrealised return in percent (from the integration's own sensor), XIRR, dividends and the largest holdings as weight bars; the tile names the interval the integration is set to. An option hides amounts and shows percentages only. Tabs (Performance, Holdings), interval buttons (1D, 1W, 1M, YTD, MAX) and, in Holdings, the choice Largest, Top 5 or Flop 5 can be shown on the tile; the intervals other than MAX are asked from the integration. |
| [Stocks & ETFs](widgets/stock_quotes.md) | <span class="release-status release-status--new">New</span> | Up to eight quotes of Yahoo Finance or Easy Stock sensors, mixed, as a list (name, price, the last session's change in percent, amount or both, optionally the symbol) or as a chart per quote with one period selector (1D, 1W, 30D, YTD, 1Y). The integration of each sensor comes from Home Assistant's entity registry. A tap opens the details with market state, quote and update time and a chart whose source is named: Easy Stock's closes against the close before the period, or Home Assistant's records and means. The top bar's stock chip reads Yahoo Finance and Easy Stock sensors the same way. |
| [Sensor](widgets/sensor.md) | <span class="release-status release-status--changed">Changed</span> | The value follows the device class: timestamps in local time, calendar dates or relative dates, durations as text or clock, units hidden, as ° or scaled to kW/kWh, currency symbols with the entity's display precision (two decimals by default; yen without decimals), readable enum values, binary sensor state words and the decimal comma of the panel language. Binary sensors can also be selected and saved in the editor. Invalid calendar dates and malformed timestamps stay unformatted. |
| [Wallbox](widgets/wallbox.md) | <span class="release-status release-status--changed">Changed</span> | An optional header shows charging power against an adjustable maximum, or the site's energy flow as in the evcc app: one bar of PV, battery and grid with their values, brackets with icons naming the sources above and home, car, battery charge and export below. The SOC bar is taller. Loadpoint discovery assigns the site's power sensors from the same evcc instance automatically; the detected entity rows are read-only. The header appears only when the tile has room for all controls below it. |
| [Car](widgets/car_widget.md), [Gauge](widgets/gauge.md), [Contact](widgets/contact_tile.md), [Vacuum](widgets/vacuum_tile.md), [Solar forecast](widgets/solar_forecast.md) | <span class="release-status release-status--changed">Changed</span> | Car status and preconditioning have separate rows; gauge extras follow the actual text height and narrow values can put the unit on a second line. Contact and vacuum states fit the available width. Solar rows on 10-inch layouts follow the font height; weather-day alignment applies to vertical bars only. |

### Fixes

- **Large stock layouts:** history registrations grow with the layout instead of stopping at 24 quotes across all pages, leaving room for the detail popup too. Registry lookups discard removed entities so repeated layout edits cannot exhaust the provider cache. Subscription and registry limits match the 4,096-entity model; a valid 1,056-quote layout no longer loses its last sensors silently.
- **Clean Panel10 builds:** the Waveshare BSP receives its split ESP-IDF driver dependencies in the first configure, instead of requiring a second configure after patching.
- **Entity lists:** the editor loads every page of the panel's entity and state lists, including layouts with more than 128 entities. Saving a layout refreshes registry lookups for newly used entities, including quote providers and display precision.
- **History requests:** waiting requests from different callers no longer replace each other. Recorder and Easy Stock HTTP workers bound their gate waits and total body transfer time and can retry initialization after a failed task creation. Parqet requests handle timer allocation failures and cancel timed-out pending responses.
- **Price and quote data:** compact price grids reject out-of-range or fractional timestamps and invalid grid values; quote times are bounded before integer conversion. Reduced quote charts preserve endpoints and short recording gaps. Recorder statistics reject out-of-range timestamps and values before numeric conversion; a failed registry-buffer allocation keeps the lookup pending for retry. The top-bar history popup uses the same quote price and currency as the chip, including converted Yahoo Finance sensors.
- **Recovery:** a failed C6 incident write remains pending for retry. JC touch scanning starts only after a successful LVGL binding, so a failed initialization cannot leave a task reading a freed controller.
- **Long names and larger text:** light, empty, camera, car, Sonos and media player tiles keep their name on one line with an ellipsis instead of wrapping over their content. In the Matte style a wide light tile stacks icon, name and state by their line heights, with the colour button beside the switch. With larger text the lock's Open button grows to its word, a minimum-size sensor drops its name before the value, and a small car ring leaves out the charging power before it reaches the name. A radial gauge full with its extras leaves out the name; the to-do name stays on one line; a wide Matte slider stacks name and state by their line heights. The tall weather tile places its icon below a taller name, and an unavailable three-day forecast shows its notice inside the tile.
- **Printer step in German:** the Bambu printer shows its current step (for example "Hotend heizt") in the panel language instead of the English key.
- **Wallbox flow sensors:** choosing the energy flow in the Inspector fills the site sensors of the loadpoint at once.
- **Text size next to a price tile:** a tile laid out while an energy price tile refreshed took over that tile's text size, so a wallbox beside one pushed its charge level out of the card.
- **Wallbox redraws:** idle timers no longer redraw the whole card four times a second. Connection changes, command timeouts and completed sessions still refresh it; the first render applies the layout before fitting button and reading text.

### Panel

- **Translations:** common vacuum states and firmware-update progress follow German or English; energy, weather and setup labels use clearer wording.

- **Quote history:** a tap on the top bar stock item opens its price history, 30 days of daily means or 12 months of weekly means, from the Home Assistant statistics of the sensor. Missing periods stay gaps and dates follow elapsed time across clock changes.
- **Touch on the 10-inch JC panel:** a separate scan task polls the controller at a 5 ms interval and queues short taps for LVGL (`drivers/touch_latch.c`); the controller's weak-signal flag in the Y word is removed instead of dropping the point. Contact counters are in `GET /api/version` (`touch`). Measured on a 10-inch unit (2026-10-18), see `docs/development/learnings.md`, "Touch contacts".

### Editor

- **C6 memory:** Settings → Device → Device status shows the Wi-Fi coprocessor's free heap, boot minimum, largest internal block and sample age. Details include DMA memory, diagnostic errors and a retained sample from the last connection incident or network-watchdog restart. Reports are cached once a minute; stale or unsupported data is labelled.

- **German and English:** the colour-theme editor, time-zone choices and solar-preview days follow the selected language. Entity-first Date/Time and Alarm choices have labels, and the add-widget message fills its placeholders in both languages. Setup, update and notification hints match the current controls. Language files are separate, with checks for missing keys, placeholders, empty texts and duplicate firmware entries.

- **Device class:** search results and the entity picker name a sensor's device class; the inspector header shows it with the unit as chips for every tile bound to a sensor or binary sensor, and the inspector offers the sensor's unit and format choices of that class.
- **Inspector:** one order for every widget: a header with type, name (edited in place) and device class, then Entities, Display, Controls and Placement as foldable cards (empty ones left out; Placement folded). Short fields put the label beside the control. The Properties and Position & size tabs are gone. Entities are rows with the role and the Home Assistant name; a click opens the search, which can also remove an optional entity or use a typed ID, and an ID Home Assistant does not list is marked. The missing-entity banner can be dismissed; an incomplete search does not mark unseen entities as missing. A section's explanations open from an info icon behind its heading, with bold standalone headings and separate paragraphs; hidden optional groups are left out and warnings stay directly visible; the page inspector has the same form. The info icon of a widget shows the summary of its guide. **Panel follows** beside Text size shows the page chosen in the editor on the panel too. A rejected navigation request can be retried on the next page synchronization. The entity search keeps the current entity on top and starts with the entities of the same device; the Energy page's sensors and the weather and stock entities in the settings use the same entity rows.
- **Preview:** widget text roles, fixed 10-inch layouts, visible elements and language follow the panel more closely. Overlapping tiles are marked on the canvas. Wallbox headers use the same height limit as the panel and show missing power without invented sample values; short tiles explain the required height. The SOC bar matches the panel's solid green fill. Documentation image zoom also recovers when a lazy thumbnail has not loaded yet. Preview checks remain separate from LVGL simulator and device validation.

### Dependencies

- `esp_sccb_intf` 0.0.9 → 1.0.0 for the JC and Tab5 camera stack, the version a fresh configure resolves.
- `libpng` 1.6.58~2 → 1.6.59 in the JC and Tab5 lock files; the Panel4 lock remains at 1.6.58~2. Panel10 now has its own lock at 1.6.59, so a fresh build cannot rewrite Panel4's dependencies.

### Validation limits

This candidate is not a published release. Host tests, simulator renders and firmware builds do not establish device behaviour.
Panel10 and paneljc-v2 still need hardware testing. The renamed `energy_price_forecast` widget type replaces the unreleased
`price_forecast` type; development layouts using the old type need their widget types changed alongside the firmware update.

## v0.9.0

!!! info "Release scope"

    Changes below belong to **andre68723's fork**, compared with upstream commit
    `420df764`. Images for all five variants are on
    [GitHub Releases](https://github.com/andre68723/BETTA-HA-PANEL/releases).
    `panel10` and `paneljc-v2` images are compile-checked only and need testers. **Guition JC and Tab5 support are new in this fork**; they have no upstream v0.8.2
    baseline. Device measurements below validate fork builds, rather than compare
    them with upstream firmware.

### Overview

| Area | What changes in this fork |
| --- | --- |
| [Toolchain](#toolchain-and-dependencies) | **ESP-IDF 5.5.2 → 6.1**, **LVGL 9.5.0 → 9.6.0**, updated and pinned shared dependencies |
| [Display devices](choose-display.md) | Guition 10.1-inch and M5Stack Tab5 profiles join the Waveshare P4 devices; hardware validation is tracked per device |
| [Widgets](#widgets-new-and-changed) | **19 new types**, **11 updated types**, plus the existing Empty spacer |
| [Appearance](appearance.md) | Flat, Glow, Glass and Matte, independent color themes, larger text and consistent icons |
| [Pages and editor](editor.md) | Room/house imports, searchable entity picker, history options, live settings and more pages |
| [Notifications](notifications.md) | MQTT/HTTP alerts, camera doorbells, Home Assistant triggers and optional sound |
| [Updates and recovery](#updates-and-rollback) | Bootable OTA slots, rollback, retained crash evidence and optional automatic updates |
| [Documentation](documentation.md) | No-code setup through the first light, graphical MQTT notifications, focused widget guides and a searchable Zensical site; exhaustive style comparisons under Development |

### Known limits

!!! note "Device validation and remaining release gaps"

    - Fork development stress runs with two editors (`tools/stress_test.py
      --webui-copies 2`) passed on panel4 and paneljc without failed requests.
      Earlier fork development freezes and asset-loading failures are documented
      with their test dates in [Development](development/learnings.md).
    - The new flash layout is verified on panel4 and paneljc only. Graph history has a quota (a quarter of the filesystem, at most 1 MiB, plus a 512 KiB free reserve) and removes orphaned files; the quota is host-tested, but its effect on the 16 MiB boards (2.625 MiB filesystem; a full graph file is about 32 KiB) is not measured on a device, and panic dumps were
      measured idle (about 21-23 KiB), not under load.
    - Panel10 and paneljc-v2 have been compile-checked only. Tab5 ran on one device
      (ST7121, ESP32-P4 v1.3; first trial 2026-10-05). Panel4, paneljc and Tab5 were
      updated over the air to v0.9.0 on 2026-10-15. Current P4 builds target v1.x
      silicon, not v3.x. See [hardware](choose-display.md).
    - The Alarm tile was tried against a template alarm with and without a numeric
      code (2026-10-15); a wrong code and text codes have not been tried.
    - Some interactions still need device trials, including doorbell camera and
      unlock, factory reset/restore, car pre-conditioning and fan controls.

### Upgrading from upstream v0.8.2

!!! warning "Older flash layouts need one USB migration"

    OTA updates cannot install the new partition table or rollback bootloader.
    Export a full backup before migrating. A combined factory image erases the
    credential region; moving LittleFS also requires copying its files to preserve them.

| Starting point | Installation path |
| --- | --- |
| Firmware older than upstream v0.8.2 | Update to v0.8.2 first; older settings formats are no longer read |
| Waveshare on upstream v0.8.2 | USB flash with the matching new table/bootloader; factory images start at `0x0` |
| First install on Guition or Tab5 | Use the matching factory build; these profiles have no upstream v0.8.2 image |
| BETTA with the current table and bootloader | Matching app updates can use OTA |

Follow [Installation](getting-started.md) and [partition migration](development/build.md#changing-the-partition-table).

??? info "Device-specific migration notes and validation"

    - **Upgrade path:** update older upstream firmware to v0.8.2 first. This fork
      no longer reads settings formats from before v0.8.2.

    - **One-time panel4 USB flash:** a panel4 coming from v0.8.2 needs the new
      bootloader and partition table over USB. A combined factory image starts
      at offset `0x0`. OTA cannot replace the old table, whose second app slot is
      above the ESP32-P4 bootable range and which has no crash-dump partition.
      A factory flash erases credentials; the current table also moves LittleFS.
      Use the documented migration to preserve NVS and copy the filesystem, or
      restore a backup and enter Wi-Fi/Home Assistant credentials again.

    - **JC8012P4A1 first install:** there is no upstream v0.8.2 image for this
      board; use a factory image. Subsequent app updates use OTA.

    - **M5Stack Tab5 first install:** build the `tab5` variant and use its factory
      image; there is no inherited upstream image for this board. The firmware
      ran on one ST7121/ESP32-P4 v1.3 unit on 2026-10-05. Follow the remaining checks
      in the [Tab5 display device guide](hardware/tab5.md#validation-status-and-next-checks).

    - **Flash layout, USB update required:** every ESP32-P4 board uses two 6.5 MiB OTA slots, a 256 KiB coredump partition and
      LittleFS from 0xD60000 (2.625 MiB on 16 MiB devices, 18.625 MiB on 32 MiB devices).
      JC and Tab5 had no upstream partition table. NVS keeps its 24 KiB and address. A panel on another table, for example upstream v0.8.2, needs one USB flash with the new table
      (OTA cannot change it: an OTA update runs fine but leaves the old table and filesystem in place) and the filesystem moves, so its layout, settings, themes and graph history are formatted away unless
      copied over (`tools/lfs_repack.py`, see development.md "Changing the partition table"); a flash that skips the NVS range
      keeps the stored passwords and token, the combined `*.factory.bin` erases them. Hardware-checked on panel4 and paneljc
      using the NVS-preserving migration and filesystem copy; both retained their
      layout, settings and Home Assistant connection. On the panel4 and the paneljc an OTA upload into each slot was
      accepted and validated after 60 s (ota_0 to ota_1 and back), and a test image that panicked while pending verification was rolled back to the
      previous app by the bootloader, with a decodable panic dump left in the new coredump partition (20.6 KiB on the panel4, 22.4 KiB on the paneljc).
      `panel10` and `paneljc-v2` have only been compile-checked; one Tab5 device trial began on 2026-10-05.

### Hardware and platform

- **Display device support:** upstream's Waveshare `panel4` and `panel10` profiles
  are retained. **Guition JC8012P4A1 (`paneljc` / `paneljc-v2`) and M5Stack Tab5
  (`tab5`) are added by this fork.** Panel4 and the original paneljc have device
  trials, including one Tab5 on 2026-10-05; panel10 and paneljc-v2 are compile checked only. Builds use
  ESP-IDF 6.1 and matching ESP-Hosted/C6 firmware.

- **M5Stack Tab5:** 5-inch ESP32-P4 + ESP32-C6, with landscape and portrait
  layouts. BMI270 automatic rotation defaults on and can be disabled in Display
  settings. A one-second stable hold selects one of four orientations; flat or
  shaking positions retain the current view. The boot screen reads the mounting
  orientation before it appears, with the last stable view as fallback when the
  sensor is unavailable or the panel lies flat. Stable applied orientations are
  remembered without changing the fixed landscape mode. Portrait widgets can be positioned
  independently and long pages scroll. The editor refuses saves from a stale
  orientation. Solar forecast cards can use the full 720-pixel portrait width.
  Other boards retain fixed orientation. Open popups and display
  protection defer rotation. One ST7121/ESP32-P4 v1.3 unit ran the landscape
  dashboard, camera-brightness and quiet speaker tests on 2026-10-05; camera
  sensing still defaults off. New installations use 60% chime volume and enable
  night mode from 22:00 to 06:00 at 1% brightness; saved settings retain their
  values. Sound remains opt-in. See the [Tab5 guide](hardware/tab5.md) and
  [display settings](display.md#tab5-automatic-rotation).

- **Cover callbacks after dashboard rebuilds:** cancelling a media or camera
  widget invalidates every outstanding image request through final dispatch.
  Reusing a widget's heap address cannot revive a deleted consumer's callback.
  This fixes the double-free exposed by the first Tab5 rotation device trial.

- **Connectivity and touch:** Wi-Fi roaming and serialized C6 calls improve
  connection reliability; panel4 touch probes both GT911 I2C addresses.

??? info "Touch support for the new JC profiles"

    The fork's JC support uses Espressif's Apache-2.0 `esp_lcd_touch_gsl3680`
    driver with raw coordinates and host-tested nearest-point tracking in
    `drivers/touch_track.c`. Stray or reordered reports are held and logged as
    `touch jump held`. There is no two-finger zoom tracking. The current driver
    revision is compile checked; its orientation and touch behavior still need
    checking on a paneljc unit.

- **Display settings:** one page with headings, and one compact reset at the bottom
  that restores every Display group and the board-specific camera and rotation
  defaults. Sound and other settings pages stay unchanged.
    - New installations enable night mode (22:00–06:00, 1 %) on every board; saved
      preferences are kept.
    - Disabled night and presence features hide their controls but keep saved values.
      Sensitivity, camera testing and pixel movement use compact expandable options.
    - Use, inactivity and night brightness each have a 30-second test with live slider
      changes, a device-side timeout and Stop. A test leaves the night schedule alone,
      touch cancels it, and a wake touch cannot activate an underlying widget.
      Requests retry briefly while the display redraws, keeping slider and Stop in order.
    - Tab5 keeps the BSP's 5 kHz, 10-bit backlight PWM and logs duty, range and
      frequency readback. Known limit on the owner's ST7121 unit (2026-10-13):
      1–50 % showed little visible difference, 75 % was brighter and 0 % switched
      off. Alternative PWM profiles did not change this; further trials were stopped.
    - Tab5 uses Espressif BSP 1.3.2 with the display-initialization patch for a NULL
      configuration. Optional backlight diagnostics distinguish LEDC register values
      from the digital signal sampled at GPIO22.

- **Shared display integration:** Waveshare LCD-4B uses BSP 3.0.1 with a reproducible adapter-constraint override.
  Every board uses the shared `esp_lvgl_adapter` 0.7.3 runtime for display, touch and locking, with PSRAM partial
  buffers, runtime/boot rotation and the bounded PPA watchdog preserved. Original JC continues CPU rotation;
  Tab5 keeps BSP 1.3.2 and its unchanged backlight defaults. The 4-inch profile retains GPIO23 touch reset and
  both GT911 addresses. Some BSPs still pull in the old port as an unused runtime compatibility dependency.
  On 2026-10-13, source `650fd258` passed OTA/API checks on the local 4-inch, original JC and ST7121 Tab5;
  the owner confirmed physical orientation and touch navigation on all three (Tab5 mounted at 270 degrees).

- **Scheduled night dimming/off:** configure a local-time window, including
  across midnight, under **Settings → Display**. A night brightness of **0%**
  switches the backlight off; higher values dim it. Touch wakes the display
  for the normal dim timeout, then the night level returns. At the end of the
  window, normal brightness resumes. Camera presence wake during this window
  requires **Wake at night**. See [Display and brightness](display.md).

- **Display and presence:** runtime controls cover active and dimmed brightness,
  plus optional display protection that moves weather or cycles pages before
  switching the backlight off. The JC camera,
  and optionally the Tab5 camera, measure ambient light and presence for brightness and wake; they do not
  record or stream images.

- **Sound:** optional generated notification and doorbell sounds on panel4 and
  Tab5 have volume, night mute and a test control. JC audio is disabled in current builds. Existing notifications
  stay silent until sound is enabled. Panel4 completed 50 API-triggered sound
  tests without audio/I2C errors or a falling heap; acoustic quality, touch
  coexistence and broader acoustic checks remain pending. The owner heard a quiet Tab5 test chime on 2026-10-05.

- **Capacity and diagnostics:** ESP32-P4 boards cache up to 4096 Home Assistant
  entities. The editor and firmware expose board, component, memory, restart
  and network diagnostics;
  the optional betta animation can be disabled to save flash.

### Widgets: new and changed

**New** means a type added since upstream v0.8.2; **Changed** means an existing
upstream type was updated. Empty spacer keeps its original role. Shared appearance,
font and icon changes apply across the visible widgets.

| Widget | Status | What is new or changed |
| --- | --- | --- |
| [Alarm](widgets/alarm_tile.md) | <span class="release-status release-status--new">New</span> | `alarm_control_panel` state with arm/disarm from a tap, only the modes the entity supports, and a numeric code pad when it has a `code_format`. The code is never stored. Text codes are not supported yet. |
| [Bambu printer](widgets/bambu_printer.md) | <span class="release-status release-status--new">New</span> | Print status, nozzle and bed temperature, progress and remaining time of a Bambu Lab printer through the Home Assistant integration, with Pause/Resume, a read-only Status popup and Stop after a confirmation. Temperatures sit below the illustration, or beside it in wide cards, without covering the drawing. The editor finds the printer's entities by choosing the printer (any Home Assistant language); they are saved as explicit bindings. Simulator and host tested; the printer lookup is verified on the paneljc against the owner's Home Assistant, Pause/Resume/Stop not yet against a live job.  Optional camera binding adds **Status → Live camera** using the shared MJPEG view; live camera device validation remains open. |
| [Binary sensor](widgets/binary_tile.md) | <span class="release-status release-status--new">New</span> | Readable binary/event states and optional Recorder history on tap. |
| [Button](widgets/button.md) | <span class="release-status release-status--changed">Changed</span> | Shared appearance styles, readable labels and touch targets. |
| [Camera](widgets/camera_tile.md) | <span class="release-status release-status--new">New</span> | Snapshots plus opt-in MJPEG live picture (default off), full-screen view on tap, hidden/sleep pause and bounded buffers. Uses Home Assistant; no RTSP/H.264/H.265 or audio. Device validation remains open. |
| [Car](widgets/car_widget.md) | <span class="release-status release-status--new">New</span> | Battery, range and charging; optional pre-conditioning, lock and interior temperature. |
| [Climate](widgets/climate_tile.md) | <span class="release-status release-status--new">New</span> | Air-conditioning controls with supported HVAC, fan and swing modes. |
| [Contact](widgets/contact_tile.md) | <span class="release-status release-status--new">New</span> | Door/window views, readable state words and optional history. |
| [Cover](widgets/cover_tile.md) | <span class="release-status release-status--new">New</span> | Open, stop, close and position controls. |
| [Date/Time](widgets/datetime_tile.md) | <span class="release-status release-status--new">New</span> | Edit supported HA date/time entities in local time; invalid daylight-saving times are rejected. |
| [Empty spacer](widgets/empty_tile.md) | <span class="release-status release-status--unchanged">Unchanged</span> | Retains its layout-spacing role; no entity or action. |
| [Energy timeline](widgets/energy_timeline.md) | <span class="release-status release-status--new">New</span> | 24-hour flows, source shares, rings and a detail popup, independent of an Energy page. |
| [Fan](widgets/fan_tile.md) | <span class="release-status release-status--new">New</span> | Speed, oscillation and presets, with a larger rotor view. |
| [Gauge](widgets/gauge.md) | <span class="release-status release-status--new">New</span> | Linear or radial readings with units, HA display precision and optional extra values. |
| [Graph](widgets/graph.md) | <span class="release-status release-status--changed">Changed</span> | Local sampling or optional 24-hour Recorder history, with updated line/smoothed/bar presentation. |
| [Heating](widgets/heating_tile.md) | <span class="release-status release-status--changed">Changed</span> | Improved controls, state handling and compact/large layouts. |
| [Light](widgets/light_tile.md) | <span class="release-status release-status--changed">Changed</span> | Style-aware controls, state handling, an effect picker where supported and wider Matte layouts. Colour temperature is Kelvin only (Home Assistant 2026.3 removed mireds). |
| [Lock](widgets/lock_tile.md) | <span class="release-status release-status--new">New</span> | Lock/unlock/open where supported, optional confirmation and history on long press. |
| [Media player](widgets/media_player.md) | <span class="release-status release-status--changed">Changed</span> | Larger volume controls, shuffle/repeat where supported, radio layout and cover-art handling. |
| [Room card](widgets/room_tile.md) | <span class="release-status release-status--new">New</span> | HA area status/actions, plus an optional humidity, cover or media field. |
| [Scene](widgets/scene_tile.md) | <span class="release-status release-status--new">New</span> | Dedicated tile for scenes, scripts and momentary HA actions. |
| [Select](widgets/select_tile.md) | <span class="release-status release-status--new">New</span> | Choose a select/input-select option through its popup. |
| [Sensor](widgets/sensor.md) | <span class="release-status release-status--changed">Changed</span> | HA display precision, optional name/age and Recorder history. |
| [Slider](widgets/slider.md) | <span class="release-status release-status--changed">Changed</span> | Improved fitting, controls and wider Matte presentation. |
| [Solar forecast](widgets/solar_forecast.md) | <span class="release-status release-status--new">New</span> | Daily yield with optional weather-date alignment; today shows the weekday and its total, with the remaining part bright; editor bars keep their full height. |
| [Sonos favorites](widgets/sonos_favorites.md) | <span class="release-status release-status--new">New</span> | Play configured media favorites directly. |
| [Todo list](widgets/todo_list.md) | <span class="release-status release-status--changed">Changed</span> | Improved state handling, controls and layout. |
| [Vacuum](widgets/vacuum_tile.md) | <span class="release-status release-status--changed">Changed</span> | General vacuum tile replaces the Roborock-specific type and also serves Matter-based vacuums; legacy layout name remains compatible. A suction-level picker follows the vacuum's supported fan speeds. The Locate button is gone, the charge comes from a battery sensor (Home Assistant 2026.9 removed the vacuum's own battery value; the editor suggests the sensor of a new card), and with **History on tap** a long press opens the last 24 hours of activity. |
| [Wallbox](widgets/wallbox.md) | <span class="release-status release-status--new">New</span> | Minimal evcc loadpoint control through Home Assistant: direct Off, Smart/Fast mode, vehicle assignment and target SOC with Apply, plus status, power, charged energy, remaining time and a solid green horizontal SOC bar. Sessions lists completed charges; without a cable, the modes remain visible and the last session replaces SOC/target. The editor detects loadpoint bindings without a full registry dump and prefers the number charge limit. Commands wait for HA state confirmation; physical charger acceptance remains open. |
| [Weather (now)](widgets/weather_tile.md) | <span class="release-status release-status--changed">Changed</span> | Shared condition icons, extra readings and optional Show name. |
| [Weather forecast](widgets/weather_3day.md) | <span class="release-status release-status--changed">Changed</span> | Available highs/lows/rain, daily report details, compact fitting and optional Show name. |

See [Integrations](home-assistant.md) for entity requirements and [Widget anatomy](widget-anatomy.md) for sizing and touch guidance.

### Special page types

- **Room overview:** import Home Assistant areas in one step. Entity discovery inherits areas from parent devices and follows HA display names, including empty entity-name overrides. The page groups
  their room cards by floor and updates them from the area's entities. Room
  cards can show an extra humidity, cover or media field with a direct action
  where space allows.
- **Room page:** choose one Home Assistant area, then select which temperature,
  humidity, presence, door/window, climate, media, light, cover, fan and vacuum
  entities to include. A live preview shows the proposed page: readings at the
  top, climate and media on the left, lights and covers on the right. The
  result stays freely editable in the normal layout editor.
- **Energy dashboard:** the existing page gains clearer node values and units,
  style-aware circles and bidirectional grid/battery readings on panel4. The
  independent Energy timeline is a normal widget and needs no Energy page.
- **Page management:** pages have icons, can be reordered and duplicated, and
  duplicated widgets receive fresh IDs. The limit is 12 pages on 1280 px
  boards and 8 on the 4-inch panel (compact bottom tabs, see "More pages" below).
  Bambu, EVCC and Sonos Favorites cards keep their current readings when switching
  pages, avoiding redundant state replay and list rebuilding before the first frame.

- **More pages:** upstream v0.8.2 allows **5 pages on every variant**. The fork
  allows **8 on panel4** and **12 on 1280-pixel profiles**; the 12-page limit also
  applies to the newly added JC and Tab5 profiles. Tabs become compact when full
  labels no longer fit: the active page keeps its title, others show an icon or
  number. The editor warns about duplicate icons in the compact range.

- **Larger layouts:** the JSON limit rises from upstream's **16 KB to 32 KB**.
  Current profiles support up to **224 widgets**, with the editor reading the
  device's limits. These limits also define the new JC and Tab5 profiles.

### History and energy timeline

- **Optional history:** sensor, binary sensor and contact tiles open Recorder
  history on tap; locks use a long press to preserve their tap action. Numeric
  entities show a 24-hour chart and discrete entities a state timeline.
  Its Inspector checkbox is applied immediately and retained on save/reload.
  History loads on demand, handles unavailable periods and shortens oversized
  replies. It is off by default and requires Recorder data. A numeric
  `measurement` sensor with long-term statistics also offers **7 days**: 168
  hourly means with real gaps (tried on a panel4, 2026-10-07).
- **Seven-day Energy details:** Gas, Water and Solar open seven days of daily
  consumption, in a popup or a right-hand panel (tried on a 10-inch panel).
- **Graph data sources:** graphs retain local sampling by default. The new
  **History from Home Assistant** option fills a 24-hour graph from Recorder,
  including after a restart, without storing those samples on the panel. Up to
  eight graphs use this source, fetched sequentially and refreshed every
  15 minutes while their page is shown. The Inspector checkbox is applied
  immediately and retained on save/reload. Repeated refreshes keep Recorder
  samples separate from live values, preventing the loaded history from disappearing.
  Empty refreshes retain the last usable graph. See [history sources](history.md#choose-the-history-view).
- **Energy timeline:** Home Assistant long-term statistics supply 96
  quarter-hour bars for solar, grid and optional battery flows. Labeled rings,
  an optional Solcast forecast, a details popup and a nested-rings look show
  source shares; missing intervals remain gaps. Allocations inferred from
  aggregate counters are marked as estimates, while measured counters remain
  available in details. Visual inspiration:
  [aharren/ios-energy-widget](https://github.com/aharren/ios-energy-widget)
  (MIT); the implementation is new.

- **Camera tile:** a picture request that gets lost no longer leaves the tile on its placeholder; it asks again after 30 seconds.

### Shared widget improvements

??? info "Rendering, controls, precision and image handling"

    - **Weather:** updated upstream weather tiles use shared condition icons,
      available high/low/rain readings, optional names and daily report details.
      The new Solar forecast can align its dates with weather while keeping
      remaining yield separate.
    - **Values and units:** sensor and gauge readings use Home Assistant display
      precision when provided. Solar, car and gauge values include units;
      sufficiently large distance displays can convert metres to kilometers.
    - **State handling:** shared HA attribute compaction handles large payloads
      for existing and newly added widget types. Existing Vacuum, Light,
      Heating, Media and Todo widgets have updated controls and layout handling.
    - **Images:** cover art and the new Camera tile use area-average downscaling.
      Hardware-decoded JPEGs are reduced before display, and decoder padding is
      cropped. Device checks with real covers remain pending for this decoder path.
    - **Navigation and media controls:** bottom tabs have equal widths when the
      first title fits. Volume buttons, slider and touch targets have larger
      layouts for the retained Waveshare profiles; the new JC/Tab5 profiles use
      those shared controls. Current dimensions are 46 px buttons / 20 px track
      on 1280-pixel-wide 10-inch displays, and 52 / 22 px on panel4. Supported
      players expose shuffle/repeat; radio streams omit duration-dependent rows.
      Taller cards distribute space around the cover.
    - **History:** the newly added Recorder popup uses round value-axis ticks,
      units, grid lines and Min/Max with precision suited to the tick interval.
    - **Editor previews:** the new Solar forecast uses full-height bars.
      Selecting an existing page/widget keeps the saved/unsaved state unchanged.

### Appearance styles

Appearance is a new setting independent of the color theme. The four styles
apply to widgets, controls, energy nodes and the editor's layout preview:

| Style | Look |
| --- | --- |
| Flat | Crisp outlines and near-square cards/tracks, without depth effects |
| Glow | Rounded, raised cards and inset tracks with soft shadows |
| Glass | Translucent, frosted cards with a light edge and distinct page background |
| Matte | Opaque, muted surfaces, active-state colors and wider Light/Climate/Slider layouts |

The styles work with six built-in color themes, including the new **Pure Black**,
and custom themes. Value-coded fill gradients keep their meaning in every
style; text and symbols use the same fonts throughout. See
[appearance](appearance.md) and the four-style [widget gallery](gallery.md).

### Typography and readability

- **Fonts and icons:** all text, symbols, icons and emoji are drawn at run time by `lv_tiny_ttf` from four small embedded subsets (Poppins Medium, Poppins Bold for notification emphasis, Material Design Icons,
  Noto Emoji) instead of generated bitmap fonts: about 120 KB of font data (15 + 26 + 81 KB) replace roughly 490 KB of compiled bitmap fonts, and any size is available. Material Design Icons
  replace mixed built-in UI symbols; accents, supported units and emoji render consistently across widgets, popups and the boot screen. `lv_font_conv` is no longer used;
  `tools/generate_widget_fonts.py` subsets the sources with fontTools.

- **Text size setting:** Text size in the Layout header (Standard, Larger, Largest) applies and saves at once without a restart. Text asks for a role (small notes, names, values,
  prominent and large readings); the size follows the role, the board (the Tab5 starts one step above the Panel4, the 10-inch boards use the Panel4's sizes) and the user's step.
  A tile too small for larger text keeps a smaller one (calibrated from the layout fixtures), and `tools/check.sh` renders every variant at all three steps.

- **Notification emoji:** notification titles, messages and action labels show supported monochrome emoji (about 180 faces, reactions, weather, alerts, device states and arrows);
  joined emoji, flags, keycaps and unsupported emoji appear as `[Emoji]`, and the text always fits its buffer without splitting a character or an emoji sequence (a text that is too long is cut before the whole emoji). The notification card draws a selected set of 37 emoji in color (list in [Notifications](notifications.md#emoji)); every other emoji stays monochrome.

- **Subscript digits:** the shared text font includes `₀`–`₉`, so notifications and readings show chemical names such as `CO₂` and `PM₂.₅` correctly. Superscript units `m²` and `m³` remain supported.

- **Top-bar stock quote:** this fork adds the stock slot. On panel4 and Tab5 it
  keeps 20 px text, shortening the exchange suffix, currency and then symbol
  when space is tight. Other profiles can use a 16 px fallback.

??? info "Panel4 readability checks"

    Larger labels and values fit compact weather layouts, with condition icons
    moved into the day popup where needed. The maintained panel4 layout passed
    a 90-second navigation/discovery/editor workload without a reset or coredump.
    This is a functional check of a fork build; physical readability at 30 cm
    still needs assessment. See the [4-inch simulator example](pages.md#free-pages).

- **Top bar status chips:** Wi-Fi, Home Assistant and MQTT each have their own compact chip, and each opens its own detail card (Home Assistant with the firmware version, MQTT with broker and device id). The MQTT chip appears only while MQTT is running. On the Tab5 the touch area of each chip fills the whole 60 px bar.
  Detail cards grow for larger text, wrap long values and scroll when necessary, keeping Wi-Fi and firmware information readable at Largest.

- **Top bar items:** the 1280 px boards (10-inch and Tab5) show up to three items left of the clock (date, weather, stock quote); the 4-inch panel shows two. The third item is set in Settings > Top bar.

- **Top bar and navigation:** date, weather and stock slots sit beside live
  Wi-Fi/Home Assistant/MQTT status popups. The stock quote fits beside the date
  and clock on panel4; the Wi-Fi icon conveys connection and signal strength.
  Navigation follows editor page order, shortens long labels and uses larger
  popup close targets.

- **Layout fitting:** important readings and controls stay inside short or
  narrow cards, including weather, heating, sensor, gauge, car, slider, cover,
  media and climate. Long labels truncate, unavailable values are formatted,
  and value-coded buttons keep contrast across styles. The SDL
  [widget gallery](gallery.md) and [simulator](simulator.md) cover these layouts.
  States, readings and button words are no longer cut: the car shows lock and interior temperature on one line and a
  full-width pre-conditioning button below, the radial gauge stacks its extra readouts without overlap, a narrow
  vertical gauge puts the unit on a second line, contact and vacuum states shrink before they shorten, and the
  10-inch solar rows are as tall as their text.
- **German and English texts:** every built-in section follows the panel language.

- **Notification emphasis:** message bodies recognize paired `**bold**` and `__bold__` markers, including inline
  text, using a Poppins Bold subset with the same line metrics and Unicode/emoji support. Plain messages keep
  their existing label rendering. A bounded parser preserves unmatched/escaped markers and falls back to literal
  text at the 32-run limit; replacement, scrolling, tap dismissal and last-message recall retain their behavior.

### Web editor

- **Settings layout:** room and Home Assistant name inputs sit together; the
  firmware build timestamp appears once in the editor header. Network settings
  omit the redundant setup access-point instructions already shown during setup.
  Home Assistant setup offers a button to insert the suggested WebSocket URL
  into the editable field.
  Display settings group brightness, protection timings and night scheduling
  into compact rows that wrap on narrow screens.
  Alert settings use compact columns and include an offline gallery example
  of the camera/action notification card.
  Reloading keeps the current editor menu in the same browser tab. Clearing a
  crash report hides its actions, and status refreshes retain its timestamp.
  Firmware has its own menu; admin password controls are grouped with Backup & security.
  Language & time has a separate System page with collapsible custom translation
  upload. Menu labels expose MQTT and security; appearance and colour theme stay
  together before the top bar. Field widths match the content they hold.

- **Finding and adding widgets:** one searchable dialog combines widget types
  with compatible Home Assistant entities. The page menu groups blank, room,
  house and energy pages; an empty page offers immediate add actions.

- **Editing layouts:** the existing drag/resize editor gains a page inspector,
  room import, keyboard controls, reorder, widget duplication and list search
  for larger pages. A duplicated widget keeps its settings; a duplicated page gets fresh widget IDs.
  The widget row menu copies or moves a complete widget to another normal page, preserving every entity
  binding and option, checking widget/JSON limits and selecting its destination.
  The compact header groups title, status and Text size, with translated Reload/Save tooltips and a JSON import/export menu beside dark/light. Font-size details open on demand; narrow screens wrap the header, leaving the right column for the inspector.

- **Energy timeline colors:** Appearance → Colors offers six flow colors with swatches, HEX editing
  and reset to the original palette. Saved overrides apply to the tile, detail popup and editor preview.
- **Firmware settings:** board and RAM information appear once in Device status. Manual update checks
  send JSON, matching the API request guard and avoiding HTTP 415.
- **Inspector and previews:** previews show current entity values and units,
  the selected theme/style and Poppins text. A shared **Show name** option
  frees space on named tiles while old sensor settings remain compatible.
  History, forecast day count, solar alignment and gauge sizing have their
  own controls and hints.
  Properties and Position & size use separate tabs. Compact field labels, wrapping entity IDs and
  magnifying-glass Search buttons keep bindings readable. The Energy timeline folds optional battery
  and forecast bindings; Appearance opens automatically when it fits. Extended Inspector explanations
  use a short label and an info icon opening the complete text in a popup. Desktop page/widget lists
  scroll independently with matching row widths.
  Page title/navigation and optional Energy-page sensor groups open separately from the main source and view options.
  The fan preview uses three curved blades and a central hub for a clearer fan silhouette.
  Sample texts in the previews follow the editor language (several were
  German-only) and take state from the entity where there is one (contact,
  vacuum). The media player preview hides the cover and the heating preview
  shows target and status like the panel, so neither overflows or differs
  at the gallery sizes.
  The gauge looks (smooth, segmented, radial), graph modes, semi-arc heating, forecast weekdays and
  precipitation, the todo count and the energy timeline's rings (also nested) now follow the panel too.
  Previews scale with the tile (text, arcs, icons, switches) and drop what the panel drops at small sizes;
  sliders show their value in the bar, the button its state and a large switch, and contact, scene, lock and binary tiles use the panel's icons.
  The house page preview uses the panel's grid and room cards with the states of the room's entities.
  Overlapping tiles are outlined in red, also while one is dragged or resized, since they cover each other on the panel.
  The Inspector of a car with lock, interior temperature or pre-conditioning says when the tile is too small to show them,
  and the gauge's colour modes are translated.
  On the 10-inch boards the previews keep the panel's fixed rows and smaller texts; car, radial gauge and semi-arc
  heating are placed like on the panel, a car range in metres or miles reads in kilometres, and solar days use weekday names.
  Editor usability: Settings > Design and Top bar are separate pages; long settings hints show their first sentence with an info button;
  the right column stays reserved for the selected widget or page; below 1100 px the
  canvas rises (lists collapsed, page chips, sticky header actions, a multi-column settings navigation);
  the display settings line up on one grid; the delete buttons keep their distance from the duplicate buttons.

- **Safer changes:** unsaved edits mark Save and trigger a warning before
  Reload or leaving. Toolbar Undo/Redo and Ctrl/Cmd+Z restore layout edits, including page changes,
  geometry, Inspector fields and imports. A completed drag or continuous field edit is one step;
  history retains up to 50 steps within a 2 MiB UTF-8 budget. The deletion toast uses the same history.
  Save updates the dirty baseline without clearing history; edits during a pending save remain unsaved.
  Failed reloads preserve model, selection and history, and show an error rather than substituting defaults.
  A successful reload resets history; late responses cannot replace intervening edits.
  Imports report invalid structure, excessive size or off-canvas widgets;
  editor size limits match firmware. Saving keeps the current page selected. The badge compares the layout with the loaded or saved one, so selecting a page or a widget no longer counts as an edit.

- **Entity search:** every widget entity field has a Search button, including the
  Energy timeline's battery and forecast sensors, vehicle controls, vacuum battery,
  Bambu bindings, Room entities and advanced Wallbox bindings. Results respect
  each field's allowed domains; Room extras remain limited to the card's entities.
  Manual Energy-page sensors also offer Search.
- **Inspector consistency:** the new Gauge keeps extra entities through import and Save; sensor name
  and freshness switches apply immediately. The missing-entity banner clears
  after Save and its close button hides it until another entity goes missing, inspector hints fit narrower windows, and translated picker and
  page controls no longer expose raw translation keys. Sidebar controls have
  clearer accessibility labels.

- **Settings and backup:** settings are grouped by task and apply live where
  possible. Save restarts only when Wi-Fi, Home Assistant, MQTT, time server or
  room settings change. Backup/restore includes layout and customization
  without secrets, also the text size and the update options; a test compares
  the settings the panel reports with what the backup writes.

### Notifications and MQTT

- Notifications arrive through the HTTP API or MQTT and can include an action,
  camera image and optional sound. Alerts can also react directly to Home
  Assistant event or state entities. MQTT Discovery exposes the panel as a
  notify device with status and IP entities; see [notifications](notifications.md).
- A double tap on the clock shows the last HTTP/MQTT notification again (RAM only, no alerts, no sound or action).

### Updates and rollback

??? info "Device information and updates"

    **Device information and updates:** the editor shows logs, CPU and memory,
      restart reason, component versions and crash-dump controls. The panel checks
      this fork's GitHub releases for matching OTA images and offers a URL; users
      initiate installation separately. See [web editor](editor.md). When the URL is the release asset the check found,
      the panel compares the SHA-256 of the whole download with the digest GitHub reports and installs nothing on a
      mismatch (`sha256_verified` in `/api/ota/status`); a manually entered URL is not covered by that release-asset digest check.

??? info "Automatic updates (optional)"

    **Automatic updates (optional):** "Install updates automatically at night" (Settings > Firmware, off by default, with a warning when turned on). When nobody has used the panel or the editor for 10 minutes, between 03:00 and 04:00 local time (each panel at its own minute), with a good Wi-Fi signal and enough free memory, a panel whose bootloader supports rollback installs the newer release the daily check found, pre-releases included when the check includes them. It tries once a day. If the new firmware fails and the panel returns to the previous one, that version is skipped from then on and the panel shows a notice; the editor says what the last automatic update came to. Downloads are checked against GitHub's checksum, not a signature.

- **Version checks:** this fork's update checker compares complete version
  strings, so a newer pre-release or final release is eligible without the
  upload-time delay used by a plain version string. Pre-releases require
  **Include pre-releases**.

- **Chip-revision checks:** OTA uploads and downloads reject an image whose
  declared revision range excludes the device's ESP32-P4 revision before
  writing it. This does not make images for different display profiles interchangeable.
  Device settings show the actual chip revision and the firmware family
  (pre-v3/v1.x or v3.x), rather than a technical upper bound such as v1.99.

- **Rollback verified:** on a panel with the new bootloader a deliberately crashing image was installed over the air and the panel returned to the previous firmware by itself.

- **Bootloader shown in the editor:** Settings > Device says whether the panel's bootloader supports OTA rollback; its build date remains in the technical API data. An old one needs one USB flash (see development.md). Device memory labels distinguish internal RAM from external PSRAM.

??? info "OTA rollback protection"

    **OTA rollback protection:** a freshly installed image must run 60 s with the panel's own HTTP server answering
      and Wi-Fi (or the setup access point) up before it is marked valid; a reset earlier returns to the previous image
      (`ota_image_state` in `/api/version`). It needs a bootloader built with the option, so flash the bootloader over USB
      once per panel. NVS credentials survive `tools/build.sh <variant> -p <port> flash`;
      layout/settings files survive only if LittleFS stays in place or is migrated
      as described above. A panel with an
      older bootloader installs new images by OTA exactly as before, without rollback protection.

### Security and reliability

- **Wallbox sessions:** cache timestamps are read before entering the critical section. This prevents a panic when an evcc session refresh competes for the system clock's mutex.

- **Home Assistant authentication:** a rejected token stops repeated logins
  that could otherwise ban the panel; the editor reports the problem.
  Optional admin authentication, request validation, a Content-Security-Policy
  and scoped credential handling harden the local HTTP editor and API. The
  first-run setup access point now uses a random WPA2 passphrase of two words
  and two digits (`maple-river-42`) shown on the panel. See [Security](SECURITY.md).

An unreadable admin credential closes the protected routes with 503 instead of disabling
authentication. Every fix is in [Security fixes](security-fixes.md); firmware stays unsigned and the
editor uses plain HTTP ([Security](SECURITY.md)).

??? info "Settings, credentials and transport safeguards"

    - Read/allocation errors preserve settings and layouts; secret generations commit before endpoint changes. After connection/credential changes, downgrades require re-entry ([storage](hardware/storage.md)).
    - Layout, language, style, text size and palette reload under the display lock, with retry after contention or a full queue. Settings, themes, translations and local graph histories use atomic file replacement; graph storage has a quota and orphan cleanup.
    - Automatic OTA requires a committed attempt record; storage errors retain blocked releases. Uploads have idle/total deadlines, and validation aborts when the flash gate is unavailable.
    - HA history and image-fetch tasks publish LVGL results under the display
      lock. Home Assistant credentials are scoped to its server; external image
      hosts do not receive the token. PNG input is bounds checked, and HTTPS OTA
      downloads are not redirected onto plain HTTP.
    - Full 32-character SSIDs and raw 64-digit Wi-Fi keys are supported; setup
      uses the word-based WPA2 passphrase shown on the panel.
    - ESP32-C6 recovery waits for active scan/status RPCs before stopping its
      transport. The C6 updater limits repeat attempts to avoid reboot loops.

- **Recovery:** a crash-loop guard starts with the built-in layout after
  repeated crashes, leaving the saved layout intact. P4 boards can store and
  download a panic dump when their partition table includes that slot. LittleFS keeps the two newest reports,
  each with download, details and confirmed erase. A yellow top-bar triangle remains until all reports are erased.
  Reports retain their ELF hash and available build/time metadata; PPA recovery records geometry and buffers,
  and UI stalls carry a readable cause. The original `paneljc` uses CPU rotation to bypass recurring PPA timeouts;
  long-run stability remains under observation. Other profiles retain their existing rotation paths.
  Wi-Fi recovery records survive resets; bounded ESP-Hosted send waits avoid an indefinite queue
  stall. See [coredumps](coredump.md) and [hardware](choose-display.md).

??? info "Network watchdog"

    **Network watchdog:** the panel restarts itself (which also resets the ESP32-C6) when a configured network stays dead: no Wi-Fi link for 10 minutes, or a link that looks up while the gateway, which answered pings before, stays silent for 10 minutes with no Home Assistant connection to show traffic flows. Never in the first 5 minutes after boot, not in setup AP mode, and at most three times in a row, so a router outage cannot cause a reboot loop. A third rule restarts the panel when its own web server, probed over loopback, stops answering for 5 minutes (also in setup AP mode). Restarts are recorded as "network watchdog" or "HTTP watchdog" in the restart history; state in `net_watchdog` of `/api/version`.

- **UI stall watchdog:** when the screen stops updating while the rest of the panel still works (a blocked UI task), the panel writes a core dump of all tasks and restarts after 90 seconds, and records it as "UI watchdog" in the restart history. At most three times in a row.

- **Restart history:** the panel keeps the last eight restarts with why each happened (settings restart, firmware update, factory reset, coprocessor update, panic, watchdog, power-on, brownout), how long the run before it lasted and its lowest free internal RAM. A breadcrumb in RTC memory survives a software reset, a panic and a watchdog reset (not a power cut) and is written to NVS once per boot. Shown under Settings > Device, as `GET /api/reboot-log` and as `last_reboot` in `/api/version`.

### Compatibility changes

- **ESP32-S3 support removed:** the `panels3` variant (Guition ESP32-S3-4848S040, 480 x 480) is gone, with its drivers, partition table, defaults and the editor's compact (480 px) layout paths. There is no hardware for it, and its screen cannot hold the newer widgets such as the weather forecast. v0.8.2 images of it stay in the upstream history.

- **Spanish and French removed:** the built-in languages are English and German (the editor and the device); the Spanish and French tables were outdated and unmaintained. A panel set to `es` or `fr` shows English until another language is chosen, or a translation file uploaded for that code is used. Custom translations for any language still work.

- **Vacuum rooms and Locate:** the Roborock tile's room-selection popup (cleaning single rooms through `roborock.get_maps`) and its Locate button are not part of the general Vacuum tile. The map from an optional image entity is shown inside the tile.

- **Quick Setup:** the first-run starter wizard of the editor is replaced by the add-widget dialog and the room and house page imports.

- **Removed-profile cleanup:** the editor no longer labels the retired S3 board; its camera help and device label now cover Tab5. The gallery lists sizes for supported P4 panels only. The camera-component vendor tool preserves both the JC OV02C10 and Tab5 SC202CS drivers during updates.

### Under the hood

The sections below matter to developers and maintainers: toolchain, memory, test coverage and the flash layout.

#### Toolchain and dependencies

The fork moves the firmware build from **ESP-IDF 5.5.2 to 6.1** and the UI
from **LVGL 9.5.0 to 9.6.0**. This updates the framework and rendering stack,
alongside the individual components:

| Component | Upstream v0.8.2 | Fork v0.9.0 |
| --- | --- | --- |
| **ESP-IDF** | **5.5.2** | **6.1** |
| **LVGL** | **9.5.0** | **9.6.0** (registry package `9.6.0~1`) |
| ESP-Hosted | 2.11.7 | 2.12.13 |
| Wi-Fi Remote | 1.5.1 | 1.6.5 |
| WebSocket client | 1.7.0 | 1.8.0 |
| LittleFS | 1.21.1 | 1.23.0 |

The upstream column comes from the README and committed lock file at
`420df764`; it describes the existing Waveshare build, not a previous JC or
Tab5 release. Shared component versions are pinned across profiles, and the
LVGL/SDL simulator uses the same 9.6 rendering sources.

ESP-IDF 6 integration includes explicit managed cJSON, MQTT and USB components,
updated component requirements and LCD API names in the Waveshare BSPs. Required
patches are reapplied automatically during CMake configuration so a component
refetch retains them. Use [the ESP-IDF 6.1 build instructions](development/build.md#set-up).

??? info "ESP-Hosted and C6 firmware"

    P4 builds use ESP-Hosted 2.12.13 and its matching C6 adapter firmware. The
    dependency includes locked RPC response tables, correct failure reporting
    and receive-path handling for slow RPCs. A required C6 update runs once and
    restarts the panel; with OTA rollback enabled it waits until the P4 app has
    passed validation. The newly added JC and Tab5 profiles use the same stack.

??? info "Dependencies for the new camera and audio profiles"

    JC support uses `esp_codec_dev` 1.6.2. JC and Tab5 cameras use `esp_video`
    2.3.0, `esp_cam_sensor` 2.3.0 and `esp_ipa` 2.2.0. The local sensor component
    includes the SC202CS/SC2356 driver from the same upstream release for Tab5.
    These are dependencies of new fork profiles. Newer video/IPA combinations
    require instructions unsupported by the targeted P4 v1.x silicon; see
    [the component version caps](development/components.md#vendored-camera-component-and-the-p4-silicon-cap).

#### Performance and memory

??? info "LVGL allocation and measured fork headroom"

    All P4 profiles allocate LVGL objects from PSRAM first, reserving scarce
    internal RAM for Wi-Fi/SDIO, TLS and task stacks. The measurements below
    were recorded on **2026-10-01 with fork development builds**. They are
    snapshots of those test builds, rather than upstream-versus-fork benchmarks.

    | Display device | Free internal heap after boot | Largest free internal block | Lowest free internal heap in stress runs |
    | --- | --- | --- | --- |
    | Waveshare `panel4` | 329 KB | 152 KB | 165–226 KB across separate runs |
    | Newly added Guition `paneljc` | 321 KB | 163.8 KB | 184 KB |

    Other profiles use the same allocation policy without their own confirmed
    measurements here. Earlier/later fork comparisons and workload details
    remain in [Memory and heap](development/learnings.md).

- **Layout initialization:** the shared state pass visits each widget once,
  avoiding repeated per-entity scans. A fork development test on the newly
  supported paneljc rendered 168 widgets in about 2.2 s and 224 in 3.6 s.
  See [layout measurements](development/learnings.md) for test conditions.

#### Development and verification

??? info "Documentation, examples and GitHub Pages"

    **Documentation:** the site is organised for two audiences: a feature tour, a Pages and layouts guide and a
      grouped widget library with thumbnails for Home Assistant users, and the build, hardware and device notes for
      developers; the release notes list user-facing changes before the developer sections.
      The README now highlights selected current dashboard and
      editor examples, with detailed installation, feature and editor guides in `docs/`.
      The hardware overview includes M5Stack Tab5 in one comparison ordered by display size,
      with audio and camera functions and their validation status.
      Device guides share manufacturer references and labelled illustrations with a short flashing-connector instruction. Public demo
      screenshots come from the LVGL simulator and the current editor against an offline
      API. `tools/editor_local.py` serves the current editor with synthetic data and local saves, without flashing. Zensical builds the documentation with uv and locked dependencies; CI validates
      pull requests and publishes `main` to GitHub Pages in the maintainer's fork.
      The unused upstream screenshots are retired from the published tree; the
      unchanged original BETTA OS logo lives in `docs/img/brand/` with attribution.
      Direct `.html` navigation supports opening the built pages from the filesystem;
      the wider content area and scrollable gallery tables keep widget previews readable.
      Hardware documentation separates device selection and display device guides from CPU/memory,
      partition/storage and peripheral references. Display controls have their own guide;
      Appearance compares the same dashboard side by side in all four styles.
      Each widget has a guide under Get started, followed by a [Top bar](top-bar.md) guide
      for date/weather/stock slots, the clock and connection detail cards, with cropped
      4-, 5- and 10-inch examples explaining the available space, including Date + Weather + Stock
      on wide displays with a demo VWCE.DE ETF quote. Gallery previews preserve relative
      pixel sizes, detailed widgets use wider comparisons, and documentation images
      open in a local enlarged viewer with Close/Escape controls.
      Installation documents published GitHub images and source builds as separate
      paths, including display device/revision selection, factory versus OTA images, adapter
      firmware and first-run setup. Published images cover Waveshare 4-inch and Guition original 10-inch.
      Setup examples show the real boot splash with demo AP credentials and the WebUI's
      Wi-Fi/Home Assistant stages. The Select example is wider and includes its option
      picker; Recorder history has a user guide and widget-level popup comparisons.
      Notifications include finished overlay previews and separate MQTT and REST setup
      paths. Gallery generators write Markdown and images; Zensical produces the HTML pages,
      replacing the separate standalone gallery exports. The documentation search indexes pages and sections and works from
      `file://` with Zensical's offline mode and a locally bundled worker shim.
      README and site invite forks, personal adaptations, issues and pull requests,
      with a short [Contributing](contributing.md) guide and links to this fork.
      Appearance and notification user guides link to separate implementation
      references. Widget guides use readable option tables; installation, backups,
      night shutoff and the local/Recorder graph sources have explicit instructions.

- Variant build wrappers, pinned shared dependencies, host tests, editor tests,
  source checks and SDL layout checks support panel4 and paneljc; CI installs pinned FontTools, Brotli and Pillow.
  The simulator renders firmware widgets and page chrome; the
  [dashboard examples](features.md) and [10-inch gallery](gallery-sim-paneljc.md) use public synthetic data.
  Household captures stay local; vendored camera build files carry Apache-2.0 modification notices. See [development](development.md) for commands and limits.

#### Why this fork changes flash and boot behaviour

??? info "OTA placement, crash evidence and bootloader recovery"

    - **Bootable OTA locations:** upstream v0.8.2's 32 MiB table places the second
      OTA slot above the ESP32-P4's current 16 MiB quad-flash cache mapping range.
      The fork uses two app slots below that boundary so an update can boot from
      either location. The current fork uses two 6.5 MiB slots on all P4 profiles,
      including the newly introduced 16 MiB JC and Tab5 targets. This corrects the
      inherited 32 MiB mapping issue and defines storage for the new profiles;
      LittleFS data can still be accessed above 16 MiB through the SPI flash API.
    - **Crash evidence after restart:** dedicated panic-dump partitions retain
      the failing task and execution state so a crash can be investigated after
      the panel reboots. They are separate from the eight-entry restart history
      in NVS and the running text log in PSRAM. See [coredumps](coredump.md).
    - **Recovery from a failed update:** with the rollback-capable bootloader,
      the new app must pass its uptime, HTTP and network checks before it is
      accepted. A reset before validation returns to the preceding firmware,
      reducing the need for USB
      recovery after a bad OTA update. Installing this bootloader needs USB once.
      This is OTA recovery, not ESP Secure Boot or signed-image enforcement.
