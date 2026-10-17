/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "net/update_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "app_config.h"
#include "build_stamp.h"
#include "net/update_check_parse.h"
#include "net/wifi_mgr.h"

#define TAG_UPDATE "update_check"

#define UPDATE_CHECK_FIRST_DELAY_S 150
#define UPDATE_CHECK_INTERVAL_S (24 * 3600)
#define UPDATE_CHECK_JITTER_S 1800
#define UPDATE_CHECK_RETRY_S 3600       /* after a failed request */
#define UPDATE_CHECK_WAIT_NET_S 300     /* no Wi-Fi or no clock yet */
#define UPDATE_CHECK_MANUAL_GAP_S 60
#define UPDATE_CHECK_TASK_STACK 8192 /* PSRAM; TLS handshake + JSON left 1.7 KB of 6 KB */
#define UPDATE_CHECK_TASK_PRIO 2
/* A release with four assets is about 15 KB of JSON, so a /releases page of five stays below 80 KB. */
#define UPDATE_CHECK_RESPONSE_MAX (128 * 1024)
/* Any time before this means SNTP has not set the clock yet. */
#define UPDATE_CHECK_VALID_EPOCH 1767225600 /* 2026-01-01 */

typedef enum {
    UPDATE_STATE_IDLE = 0,
    UPDATE_STATE_CHECKING,
    UPDATE_STATE_OK,
    UPDATE_STATE_ERROR,
} update_state_t;

typedef struct {
    bool enabled;
    bool include_prereleases;
    update_state_t state;
    char error[64];
    int64_t checked_at; /* wall clock, 0 = never */
    update_check_release_t release;
} update_status_t;

static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;
static update_status_t s_status;
static int64_t s_next_due_us;
static int64_t s_last_run_us;

static int64_t jitter_s(int max_s)
{
    return (int64_t)(esp_random() % (uint32_t)max_s);
}

static void schedule_in(int64_t seconds)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_next_due_us = esp_timer_get_time() + seconds * 1000000;
    xSemaphoreGive(s_mutex);
}

static void *cjson_psram_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : malloc(size);
}

/* __DATE__ " " __TIME__ of this build, read in the panel's time zone; 0 if it cannot be read. */
static int64_t build_epoch(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month[4] = {0};
    struct tm tm = {0};
    if (sscanf(app_build_stamp, "%3s %d %d %d:%d:%d", month, &tm.tm_mday, &tm.tm_year, &tm.tm_hour, &tm.tm_min,
            &tm.tm_sec) != 6) {
        return 0;
    }
    const char *found = strstr(months, month);
    if (found == NULL || strlen(month) != 3) {
        return 0;
    }
    tm.tm_mon = (int)((found - months) / 3);
    tm.tm_year -= 1900;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    return t > 0 ? (int64_t)t : 0;
}

static void set_result(update_state_t state, const char *error, const update_check_release_t *release)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.state = state;
    snprintf(s_status.error, sizeof(s_status.error), "%s", error != NULL ? error : "");
    if (state == UPDATE_STATE_OK) {
        s_status.checked_at = (int64_t)time(NULL);
        s_status.release = *release;
    }
    xSemaphoreGive(s_mutex);
}

/* GET url into a PSRAM buffer; *out_status is the HTTP status. The caller frees *out_body. */
static esp_err_t fetch(const char *url, char **out_body, size_t *out_len, int *out_status)
{
    *out_body = NULL;
    *out_len = 0;
    *out_status = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    esp_http_client_set_header(client, "User-Agent", "BETTA-HA-PANEL-update-check");

    char *body = heap_caps_malloc(UPDATE_CHECK_RESPONSE_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_err_t err = body != NULL ? esp_http_client_open(client, 0) : ESP_ERR_NO_MEM;
    size_t len = 0;
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        *out_status = esp_http_client_get_status_code(client);
        while (len < UPDATE_CHECK_RESPONSE_MAX) {
            int r = esp_http_client_read(client, body + len, (int)(UPDATE_CHECK_RESPONSE_MAX - len));
            if (r < 0) {
                err = ESP_FAIL;
                break;
            }
            if (r == 0) {
                break;
            }
            len += (size_t)r;
        }
        if (err == ESP_OK && len >= UPDATE_CHECK_RESPONSE_MAX && !esp_http_client_is_complete_data_received(client)) {
            err = ESP_ERR_INVALID_SIZE;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        free(body);
        return err;
    }
    body[len] = '\0';
    *out_body = body;
    *out_len = len;
    return ESP_OK;
}

static void run_check(void)
{
    bool include_prereleases = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    include_prereleases = s_status.include_prereleases;
    s_status.state = UPDATE_STATE_CHECKING;
    xSemaphoreGive(s_mutex);

    const esp_app_desc_t *app = esp_app_get_description();
    const char *variant = update_check_variant_for_project(app->project_name);
    if (variant == NULL) {
        set_result(UPDATE_STATE_ERROR, "unknown firmware variant", NULL);
        schedule_in(UPDATE_CHECK_INTERVAL_S);
        return;
    }

    /* /releases/latest never returns a pre-release; the first page of /releases does, newest first. */
    char url[128];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases%s", APP_OTA_RELEASE_REPO,
        include_prereleases ? "?per_page=5" : "/latest");
    char *body = NULL;
    size_t len = 0;
    int status = 0;
    esp_err_t err = fetch(url, &body, &len, &status);
    s_last_run_us = esp_timer_get_time();

    update_check_release_t release = {0};
    if (err == ESP_OK && status == 404 && !include_prereleases) {
        /* No stable release published yet: a valid answer with nothing to offer. */
        set_result(UPDATE_STATE_OK, NULL, &release);
        schedule_in(UPDATE_CHECK_INTERVAL_S + jitter_s(UPDATE_CHECK_JITTER_S));
    } else if (err != ESP_OK || status != 200) {
        char msg[64];
        if (err != ESP_OK) {
            snprintf(msg, sizeof(msg), "request failed: %s", esp_err_to_name(err));
        } else {
            snprintf(msg, sizeof(msg), "GitHub answered HTTP %d", status);
        }
        ESP_LOGW(TAG_UPDATE, "%s", msg);
        set_result(UPDATE_STATE_ERROR, msg, NULL);
        schedule_in(UPDATE_CHECK_RETRY_S + jitter_s(300));
    } else if (!update_check_parse_releases(body, len, variant, include_prereleases, app->version, build_epoch(),
                   &release)) {
        set_result(UPDATE_STATE_ERROR, "unreadable release list", NULL);
        schedule_in(UPDATE_CHECK_RETRY_S + jitter_s(300));
    } else {
        ESP_LOGI(TAG_UPDATE, "Latest %s release: %s%s", variant, release.found ? release.tag : "none",
            release.newer ? " (newer)" : "");
        set_result(UPDATE_STATE_OK, NULL, &release);
        schedule_in(UPDATE_CHECK_INTERVAL_S + jitter_s(UPDATE_CHECK_JITTER_S));
    }
    free(body);
}

static void update_check_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        const int64_t due_us = s_next_due_us;
        xSemaphoreGive(s_mutex);
        const int64_t wait_us = due_us - esp_timer_get_time();
        const TickType_t ticks = wait_us <= 0 ? 0 : pdMS_TO_TICKS(wait_us > 60000000 ? 60000 : wait_us / 1000);
        const bool manual = ulTaskNotifyTake(pdTRUE, ticks) > 0;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        const bool due = s_status.enabled && esp_timer_get_time() >= s_next_due_us;
        xSemaphoreGive(s_mutex);
        if (!manual && !due) {
            continue;
        }
        if (!wifi_mgr_is_connected() || time(NULL) < UPDATE_CHECK_VALID_EPOCH) {
            set_result(UPDATE_STATE_ERROR, "waiting for Wi-Fi and time", NULL);
            schedule_in(UPDATE_CHECK_WAIT_NET_S);
            continue;
        }
        run_check();
    }
}

esp_err_t update_check_start(bool enabled, bool include_prereleases)
{
    if (s_task != NULL) {
        return ESP_OK;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    /* Same PSRAM hooks as ha_client_start(): a release list parses into a few hundred cJSON nodes,
     * which should not come from internal RAM on a panel without Home Assistant configured. Both
     * allocators are released with free(), so switching hooks while another task uses cJSON is safe. */
    cJSON_Hooks hooks = {.malloc_fn = cjson_psram_malloc, .free_fn = free};
    cJSON_InitHooks(&hooks);

    s_status.enabled = enabled;
    s_status.include_prereleases = include_prereleases;
    schedule_in(UPDATE_CHECK_FIRST_DELAY_S + jitter_s(60));
    BaseType_t ok = xTaskCreateWithCaps(update_check_task, "update_check", UPDATE_CHECK_TASK_STACK, NULL,
        UPDATE_CHECK_TASK_PRIO, &s_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        s_task = NULL;
        ESP_LOGW(TAG_UPDATE, "Task not started");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void update_check_configure(bool enabled, bool include_prereleases)
{
    if (s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool channel_changed = s_status.include_prereleases != include_prereleases;
    const bool turned_on = enabled && !s_status.enabled;
    s_status.enabled = enabled;
    s_status.include_prereleases = include_prereleases;
    if (!enabled || channel_changed) {
        memset(&s_status.release, 0, sizeof(s_status.release));
        s_status.checked_at = 0;
        s_status.state = UPDATE_STATE_IDLE;
        s_status.error[0] = '\0';
    }
    xSemaphoreGive(s_mutex);
    if (enabled && (channel_changed || turned_on)) {
        schedule_in(5);
    }
}

esp_err_t update_check_request_now(void)
{
    if (s_task == NULL || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool busy = s_status.state == UPDATE_STATE_CHECKING;
    xSemaphoreGive(s_mutex);
    if (busy || (s_last_run_us > 0 && esp_timer_get_time() - s_last_run_us < UPDATE_CHECK_MANUAL_GAP_S * 1000000LL)) {
        return ESP_ERR_INVALID_STATE;
    }
    xTaskNotifyGive(s_task);
    return ESP_OK;
}

static const char *state_text(update_state_t state)
{
    switch (state) {
    case UPDATE_STATE_CHECKING:
        return "checking";
    case UPDATE_STATE_OK:
        return "ok";
    case UPDATE_STATE_ERROR:
        return "error";
    default:
        return "idle";
    }
}

void update_check_add_status_json(cJSON *root)
{
    if (root == NULL || s_mutex == NULL) {
        return;
    }
    update_status_t status;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    status = s_status;
    xSemaphoreGive(s_mutex);

    cJSON *update = cJSON_AddObjectToObject(root, "update");
    if (update == NULL) {
        return;
    }
    cJSON_AddBoolToObject(update, "enabled", status.enabled);
    cJSON_AddBoolToObject(update, "include_prereleases", status.include_prereleases);
    cJSON_AddStringToObject(update, "state", state_text(status.state));
    cJSON_AddStringToObject(update, "error", status.error);
    cJSON_AddNumberToObject(update, "checked_at", (double)status.checked_at);
    cJSON_AddBoolToObject(update, "found", status.release.found);
    cJSON_AddBoolToObject(update, "available", status.release.newer);
    cJSON_AddStringToObject(update, "version", status.release.tag);
    cJSON_AddStringToObject(update, "url", status.release.asset_url);
    cJSON_AddBoolToObject(update, "prerelease", status.release.prerelease);
    cJSON_AddNumberToObject(update, "published_at", (double)status.release.published);
}

bool update_check_expected_sha256(const char *url, char out[65])
{
    if (s_mutex == NULL || url == NULL || out == NULL) {
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool match = s_status.release.found && s_status.release.asset_sha256[0] != '\0' &&
                       strcmp(url, s_status.release.asset_url) == 0;
    if (match) {
        memcpy(out, s_status.release.asset_sha256, 65);
    }
    xSemaphoreGive(s_mutex);
    return match;
}

bool update_check_get_available(char *tag, size_t tag_len, char *url, size_t url_len, bool *has_sha256)
{
    if (s_mutex == NULL || tag == NULL || url == NULL || has_sha256 == NULL) {
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool newer = s_status.enabled && s_status.release.newer;
    if (newer) {
        snprintf(tag, tag_len, "%s", s_status.release.tag);
        snprintf(url, url_len, "%s", s_status.release.asset_url);
        *has_sha256 = s_status.release.asset_sha256[0] != '\0';
    }
    xSemaphoreGive(s_mutex);
    return newer;
}

bool update_check_available_version(char *out, size_t out_len)
{
    if (s_mutex == NULL || out == NULL || out_len == 0) {
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool newer = s_status.enabled && s_status.release.newer;
    if (newer) {
        snprintf(out, out_len, "%s", s_status.release.tag);
    }
    xSemaphoreGive(s_mutex);
    return newer;
}
