/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "net/auto_update_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "api/api_ota.h"
#include "app_config.h"
#include "app_events.h"
#include "esp_app_desc.h"
#include "drivers/display_init.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net/update_check.h"
#include "net/wifi_mgr.h"
#include "nvs.h"
#include "util/flash_gate.h"
#include "ui/ui_i18n.h"
#include "util/auto_update.h"
#include "util/auto_update_state.h"
#include "util/boot_guard.h"
#include "util/ota_health.h"
#include "util/ota_validate.h"
#include "util/reboot_log_esp.h"
#include "util/user_activity.h"

#define AUTO_UPDATE_NVS_NAMESPACE "auto_update"
#define AUTO_UPDATE_NVS_KEY "state"
#define AUTO_UPDATE_TASK_STACK 5120
#define AUTO_UPDATE_FIRST_DELAY_MS (90 * 1000) /* the dashboard is up and the update check has run */
#define AUTO_UPDATE_POLL_MS (AUTO_UPDATE_IDLE_MIN_S * 1000) /* inside the window, after a brief obstacle: as long as the idle time asks for */
#define AUTO_UPDATE_POLL_NO_CLOCK_MS (10 * 60 * 1000) /* until the clock is set */
#define AUTO_UPDATE_POLL_MAX_MS (6 * 60 * 60 * 1000)  /* longest sleep between two evaluations */
#define AUTO_UPDATE_INSTALL_WATCH_MS (20 * 60 * 1000)

static const char *TAG __attribute__((unused)) = "auto_update"; /* unused where the host tests stub the log macros */

static SemaphoreHandle_t s_lock;
static auto_update_state_t s_state;
static bool s_enabled;
static bool s_state_loaded;
static bool s_storage_ok;
static auto_update_decision_t s_decision = AUTO_UPDATE_WAIT_DISABLED;
static bool s_started;
static TaskHandle_t s_task;
#if CONFIG_APP_AUTO_UPDATE_TEST_HOOK
static bool s_test_force;
static bool s_test_hold; /* a faked attempt is not resolved by this boot: in real life the panel restarts right after writing the image */
#endif

static esp_err_t state_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(AUTO_UPDATE_NVS_NAMESPACE, NVS_READONLY, &handle);
    auto_update_state_t stored;
    auto_update_state_init(&stored);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else if (err == ESP_OK) {
        size_t len = sizeof(stored);
        err = nvs_get_blob(handle, AUTO_UPDATE_NVS_KEY, &stored, &len);
        nvs_close(handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) { auto_update_state_init(&stored); err = ESP_OK; }
        else if (err == ESP_OK && (len != sizeof(stored) || stored.version != AUTO_UPDATE_STATE_VERSION ||
            stored.result > AUTO_UPDATE_RESULT_FAILED ||
            memchr(stored.attempt_tag, 0, sizeof(stored.attempt_tag)) == NULL ||
            memchr(stored.blocked_tag, 0, sizeof(stored.blocked_tag)) == NULL ||
            memchr(stored.result_tag, 0, sizeof(stored.result_tag)) == NULL)) err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) { s_state = stored; s_state_loaded = true; }
    s_storage_ok = err == ESP_OK;
    return err;
}

static esp_err_t state_save_locked(void)
{
    esp_err_t err = ESP_ERR_TIMEOUT;
    if (flash_gate_enter(2000)) {
        nvs_handle_t handle;
        err = nvs_open(AUTO_UPDATE_NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_blob(handle, AUTO_UPDATE_NVS_KEY, &s_state, sizeof(s_state));
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
        flash_gate_leave();
    }
    s_storage_ok = err == ESP_OK;
    if (!s_storage_ok) {
        s_decision = AUTO_UPDATE_WAIT_STORAGE;
        ESP_LOGE(TAG, "Automatic update state not saved: %s", esp_err_to_name(err));
    }
    return err;
}

static int64_t wall_epoch(void)
{
    const time_t now = time(NULL);
    return now > 1700000000 ? (int64_t)now : 0; /* 0 while the clock is not set */
}

static bool show_rollback_notice(const char *tag)
{
    app_notification_t *n = calloc(1, sizeof(*n));
    if (n == NULL) return false;
    if (!display_lock(20)) { free(n); return false; }
    snprintf(n->title, sizeof(n->title), "%s", ui_i18n_get("autoupdate.rollback_title", "Update rolled back"));
    snprintf(n->message, sizeof(n->message), "%s %s %s", ui_i18n_get("autoupdate.rollback_message", "The automatic update to"), tag,
        ui_i18n_get("autoupdate.rollback_message2", "failed. The panel returned to the previous firmware and will not try this version again."));
    display_unlock();
    snprintf(n->message_id, sizeof(n->message_id), "auto-update-rollback");
    n->timeout_sec = 0; /* until tapped */
    app_event_t ev = {.type = EV_NOTIFICATION_SHOW};
    ev.data.notification.notification = n;
    if (!app_events_publish(&ev, pdMS_TO_TICKS(1000))) {
        free(n);
        return false;
    }
    return true;
}

/* A wait that nothing in the next minutes can change: the setting is off, there is nothing newer, the version is blocked, an attempt was made
 * today, or the panel can never install (no checksum, an old bootloader, safe mode). */
static bool decision_is_settled(auto_update_decision_t decision)
{
    switch (decision) {
    case AUTO_UPDATE_WAIT_DISABLED:
    case AUTO_UPDATE_WAIT_NO_UPDATE:
    case AUTO_UPDATE_WAIT_BLOCKED_VERSION:
    case AUTO_UPDATE_WAIT_TRIED_TODAY:
    case AUTO_UPDATE_WAIT_NO_SHA256:
    case AUTO_UPDATE_WAIT_NO_ROLLBACK:
    case AUTO_UPDATE_WAIT_SAFE_MODE:
        return true;
    default:
        return false;
    }
}

/* How long to sleep after an evaluation: ten minutes while an install in the window only waits for a quiet moment (at most six tries a night), otherwise until the next
 * window starts (at most six hours, so a clock that gets set later or a changed time zone is noticed). Enabling the setting wakes the
 * task at once (auto_update_task_configure). */
static uint32_t next_poll_ms(auto_update_decision_t decision)
{
    const time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    if (local.tm_year < 124) {
        return AUTO_UPDATE_POLL_NO_CLOCK_MS;
    }
    const int minute = local.tm_hour * 60 + local.tm_min;
    const bool in_window = minute >= AUTO_UPDATE_WINDOW_START_MIN && minute < AUTO_UPDATE_WINDOW_END_MIN;
    if (in_window && !decision_is_settled(decision)) {
        return AUTO_UPDATE_POLL_MS;
    }
    const int until_window_min = (AUTO_UPDATE_WINDOW_START_MIN - minute + 24 * 60) % (24 * 60);
    const uint32_t until_window_ms = (uint32_t)(until_window_min == 0 ? 24 * 60 : until_window_min) * 60U * 1000U + 1000U;
    return until_window_ms < AUTO_UPDATE_POLL_MAX_MS ? until_window_ms : AUTO_UPDATE_POLL_MAX_MS;
}

/* What the last attempt came to, once that is known. True when there is nothing left to resolve. A freshly installed image only counts as installed
 * after it has proven itself (the bootloader's "pending verify" ends, util/ota_validate.c): one that crashes first is rolled back, and it is the previous
 * firmware that then finds the attempt unfinished and records the rollback. Resolving at the first boot would call a crashing image "installed". */
static bool resolve_attempt(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* Retry the same candidate after a failed commit; never discard its blocked version. */
    if ((!s_state_loaded && state_load() != ESP_OK) ||
        (s_state_loaded && !s_storage_ok && state_save_locked() != ESP_OK)) {
        s_decision = AUTO_UPDATE_WAIT_STORAGE;
        xSemaphoreGive(s_lock);
        return false;
    }
    bool done = s_state.attempt_tag[0] == '\0';
#if CONFIG_APP_AUTO_UPDATE_TEST_HOOK
    if (!done && s_test_hold) {
        xSemaphoreGive(s_lock);
        return false;
    }
#endif
    if (!done) {
        const esp_app_desc_t *app = esp_app_get_description();
        const bool running_attempt = strcmp(app->version, s_state.attempt_tag) == 0;
        if (running_attempt && strcmp(ota_validate_image_state(), "pending_verify") == 0) {
            xSemaphoreGive(s_lock);
            return false; /* still proving itself */
        }
        if (auto_update_state_resolve_boot(&s_state, app->version, reboot_log_last_run_failed(), wall_epoch())) {
            ESP_LOGW(TAG, "automatic update to %s: %s", s_state.result_tag, auto_update_result_name((auto_update_result_t)s_state.result));
            if (state_save_locked() != ESP_OK) { xSemaphoreGive(s_lock); return false; }
        }
        done = true;
    }
    xSemaphoreGive(s_lock);
    return done;
}

static void evaluate(void)
{
    char tag[AUTO_UPDATE_TAG_LEN] = {0};
    char url[APP_OTA_URL_MAX_LEN] = {0};
    bool has_sha = false;
    const bool available = update_check_get_available(tag, sizeof(tag), url, sizeof(url), &has_sha);

    const time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    const int32_t day = local.tm_year * 366 + local.tm_yday;
    uint8_t mac[6] = {0};
    (void)esp_efuse_mac_get_default(mac);
    uint32_t bootloader_version = 0;
    int8_t rssi = 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    auto_update_input_t in = {
        .enabled = s_enabled,
        .update_available = available,
        .sha256_known = has_sha,
        .bootloader_rollback = ota_validate_bootloader_info(&bootloader_version, NULL, 0) && ota_bootloader_has_rollback(bootloader_version),
        /* A freshly installed image that is still on probation is not replaced by another one. */
        .busy = api_ota_phase() != API_OTA_PHASE_IDLE && api_ota_phase() != API_OTA_PHASE_FAILED,
        .safe_mode = boot_guard_safe_mode(),
        .time_valid = local.tm_year >= 124,
        .local_minute_of_day = local.tm_hour * 60 + local.tm_min,
        .start_offset_min = auto_update_start_offset_min(((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5], day),
        .tried_today = s_state.last_attempt_day == day,
        .idle_s = user_activity_idle_s((uint32_t)(esp_timer_get_time() / 1000000)),
        .wifi_connected = wifi_mgr_is_connected(),
        .rssi_dbm = -127,
        .free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        .release_tag = tag,
        .blocked_tag = s_state.blocked_tag,
    };
    if (strcmp(ota_validate_image_state(), "pending_verify") == 0) {
        in.busy = true;
    }
#if CONFIG_APP_AUTO_UPDATE_TEST_HOOK
    if (s_test_force) {
        in.local_minute_of_day = AUTO_UPDATE_WINDOW_START_MIN + in.start_offset_min;
        in.idle_s = AUTO_UPDATE_IDLE_MIN_S;
        in.tried_today = false;
        in.time_valid = true;
    }
#endif
    if (in.wifi_connected && wifi_mgr_try_get_sta_rssi(&rssi) == ESP_OK) {
        in.rssi_dbm = rssi;
    }
    s_decision = auto_update_decide(&in);
    auto_update_decision_t decision = s_decision;
    if (decision == AUTO_UPDATE_GO) {
        auto_update_state_begin(&s_state, tag, wall_epoch(), day);
        if (state_save_locked() != ESP_OK) decision = s_decision = AUTO_UPDATE_WAIT_STORAGE;
#if CONFIG_APP_AUTO_UPDATE_TEST_HOOK
        s_test_force = false; /* one install per request */
#endif
    }
    xSemaphoreGive(s_lock);
    if (decision != AUTO_UPDATE_GO) {
        return;
    }

    ESP_LOGW(TAG, "installing %s automatically (idle, in the update window)", tag);
    esp_err_t err = api_ota_start_url(url);
    bool restart_pending = false;
    for (int waited = 0; err == ESP_OK && waited < AUTO_UPDATE_INSTALL_WATCH_MS; waited += 2000) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        const api_ota_phase_t phase = api_ota_phase();
        if (phase == API_OTA_PHASE_REBOOTING) {
            restart_pending = true; /* the attempt stays in NVS; the next boot resolves it */
            break;
        }
        if (phase != API_OTA_PHASE_RUNNING) {
            err = ESP_FAIL;
        }
    }
    if (!restart_pending && err == ESP_OK && api_ota_phase() == API_OTA_PHASE_RUNNING) {
        /* Cannot happen while the download is bounded below the watch time; if it does, the worker may still select the
         * image and reboot, so the durable attempt stays and the next boot resolves it. */
        ESP_LOGW(TAG, "automatic update to %s still running after the watch time; keeping the attempt", tag);
        return;
    }
    if (!restart_pending) {
        ESP_LOGW(TAG, "automatic update to %s did not complete: %s", tag, err == ESP_OK ? "timed out" : esp_err_to_name(err));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        auto_update_state_failed(&s_state, wall_epoch());
        state_save_locked();
        xSemaphoreGive(s_lock);
    }
}

static void auto_update_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(AUTO_UPDATE_FIRST_DELAY_MS));
    for (;;) {
        if (!resolve_attempt()) {
            vTaskDelay(pdMS_TO_TICKS(30 * 1000));
            continue;
        }
        char blocked[AUTO_UPDATE_TAG_LEN] = {0};
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool notice = s_state.notice_pending;
        snprintf(blocked, sizeof(blocked), "%s", s_state.result_tag);
        xSemaphoreGive(s_lock);
        if (notice && show_rollback_notice(blocked)) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            auto_update_state_ack_notice(&s_state);
            esp_err_t saved = state_save_locked();
            xSemaphoreGive(s_lock);
            if (saved != ESP_OK) continue;
        }
        evaluate();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const auto_update_decision_t last = s_decision;
        xSemaphoreGive(s_lock);
        /* A notification from auto_update_task_configure (the setting changed) ends the sleep early. */
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(next_poll_ms(last)));
    }
}

void auto_update_task_configure(bool enabled)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_enabled = enabled;
    if (!enabled) {
        s_decision = AUTO_UPDATE_WAIT_DISABLED;
    }
    xSemaphoreGive(s_lock);
    if (s_task != NULL) {
        xTaskNotifyGive(s_task); /* look again now, so the status and an enabled setting take effect without waiting */
    }
}

#if CONFIG_APP_AUTO_UPDATE_TEST_HOOK
void auto_update_test_hook(int force, bool reset, const char *attempt)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (reset) {
        auto_update_state_init(&s_state);
        state_save_locked();
    }
    if (attempt != NULL && attempt[0] != '\0') {
        auto_update_state_begin(&s_state, attempt, wall_epoch(), 0);
        state_save_locked();
        s_test_hold = true;
    }
    if (force >= 0) {
        s_test_force = force != 0;
    }
    xSemaphoreGive(s_lock);
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}
#endif

void auto_update_task_start(bool enabled)
{
    if (s_started) {
        return;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return;
    }
    s_enabled = enabled;
    state_load();
    (void)resolve_attempt(); /* a rollback shows at once; a new image that is still proving itself is resolved by the task */
    s_started = xTaskCreate(auto_update_task, "auto_update", AUTO_UPDATE_TASK_STACK, NULL, 2, &s_task) == pdPASS;
}

void auto_update_add_status_json(cJSON *root)
{
    cJSON *obj = cJSON_AddObjectToObject(root, "auto_update");
    if (obj == NULL || s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON_AddBoolToObject(obj, "enabled", s_enabled);
    cJSON_AddBoolToObject(obj, "storage_ok", s_storage_ok);
    cJSON_AddStringToObject(obj, "state", auto_update_decision_name(s_decision));
    cJSON_AddStringToObject(obj, "result", auto_update_result_name((auto_update_result_t)s_state.result));
    cJSON_AddStringToObject(obj, "result_version", s_state.result_tag);
    cJSON_AddNumberToObject(obj, "result_at", (double)s_state.result_epoch);
    cJSON_AddStringToObject(obj, "blocked_version", s_state.blocked_tag);
    cJSON_AddStringToObject(obj, "installing", s_state.attempt_tag);
#if CONFIG_APP_AUTO_UPDATE_TEST_HOOK
    cJSON_AddBoolToObject(obj, "test_hook", true);
    cJSON_AddBoolToObject(obj, "test_force", s_test_force);
#endif
    xSemaphoreGive(s_lock);
}
