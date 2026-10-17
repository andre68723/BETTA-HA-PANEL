/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/coredump_report.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "util/log_tags.h"

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
#include "esp_core_dump.h"
#endif

bool coredump_report_supported(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL) != NULL;
}

bool coredump_report_read(coredump_report_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    /* No partition on this variant, or nothing stored yet: esp_core_dump_image_check() reports that by
     * itself (ESP_ERR_NOT_FOUND either way), not an error worth logging. */
    if (esp_core_dump_image_check() != ESP_OK) {
        return true;
    }
    esp_core_dump_summary_t *summary = calloc(1, sizeof(*summary));
    if (summary == NULL) {
        return false;
    }
    bool ok = esp_core_dump_get_summary(summary) == ESP_OK;
    if (ok) {
        out->present = true;
        snprintf(out->task, sizeof(out->task), "%s", summary->exc_task);
        out->pc = summary->exc_pc;
        out->mcause = summary->ex_info.mcause;
        out->mtval = summary->ex_info.mtval;
        out->ra = summary->ex_info.ra;
        out->sp = summary->ex_info.sp;
        snprintf(out->elf_sha256, sizeof(out->elf_sha256), "%.*s", (int)sizeof(summary->app_elf_sha256),
                 (const char *)summary->app_elf_sha256);
        size_t addr = 0;
        esp_core_dump_image_get(&addr, &out->size);
    }
    free(summary);
    return ok;
#else
    return true;
#endif
}

const char *coredump_report_mcause_name(uint32_t mcause)
{
    /* RISC-V privileged spec, the exception causes this firmware can actually hit (interrupts have the
     * top bit set and never reach here as an exception). */
    switch (mcause) {
        case 0x1: return "Instruction access fault";
        case 0x2: return "Illegal instruction";
        case 0x5: return "Load access fault";
        case 0x7: return "Store access fault";
        default:  return NULL;
    }
}

void coredump_report_log_at_boot(void)
{
    coredump_report_t report;
    if (!coredump_report_read(&report) || !report.present) {
        return;
    }
    const char *cause = coredump_report_mcause_name(report.mcause);
    ESP_LOGW(TAG_APP,
        "A crash dump from a previous run is stored (%u bytes): task '%s', pc 0x%08" PRIx32
        ", mcause 0x%" PRIx32 " (%s), mtval 0x%08" PRIx32 " -- see docs/coredump.md for the full backtrace",
        (unsigned)report.size, report.task, report.pc, report.mcause, cause != NULL ? cause : "?", report.mtval);
}
