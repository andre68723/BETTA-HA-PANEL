/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/coredump_archive.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#include "component_versions.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "util/coredump_report.h"
#include "util/reboot_log_esp.h"
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
#include "esp_core_dump.h"
#endif

static const char *TAG = "coredump_archive";
static coredump_store_t s_store;
static coredump_record_t s_live;
static const esp_partition_t *s_partition;
static bool s_ready;
static atomic_uint s_report_count;
static size_t list_records(coredump_record_t out[COREDUMP_STORE_LIMIT]);

static void update_count(void)
{
    coredump_record_t records[COREDUMP_STORE_LIMIT];
    atomic_store(&s_report_count, (unsigned)list_records(records));
}

unsigned coredump_archive_count(void)
{
    return atomic_load(&s_report_count);
}

static bool read_partition(void *context, size_t offset, void *buffer, size_t size)
{
    const esp_partition_t *partition = context;
    return partition && esp_partition_read(partition, offset, buffer, size) == ESP_OK;
}

/* A retained dump can belong to an older image after OTA. Never label that dump with the
 * running image's build. Keep its ELF hash even when the original build text is unavailable. */
static void running_build_if_matching(coredump_record_t *record)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char elf[65];
    for (size_t i = 0; i < 32; i++) snprintf(elf + i * 2, 3, "%02x", app->app_elf_sha256[i]);
    size_t n = strlen(record->elf_sha256);
    if (n < 9 || n > 64 || strncmp(elf, record->elf_sha256, n)) return;
    snprintf(record->project, sizeof(record->project), "%s", app->project_name);
    snprintf(record->version, sizeof(record->version), "%s", app->version);
    snprintf(record->git_commit, sizeof(record->git_commit), "%s", APP_GIT_COMMIT_SHA);
    snprintf(record->build_date, sizeof(record->build_date), "%s", app->date);
    snprintf(record->build_time, sizeof(record->build_time), "%s", app->time);
}

static void import_dump(void)
{
    coredump_diagnostic_t diagnostic;
    coredump_diagnostics_take(&diagnostic); /* clear stale markers even when no valid dump was saved */
    s_ready = coredump_store_init(&s_store, "/littlefs/coredumps");
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    coredump_report_t report;
    if (!s_ready) ESP_LOGW(TAG, "Archive unavailable; raw crash dump will be kept in its partition");
    if (!s_partition || !coredump_report_read(&report) || !report.present || report.size > COREDUMP_STORE_MAX_BYTES) return;
    memset(&s_live, 0, sizeof(s_live));
    s_live.size = (uint32_t)report.size;
    s_live.pc = report.pc;
    s_live.mcause = report.mcause;
    s_live.diagnostic = diagnostic;
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    (void)esp_core_dump_get_panic_reason(s_live.panic_reason, sizeof(s_live.panic_reason));
#endif
    snprintf(s_live.task, sizeof(s_live.task), "%s", report.task);
    snprintf(s_live.elf_sha256, sizeof(s_live.elf_sha256), "%s", report.elf_sha256);
    reboot_entry_t reboot;
    if (reboot_log_latest_panic(&reboot)) {
        s_live.boot_seq = reboot.seq;
        s_live.crashed_at = reboot.prev_wall_epoch;
    }
    running_build_if_matching(&s_live);
    if (!coredump_store_hash(read_partition, (void *)s_partition, s_live.size, s_live.id)) {
        ESP_LOGW(TAG, "Unable to read raw crash dump for import");
        memset(&s_live, 0, sizeof(s_live));
        return;
    }
    if (!s_ready || !coredump_store_put(&s_store, &s_live, read_partition, (void *)s_partition)) {
        ESP_LOGW(TAG, "Archive import failed; raw crash dump remains downloadable");
        return;
    }
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    /* Rename, fsync and read-back checksum validation have succeeded before any flash erase. */
    if (esp_core_dump_image_erase() != ESP_OK) {
        ESP_LOGW(TAG, "Archive saved, but raw dump could not be cleared; next boot will deduplicate it");
        return;
    }
#endif
    ESP_LOGI(TAG, "Crash dump archived (%" PRIu32 " bytes); keeping at most %d reports", s_live.size, COREDUMP_STORE_LIMIT);
    memset(&s_live, 0, sizeof(s_live));
}

void coredump_archive_init(void)
{
    import_dump();
    update_count();
}

static size_t list_records(coredump_record_t out[COREDUMP_STORE_LIMIT])
{
    size_t count = 0;
    if (s_live.id[0]) out[count++] = s_live;
    for (size_t i = 0; i < s_store.count && count < COREDUMP_STORE_LIMIT; i++) {
        if (s_live.id[0] && !strcmp(s_live.id, s_store.records[i].id)) {
            out[0] = s_store.records[i]; /* interrupted erase: preserve original metadata */
            continue;
        }
        out[count++] = s_store.records[i];
    }
    return count;
}

bool coredump_archive_get(const char *id, coredump_record_t *out)
{
    if (!out || (id && id[0] && !coredump_store_id_valid(id))) return false;
    coredump_record_t records[COREDUMP_STORE_LIMIT];
    size_t count = list_records(records);
    for (size_t i = 0; i < count; i++) {
        if (!id || !id[0] || !strcmp(id, records[i].id)) { *out = records[i]; return true; }
    }
    return false;
}

bool coredump_archive_read(const char *id, size_t offset, void *buffer, size_t size)
{
    coredump_record_t record;
    if (!coredump_archive_get(id, &record) || offset > record.size || size > record.size - offset) return false;
    if (!strcmp(record.id, s_live.id)) return read_partition((void *)s_partition, offset, buffer, size);
    return coredump_store_read(&s_store, record.id, offset, buffer, size);
}

esp_err_t coredump_archive_delete(const char *id)
{
    coredump_record_t record;
    if (!coredump_archive_get(id, &record)) return ESP_ERR_NOT_FOUND;
    if (!strcmp(record.id, s_live.id)) {
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
        esp_err_t err = esp_core_dump_image_erase();
        if (err != ESP_OK) return err;
#endif
        memset(&s_live, 0, sizeof(s_live));
        update_count();
    }
    for (size_t i = 0; i < s_store.count; i++) {
        if (!strcmp(record.id, s_store.records[i].id)) {
            bool ok = coredump_store_delete(&s_store, record.id);
            update_count();
            return ok ? ESP_OK : ESP_FAIL;
        }
    }
    return ESP_OK;
}

static void add_record(cJSON *json, const coredump_record_t *record)
{
    cJSON_AddBoolToObject(json, "present", true);
    cJSON_AddStringToObject(json, "id", record->id);
    cJSON_AddStringToObject(json, "task", record->task);
    char hex[11];
    snprintf(hex, sizeof(hex), "0x%08" PRIx32, record->pc);
    cJSON_AddStringToObject(json, "pc", hex);
    snprintf(hex, sizeof(hex), "0x%08" PRIx32, record->mcause);
    cJSON_AddStringToObject(json, "mcause", hex);
    const char *trap_name = coredump_report_mcause_name(record->mcause);
    if (trap_name) cJSON_AddStringToObject(json, "mcause_name", trap_name);
    cJSON_AddNumberToObject(json, "size", record->size);
    cJSON_AddNumberToObject(json, "crashed_at", (double)record->crashed_at);
    cJSON_AddNumberToObject(json, "boot_seq", record->boot_seq);
    cJSON_AddStringToObject(json, "elf_sha256", record->elf_sha256);
    cJSON_AddStringToObject(json, "project", record->project);
    cJSON_AddStringToObject(json, "version", record->version);
    cJSON_AddStringToObject(json, "git_commit", record->git_commit);
    cJSON_AddStringToObject(json, "build_date", record->build_date);
    cJSON_AddStringToObject(json, "build_time", record->build_time);
    cJSON_AddStringToObject(json, "panic_reason", record->panic_reason);
    static const char *causes[] = {"", "ppa_timeout", "lvgl_stall", "runtime_stall"};
    uint32_t cause = record->diagnostic.cause;
    if (cause <= COREDUMP_CAUSE_RUNTIME_STALL) cJSON_AddStringToObject(json, "cause", causes[cause]);
    if (cause == COREDUMP_CAUSE_PPA_TIMEOUT) {
        cJSON *ppa = cJSON_AddObjectToObject(json, "ppa");
        if (ppa) {
#define NUMBER(field) cJSON_AddNumberToObject(ppa, #field, record->diagnostic.field)
            NUMBER(width); NUMBER(height); NUMBER(input_x); NUMBER(input_y); NUMBER(rotation);
            NUMBER(input_buffer); NUMBER(output_buffer); NUMBER(output_capacity); NUMBER(wait_ms);
#undef NUMBER
        }
    }
}

void coredump_archive_add_json(cJSON *root)
{
    cJSON *json = cJSON_AddObjectToObject(root, "coredump");
    if (!json) return;
    cJSON_AddBoolToObject(json, "supported", coredump_report_supported());
    cJSON_AddBoolToObject(json, "archive_ready", s_ready);
    coredump_record_t records[COREDUMP_STORE_LIMIT];
    size_t count = list_records(records);
    if (count) add_record(json, &records[0]); /* scalar fields remain compatible */
    else cJSON_AddBoolToObject(json, "present", false);
    cJSON *items = cJSON_AddArrayToObject(json, "items");
    if (!items) return;
    for (size_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        if (!item) break;
        add_record(item, &records[i]);
        cJSON_AddItemToArray(items, item);
    }
}
