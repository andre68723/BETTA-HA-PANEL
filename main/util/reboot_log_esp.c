/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/reboot_log_esp.h"

#include <string.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"

#include "util/flash_trace.h"
#include "util/log_tags.h"

#define REBOOT_LOG_NVS_NAMESPACE "reboot_log"
#define REBOOT_LOG_NVS_KEY "ring"
#define REBOOT_LOG_REFRESH_US (5ULL * 1000000ULL)
#define REBOOT_LOG_VALID_EPOCH 1767225600 /* 2026-01-01: earlier means the clock was not set yet */

/* the values reboot_log.c copies */
_Static_assert(ESP_RST_POWERON == 1 && ESP_RST_SW == 3 && ESP_RST_PANIC == 4 && ESP_RST_INT_WDT == 5 &&
                   ESP_RST_TASK_WDT == 6 && ESP_RST_WDT == 7 && ESP_RST_BROWNOUT == 9,
               "esp_reset_reason_t changed: update util/reboot_log.c");

static RTC_NOINIT_ATTR reboot_crumb_t s_crumb;
static reboot_ring_t s_ring;
static char s_last_label[40];
static reboot_cause_t s_last_cause = REBOOT_CAUSE_NONE;
static bool s_last_run_failed;

static void crumb_refresh(void *arg)
{
    (void)arg;
    s_crumb.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000LL);
    s_crumb.min_free_internal = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    s_crumb.largest_internal = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const time_t now = time(NULL);
    s_crumb.wall_epoch = now >= REBOOT_LOG_VALID_EPOCH ? (int64_t)now : 0;
    reboot_crumb_seal(&s_crumb);
}

static void ring_load(void)
{
    nvs_handle_t handle;
    memset(&s_ring, 0, sizeof(s_ring));
    if (nvs_open(REBOOT_LOG_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(s_ring);
        if (nvs_get_blob(handle, REBOOT_LOG_NVS_KEY, &s_ring, &size) != ESP_OK || size != sizeof(s_ring)) {
            memset(&s_ring, 0, sizeof(s_ring));
        }
        nvs_close(handle);
    }
    reboot_ring_init_if_invalid(&s_ring);
}

static void ring_save(void)
{
    nvs_handle_t handle;
    if (nvs_open(REBOOT_LOG_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        if (nvs_set_blob(handle, REBOOT_LOG_NVS_KEY, &s_ring, sizeof(s_ring)) == ESP_OK) {
            FLASH_TRACE_BEGIN(trace_start);
            (void)nvs_commit(handle);
            FLASH_TRACE_END(trace_start, "reboot log (nvs)", sizeof(s_ring));
        }
        nvs_close(handle);
    }
}

void reboot_log_init(void)
{
    const uint8_t reason = (uint8_t)esp_reset_reason();
    reboot_entry_t entry;
    reboot_entry_build(&entry, 0, reason, &s_crumb);
    ring_load();
    reboot_ring_push(&s_ring, &entry);
    ring_save();
    snprintf(s_last_label, sizeof(s_last_label), "%s", reboot_entry_label(&entry));
    s_last_run_failed = reboot_entry_is_failure(&entry);
    s_last_cause = entry.cause < REBOOT_CAUSE_COUNT ? (reboot_cause_t)entry.cause : REBOOT_CAUSE_NONE;
    ESP_LOGI(TAG_APP, "Previous run: %s (reset reason %s, ran %us, internal heap minimum %u bytes, boot #%u)", s_last_label,
             reboot_reason_name(reason), (unsigned)entry.prev_uptime_s, (unsigned)entry.min_free_internal,
             (unsigned)entry.seq);

    memset(&s_crumb, 0, sizeof(s_crumb));
    crumb_refresh(NULL);
    static esp_timer_handle_t timer = NULL;
    const esp_timer_create_args_t args = {.callback = crumb_refresh, .name = "reboot_log"};
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        (void)esp_timer_start_periodic(timer, REBOOT_LOG_REFRESH_US);
    }
}

void reboot_log_mark(reboot_cause_t cause)
{
    s_crumb.cause = (uint8_t)cause;
    crumb_refresh(NULL);
}

reboot_cause_t reboot_log_last_cause(void)
{
    return s_last_cause;
}

bool reboot_log_last_run_failed(void)
{
    return s_last_run_failed;
}

bool reboot_log_latest_panic(reboot_entry_t *out)
{
    if (!out) return false;
    for (int i = 0; i < REBOOT_LOG_SLOTS; i++) {
        if (!reboot_ring_get(&s_ring, i, out)) break;
        if (out->reset_reason == ESP_RST_PANIC) return true;
    }
    return false;
}

const char *reboot_log_last_label(void)
{
    return s_last_label;
}

void reboot_log_add_json(cJSON *root)
{
    cJSON_AddNumberToObject(root, "boot_seq", (double)s_ring.next_seq);
    cJSON *list = cJSON_AddArrayToObject(root, "entries");
    reboot_entry_t entry;
    for (int i = 0; list != NULL && reboot_ring_get(&s_ring, i, &entry); i++) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) break;
        cJSON_AddNumberToObject(item, "seq", (double)entry.seq);
        cJSON_AddStringToObject(item, "label", reboot_entry_label(&entry));
        cJSON_AddStringToObject(item, "reason", reboot_reason_name(entry.reset_reason));
        cJSON_AddStringToObject(item, "cause", reboot_cause_name(entry.cause));
        cJSON_AddBoolToObject(item, "previous_run_known", entry.had_crumb != 0);
        cJSON_AddNumberToObject(item, "previous_uptime_s", (double)entry.prev_uptime_s);
        cJSON_AddNumberToObject(item, "min_free_internal", (double)entry.min_free_internal);
        cJSON_AddNumberToObject(item, "largest_internal", (double)entry.largest_internal);
        cJSON_AddNumberToObject(item, "previous_wall_epoch", (double)entry.prev_wall_epoch);
        cJSON_AddItemToArray(list, item);
    }
}
