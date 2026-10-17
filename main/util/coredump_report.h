/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Crash diagnostics: a panic (not a plain reboot or a watchdog reset without one) writes an ELF core
 * dump to a dedicated flash partition (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH, see config/sdkconfig.defaults).
 * It survives the reboot that follows, unlike the serial log, and is only overwritten by the next panic
 * -- there is no rush to read it. This module reads back the summary (no esp-coredump tool needed for
 * that much) so it reaches GET /api/log on every boot and GET /api/version without a USB connection.
 * A full symbolised backtrace still needs the matching .elf and the esp-coredump tool; see docs/coredump.md.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool present;      /* a checksum-valid dump is stored; the fields below are only meaningful then */
    char task[16];      /* name of the task that crashed */
    uint32_t pc;         /* program counter at the crash */
    uint32_t mcause;     /* RISC-V trap cause */
    uint32_t mtval;      /* RISC-V trap value (the bad address, for a load/store fault) */
    uint32_t ra;         /* return address */
    uint32_t sp;         /* stack pointer */
    size_t size;        /* bytes of the stored dump in flash, including its checksum */
    char elf_sha256[65]; /* crashing application's ELF SHA256 prefix from the dump */
} coredump_report_t;

/* Fills *out from the coredump partition. Safe on every boot and every variant, including one without
 * a coredump partition in its table (out->present stays false, nothing logged as an error): this is
 * exactly esp_core_dump's own behaviour when esp_partition_find_first() comes back empty. */
bool coredump_report_read(coredump_report_t *out);

/* True iff this device's actual flash has a coredump partition right now. False either for a variant
 * whose partition table never has one, or for a panel4/panel10/paneljc that has the
 * partition in its current source tree but was only ever updated over OTA, which never writes a
 * partition table -- see docs/coredump.md. The editor uses this to tell "nothing to report" (present
 * but empty) apart from "cannot report anything here" (not present at all). */
bool coredump_report_supported(void);

/* One ESP_LOGW line if a dump is stored, nothing otherwise. Call once early in app_main, after the
 * reset-reason line: a dump only means something next to knowing this boot followed a panic. */
void coredump_report_log_at_boot(void);

/* A short RISC-V mcause name ("Load access fault", ...) for the log and the API, or NULL for a value
 * this table does not name (still shown as the raw number). */
const char *coredump_report_mcause_name(uint32_t mcause);
