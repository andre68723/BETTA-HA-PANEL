/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/ui_watchdog_esp.h"

#include <stdint.h>
#include <stdlib.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "util/log_tags.h"
#include "util/reboot_log_esp.h"
#include "util/ui_watchdog.h"
#include "util/coredump_diagnostics.h"

#define UI_WD_POLL_MS 10000
#define UI_WD_BEAT_MS 1000
#define UI_WD_TASK_STACK 4096
#define UI_WD_RTC_MAGIC 0x55495744u

typedef struct {
    uint32_t magic;
    uint32_t restarts_in_a_row;
} ui_wd_rtc_t;

static RTC_NOINIT_ATTR ui_wd_rtc_t s_rtc;
static ui_wd_state_t s_state;
/* Milliseconds since boot of the last beat, 0 before the first one. A 32-bit word is written atomically. */
static volatile uint32_t s_lvgl_beat_ms;
static volatile uint32_t s_runtime_beat_ms;
static ui_wd_decision_t s_last_decision = UI_WD_OK;

static uint32_t now_ms(void)
{
    return ui_wd_stamp_ms(esp_timer_get_time()); /* never 0, which means "no beat yet" */
}

static int64_t age_s(uint32_t beat_ms)
{
    return beat_ms == 0U ? -1 : (int64_t)((uint32_t)(now_ms() - beat_ms) / 1000U);
}

void ui_watchdog_beat_runtime(void)
{
    s_runtime_beat_ms = now_ms();
}

static void lvgl_beat_cb(lv_timer_t *timer)
{
    (void)timer;
    s_lvgl_beat_ms = now_ms();
}

void ui_watchdog_attach_lvgl(void)
{
    s_lvgl_beat_ms = now_ms();
    (void)lv_timer_create(lvgl_beat_cb, UI_WD_BEAT_MS, NULL);
}

static void ui_watchdog_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(UI_WD_POLL_MS));
        const int64_t now_s = esp_timer_get_time() / 1000000LL;
        const ui_wd_input_t input = {
            .lvgl_age_s = age_s(s_lvgl_beat_ms),
            .runtime_age_s = age_s(s_runtime_beat_ms),
            .restarts_in_a_row = s_rtc.restarts_in_a_row,
        };
        s_last_decision = ui_wd_step(&s_state, &input, now_s);
        if (ui_wd_may_clear_restarts(&s_state, now_s)) s_rtc.restarts_in_a_row = 0;
        if (s_last_decision == UI_WD_SUPPRESSED) {
            ESP_LOGW(TAG_UI, "UI watchdog: the UI is stalled but %u restarts happened in a row; not restarting again",
                     (unsigned)s_rtc.restarts_in_a_row);
        } else if (s_last_decision != UI_WD_OK) {
            ESP_LOGE(TAG_UI, "UI watchdog: %s silent for %d s (LVGL %d s, ui_runtime %d s), core dump and restart (restart %u in a row)",
                     s_last_decision == UI_WD_RESTART_LVGL ? "the LVGL task" : "the ui_runtime loop",
                     (int)(s_last_decision == UI_WD_RESTART_LVGL ? input.lvgl_age_s : input.runtime_age_s),
                     (int)input.lvgl_age_s, (int)input.runtime_age_s, (unsigned)s_rtc.restarts_in_a_row + 1U);
            s_rtc.restarts_in_a_row++;
            reboot_log_mark(REBOOT_CAUSE_UI_WATCHDOG);
            const coredump_diagnostic_t evidence = {
                .cause = s_last_decision == UI_WD_RESTART_LVGL ? COREDUMP_CAUSE_LVGL_STALL : COREDUMP_CAUSE_RUNTIME_STALL,
            };
            coredump_diagnostics_mark(&evidence);
            vTaskDelay(pdMS_TO_TICKS(200));
#ifdef CONFIG_ESP_COREDUMP_ENABLE
            abort(); /* the core dump holds every task's stack: it names whoever holds the display lock */
#else
            esp_restart();
#endif
        }
    }
}

/* Strong definition of the hook the shared display adapter (and patched legacy esp_lvgl_port) calls when a PPA rotation did not finish within 1 s (tools/apply_component_patches.py).
 * The hardware never reported "done" (paneljc, 2026-10-05: taskLVGL blocked in ppa_do_operation and held the display lock), so waiting
 * longer cannot help; restart at once with a core dump instead of letting the 90 s UI watchdog do it. Same bound on restarts in a row. */
void lvgl_port_ppa_timeout_hook(uint32_t width, uint32_t height, uint32_t x, uint32_t y, uint32_t rotation,
                                uintptr_t input, uintptr_t output, uint32_t capacity)
{
    if (s_rtc.restarts_in_a_row >= UI_WD_MAX_RESTARTS) {
        ESP_LOGW(TAG_UI, "PPA rotation timed out but %u restarts happened in a row; not restarting again", (unsigned)s_rtc.restarts_in_a_row);
        return;
    }
    ESP_LOGE(TAG_UI, "PPA rotation did not finish, core dump and restart (restart %u in a row)", (unsigned)s_rtc.restarts_in_a_row + 1U);
    s_rtc.restarts_in_a_row++;
    reboot_log_mark(REBOOT_CAUSE_UI_WATCHDOG);
    const coredump_diagnostic_t evidence = {
        .cause = COREDUMP_CAUSE_PPA_TIMEOUT, .width = width, .height = height,
        .input_x = x, .input_y = y, .rotation = rotation,
        .input_buffer = (uint32_t)input, .output_buffer = (uint32_t)output,
        .output_capacity = capacity, .wait_ms = 1000,
    };
    coredump_diagnostics_mark(&evidence);
#ifdef CONFIG_ESP_COREDUMP_ENABLE
    abort();
#else
    esp_restart();
#endif
}

void ui_watchdog_start(void)
{
    const esp_reset_reason_t reason = esp_reset_reason();
    if (s_rtc.magic != UI_WD_RTC_MAGIC || reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT) {
        s_rtc.magic = UI_WD_RTC_MAGIC;
        s_rtc.restarts_in_a_row = 0;
    }
    ui_wd_init(&s_state);
    if (xTaskCreateWithCaps(ui_watchdog_task, "ui_watchdog", UI_WD_TASK_STACK, NULL, 2, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGW(TAG_UI, "UI watchdog not started");
    }
}

void ui_watchdog_add_json(cJSON *root)
{
    cJSON *wd = cJSON_AddObjectToObject(root, "ui_watchdog");
    if (wd == NULL) return;
    cJSON_AddNumberToObject(wd, "lvgl_age_s", (double)age_s(s_lvgl_beat_ms));
    cJSON_AddNumberToObject(wd, "runtime_age_s", (double)age_s(s_runtime_beat_ms));
    cJSON_AddNumberToObject(wd, "restarts_in_a_row", (double)s_rtc.restarts_in_a_row);
    cJSON_AddNumberToObject(wd, "last_decision", (double)s_last_decision);
}
