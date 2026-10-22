<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->

# Crash dumps

A panic writes a raw ELF core dump to the P4's dedicated flash partition. At the next boot,
the firmware archives it in LittleFS and keeps the **two newest reports**, newest first.
A third report replaces the oldest after the new archive has been written and verified.
No partition change is needed for the archive.

Every current variant has a 256 KiB `coredump` partition at `0xD20000`. The paneljc panic
collected on 2026-10-07 was 24,768 bytes (24.2 KiB, 29 tasks); size varies with task count
and stack usage. Two such reports use about 50 KiB plus metadata. Allow space for a third
file during import: at the 256 KiB limit, three raw payloads alone need 768 KiB.

A panel without a `coredump` partition needs the
[partition migration](development/build.md#changing-the-partition-table) over USB first.
OTA changes the app slot, not the partition table. `coredump.supported` in
`GET /api/version` reports whether the partition exists.

## Reports in the editor

Open **Settings → Device → Device status**. Each report has its own crash description,
time, build, size, **Download crash dump**, **Erase** and expandable **Details**.
Erasing asks for confirmation and deletes only the selected report. Download it and
save its metadata before erasing it; the raw file does not contain the archive's extra
RTC diagnostic marker.

The editor banner announces the newest report. Dismissing it hides that banner only.
A **yellow warning triangle in the display's top bar** stays visible while any report
is retained, including one whose editor banner was dismissed. It disappears after the
last report is erased; see [Top bar](top-bar.md#crash-warning).

## Collection

Before opening USB or rebooting a responsive panel, save `GET /api/version?tasks=1`,
`GET /api/log` and `GET /api/reboot-log`. Opening a serial port can reset the panel and
lose volatile evidence.

`GET /api/version` contains `coredump.items`, up to two reports. Each has:

- `id`: SHA256 of the raw dump; use this stable ID when downloading or deleting.
- `task`, `pc`, `mcause`, `mcause_name`, `size` and `panic_reason`.
- `crashed_at` and `boot_seq`: from the latest retained panic's reboot breadcrumb.
  Time is approximate; `0` means unknown, such as a crash before time synchronisation.
- `elf_sha256`: the crashing ELF's hash or the prefix saved by ESP-IDF.
- `project`, `version`, `git_commit`, `build_date` and `build_time`: filled only when the
  dump's ELF hash matches the importing firmware. An older dump retained across OTA
  must not be labelled with the new build; empty fields mean unknown.
- `cause` and, for a PPA timeout, `ppa`: the additional diagnostic evidence described below.

The scalar fields in `coredump` also describe the newest report for older clients.
`present: false` and an empty `items` array mean there is no retained report;
`archive_ready: false` means the filesystem archive could not be initialised.

```bash
curl -o version.json 'http://<panel-ip>/api/version?tasks=1'
curl -o reboot-log.json 'http://<panel-ip>/api/reboot-log'
curl -o panel.log 'http://<panel-ip>/api/log'
curl -o coredump.bin 'http://<panel-ip>/api/coredump?id=<report-id>'
```

Add `--user admin` when the admin password is set; curl prompts for it. A download is
raw ESP-IDF data, without the archive header. Requests without an ID select the newest
report for compatibility. A malformed ID returns HTTP 400; an expired or deleted ID
returns HTTP 404 and never selects a different report.

Delete a collected report with the editor or:

```bash
curl -X POST -H 'Content-Type: application/json' \
    'http://<panel-ip>/api/coredump/erase?id=<report-id>'
```

## Matching ELF and backtrace

The exact ELF that ran at the crash is required. Rebuilding the same commit does not
reproduce it reliably. `tools/build.sh` archives the newest 12 successful ELFs per
variant under `.local/elf/<variant>/<first-9-SHA256-digits>.elf`; compare the recorded
`elf_sha256` before decoding. Preserve older ELFs separately when keeping reports longer.
The currently running build in `/api/version` can differ from an archived report's build.

With ESP-IDF active, decode without a live panel or serial port:

```bash
python -m esp_coredump info_corefile -t raw -c coredump.bin \
    .local/elf/paneljc/<elf-hash-prefix>.elf
```

ESP-IDF provides `esp-coredump`; install it with `python -m pip install esp-coredump`
if needed. Version 1.17 reads the chip from the ELF and has no `--chip` option.
If GDB is not found in `PATH`, pass `-g <path-to-riscv32-esp-elf-gdb>` after
`info_corefile`. Keep the decoded output alongside the raw dump and saved API responses.

## Diagnostic evidence

Before a deliberate UI recovery abort, `util/coredump_diagnostics_esp.c` saves a small
checksummed marker in `RTC_NOINIT` memory. Import consumes it only after a panic reset
and stores it with that report. It adds no filesystem write or allocation to the abort
path. Power loss, an invalid checksum or an ordinary reboot discards the marker.

| `cause` | Meaning |
| --- | --- |
| `ppa_timeout` | PPA rotation was accepted but no completion arrived within 1,000 ms |
| `lvgl_stall` | The LVGL heartbeat stopped responding |
| `runtime_stall` | The dashboard runtime heartbeat stopped responding |
| empty | No additional marker; inspect the panic reason and stacks |

For `ppa_timeout`, `ppa` records `width`, `height`, the original `input_x`/`input_y`,
`rotation` in degrees, `input_buffer`, `output_buffer`, `output_capacity` in bytes and
`wait_ms`. The strong hook in `util/ui_watchdog_esp.c` receives these from the shared
`drivers/display_port.c` rotation callback. Older images used the patched `esp_lvgl_port`;
`tools/apply_component_patches.py` retains that compatibility patch.
This identifies the failed operation; it does not fix the underlying PPA failure.

A PPA timeout aborts in `lvgl` (`taskLVGL` in older images); a heartbeat timeout aborts in `ui_watchdog`.
For the latter, inspect the other tasks, especially `lvgl`/`taskLVGL`, `ui_runtime`, `httpd`
and Home Assistant tasks, to locate the blocked lock or driver. Recovery remains bounded
to three consecutive restarts; the extra metadata does not change that policy.

**Illegal instruction is not proof of a corrupt function pointer.** On RISC-V, a deliberate
`abort()` can stop at `panic_abort` with an illegal-instruction trap. Check `panic_reason`,
`cause` and the matching backtrace first. The paneljc dump from 2026-10-07 decoded to the
PPA timeout hook followed by `abort()`, rather than an unexplained jump.

| `mcause` | Trap |
| --- | --- |
| `0x1` | Instruction access fault |
| `0x2` | Illegal instruction, including deliberate abort paths |
| `0x5` | Load access fault |
| `0x7` | Store access fault |

For load/store faults, `mtval` near zero suggests a null-pointer access; the full decoder
provides the register values and task stacks.

## Archive implementation

`util/coredump_report.c` reads the raw partition. `util/coredump_archive.c` imports it after
NVS/reboot history and LittleFS are ready, before networking and UI start.
`util/coredump_store.c` owns the filesystem format and is host-tested.

Each `/littlefs/coredumps/<id>.dump` holds a versioned header, checksummed JSON metadata
and the raw payload. Import writes `incoming.tmp`, flushes and syncs it, verifies metadata
and payload by reading them back, renames it, then prunes the oldest valid report.
Only after success is the raw partition erased. Interrupted imports leave the previous
reports intact; a completed import with an interrupted raw erase is deduplicated by ID
without changing its original metadata. Ordering uses an archive sequence, independent
of NTP and reboot-log resets.

If import fails, the raw partition remains downloadable and deletable through the same
API. That fallback protects the current dump, but the next panic can overwrite it;
collect it promptly and inspect the boot log. Invalid archive files are excluded from
the list and left intact for investigation. Filesystem formatting or factory reset can
remove archived reports. The partition is still needed to capture the next panic.

Only the HTTP task accesses the store after boot. The UI reads an atomic report count;
LVGL visibility changes run in the UI task under the display lock.

## Missing dumps

- Power loss and brownout cannot write a dump.
- A hang leaves evidence only if a watchdog or another path raises a panic.
- A crash of the ESP32-C6 co-processor is outside the P4 dump's scope.

## Network restarts

A network-watchdog restart (`net_watchdog` or `http_watchdog` in `/api/reboot-log`) is deliberate and leaves no
dump. Read `wifi_diag` in the saved `/api/version`: `c6_memory` is the Wi-Fi co-processor's current heap, and
`c6_last_incident` the last sample kept for a disconnect, a transport loss or such a restart. With
`restart_cause` the sample was written just before that restart, which also covers a silent gateway with the link
still up. `same_host_boot: false` means it comes from an earlier boot; `sample_age_ms` is the sample's age at the
incident, so an old sample says little about the moment itself. A low `free_heap_bytes` or `min_free_heap_bytes`
points at the C6 rather than the P4. These values are not yet confirmed on hardware.
- A panic before import can overwrite a previous raw dump; only reports successfully
  archived after boot get the two-report retention guarantee.
