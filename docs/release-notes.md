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

- **Energy day totals:** the Energy page in Home Assistant Energy mode uses the full local day on daylight-saving transitions. Its statistics request could omit hours from the day's totals when the UTC offset changed.
- **Consistent widget controls:** the active theme colour (gold in Classic) now means active or selected. Media Play/Pause, vacuum Start/Pause and printer Pause/Resume follow the running state; the selected wallbox charging mode is highlighted even for Off. Mode-list openers, scenes, favorites, transport actions and temperature +/− stay neutral. The printer's destructive Stop is labelled Abort and remains separate from Pause/Resume with its confirmation. Shared control colours and contrast also apply to selection lists, room controls and editor previews.
- **Control edge cases:** price-day and chart-period selections also use the shared active colour; the PIN-pad confirmation stays neutral. Missing bindings no longer appear as active controls in previews, and optional Stop actions follow reported capabilities. Unavailable climate, heating, date/time, lock and alarm entities block stale actions; open action dialogs close when their entity becomes unavailable. A separate simulator topic shows 50 process, selection, alert and unavailable cases, including the legacy Roborock name.

- **Large stock layouts:** history registrations grow with the layout instead of stopping at 24 quotes across all pages, leaving room for the detail popup too. Registry lookups discard removed entities so repeated layout edits cannot exhaust the provider cache. Subscription and registry limits match the 4,096-entity model; a valid 1,056-quote layout no longer loses its last sensors silently.
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
- **Touch on the 10-inch JC panel:** short taps are queued for the display instead of waiting for its next refresh, and weak-signal contacts are retained. The dated checks and their limits are in [Touch contacts](development/learnings.md#touch-contacts).

### Editor

- **C6 memory:** Settings → Device → Device status shows the Wi-Fi coprocessor's free heap, boot minimum, largest internal block and sample age. Details include DMA memory, diagnostic errors and a retained sample from the last connection incident or network-watchdog restart. Reports are cached once a minute; stale or unsupported data is labelled.

- **German and English:** the colour-theme editor, time-zone choices and solar-preview days follow the selected language. Entity-first Date/Time and Alarm choices have labels, and the add-widget message fills its placeholders in both languages. Setup, update and notification hints match the current controls. Editor text follows the selected language consistently.

- **Device class:** search results and the entity picker name a sensor's device class; the inspector header shows it with the unit as chips for every tile bound to a sensor or binary sensor, and the inspector offers the sensor's unit and format choices of that class.
- **Inspector:** one order for every widget: a header with type, name (edited in place) and device class, then Entities, Display, Controls and Placement as foldable cards (empty ones left out; Placement folded). Short fields put the label beside the control. The Properties and Position & size tabs are gone. Entities are rows with the role and the Home Assistant name; a click opens the search, which can also remove an optional entity or use a typed ID, and an ID Home Assistant does not list is marked. The missing-entity banner can be dismissed; an incomplete search does not mark unseen entities as missing. A section's explanations open from an info icon behind its heading, with bold standalone headings and separate paragraphs; hidden optional groups are left out and warnings stay directly visible; the page inspector has the same form. The info icon of a widget shows the summary of its guide. **Panel follows** beside Text size shows the page chosen in the editor on the panel too. A rejected navigation request can be retried on the next page synchronization. The entity search keeps the current entity on top and starts with the entities of the same device; the Energy page's sensors and the weather and stock entities in the settings use the same entity rows.
- **Preview:** widget text roles, fixed 10-inch layouts, visible elements and language follow the panel more closely. Overlapping tiles are marked on the canvas. Wallbox headers use the same height limit as the panel and show missing power without invented sample values; short tiles explain the required height. The SOC bar matches the panel's solid green fill. Documentation image zoom also recovers when a lazy thumbnail has not loaded yet. Preview checks remain separate from LVGL simulator and device validation.

### Validation limits

This candidate is not a published release. Host tests, simulator renders and firmware builds do not establish device behaviour.
Panel10 and paneljc-v2 still need hardware testing. The renamed `energy_price_forecast` widget type replaces the unreleased
`price_forecast` type; development layouts using the old type need their widget types changed alongside the firmware update.

## v0.9.0

Released fork baseline, compared with upstream `420df764` (v0.8.2):

- Guition JC8012P4A1 and M5Stack Tab5 support, with validation tracked per [display](choose-display.md).
- New [widgets](widgets/README.md), House and Energy [pages](pages.md), searchable entity selection and history.
- Four [appearance styles](appearance.md), larger text, consistent icons and optional notification sound.
- [MQTT/HTTP notifications](notifications.md), OTA rollback, retained crash evidence and optional automatic updates.
- Optional admin authentication and guards for requests, secrets, image fetching and firmware updates;
  current protections and limits are in [Security implementation](security-implementation.md).

The full release history, including the upstream comparison, dated measurements and security fixes,
is preserved under [tag v0.9.0](https://github.com/andre68723/BETTA-HA-PANEL/blob/v0.9.0/docs/release-notes.md)
and its immutable [commit dd4f752c5c60](https://github.com/andre68723/BETTA-HA-PANEL/blob/dd4f752c5c607dc36e3c7d999845b1c5be576052/docs/release-notes.md).
The complete archive needs a network connection; the upgrade instructions below remain local.
Panel10 and paneljc-v2 are compile-checked only. Current guide pages describe current behavior.

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
