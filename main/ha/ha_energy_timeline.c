/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_energy_timeline.h"

#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "ha/ha_auth_state.h"
#include "ha/ha_client.h"

#define ET_TAG "ha_energy_tl"
#define ET_TASK_STACK 5120
#define ET_REFRESH_S 900
#define ET_RETRY_S 60
#define ET_RETRY_MAX_S 300
#define ET_BUSY_RETRY_S 3
#define ET_REPLY_TIMEOUT_MS 20000
#define ET_STALE_AFTER_S 2400
#define ET_GATE_WAIT_S 45
#define ET_GATE_POLL_MS 250

typedef struct {
    bool used;
    char widget_id[APP_MAX_WIDGET_ID_LEN];
    char stat_id[ET_CH_COUNT][APP_MAX_ENTITY_ID_LEN];
    int64_t next_fetch_s;   /* monotonic seconds */
    uint32_t failures;
    ha_energy_timeline_snapshot_t *snap; /* PSRAM: about 1.5 KB per widget, kept off the scarce internal RAM */
} tl_widget_t;

static tl_widget_t s_slots[HA_ET_MAX_WIDGETS];
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_reply;
static TaskHandle_t s_task;

/* The request in flight: only the task and the WS callback touch these, one request at a time. */
static et_series_t *s_work;
static volatile uint32_t s_generation;
static char s_request_stat_id[APP_MAX_ENTITY_ID_LEN];
static et_channel_t s_request_channel;
static volatile bool s_reply_ok;
static volatile size_t s_reply_parts;

static int64_t mono_s(void)
{
    return esp_timer_get_time() / 1000000;
}

static void reply_cb(bool success, cJSON *result, void *user)
{
    if ((uint32_t)(uintptr_t)user != s_generation) {
        return; /* a request the task already gave up on */
    }
    s_reply_ok = success && cJSON_IsObject(result);
    s_reply_parts = 0;
    if (s_reply_ok) {
        s_reply_parts = et_add_stats_json(s_work, s_request_channel,
                                          cJSON_GetObjectItemCaseSensitive(result, s_request_stat_id));
    }
    xSemaphoreGive(s_reply);
}

/* Builds and sends one statistics request; the client takes the command whether or not it is sent. */
static esp_err_t send_channel_request(const char *stat_id, const char *start_iso, const char *end_iso, uint32_t generation)
{
    cJSON *cmd = cJSON_CreateObject();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(cmd, "type", "recorder/statistics_during_period");
    cJSON_AddStringToObject(cmd, "start_time", start_iso);
    cJSON_AddStringToObject(cmd, "end_time", end_iso);
    cJSON_AddStringToObject(cmd, "period", "5minute");
    cJSON *ids = cJSON_AddArrayToObject(cmd, "statistic_ids");
    cJSON *types = cJSON_AddArrayToObject(cmd, "types");
    cJSON *units = cJSON_AddObjectToObject(cmd, "units");
    if (ids == NULL || types == NULL || units == NULL) {
        cJSON_Delete(cmd);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToArray(ids, cJSON_CreateString(stat_id));
    cJSON_AddItemToArray(types, cJSON_CreateString("change"));
    cJSON_AddStringToObject(units, "energy", "kWh");
    return ha_client_send_command_with_response(cmd, reply_cb, (void *)(uintptr_t)generation);
}

static esp_err_t request_channel(et_channel_t channel, const char *stat_id, int64_t start_epoch, int64_t end_epoch)
{
    char start_iso[32], end_iso[32];
    struct tm start_tm, end_tm;
    time_t start_t = (time_t)start_epoch, end_t = (time_t)end_epoch;
    if (gmtime_r(&start_t, &start_tm) == NULL || gmtime_r(&end_t, &end_tm) == NULL) {
        return ESP_FAIL;
    }
    strftime(start_iso, sizeof(start_iso), "%Y-%m-%dT%H:%M:%SZ", &start_tm);
    strftime(end_iso, sizeof(end_iso), "%Y-%m-%dT%H:%M:%SZ", &end_tm);

    s_request_channel = channel;
    strlcpy(s_request_stat_id, stat_id, sizeof(s_request_stat_id));
    /* The heavy gate is busy while another WebSocket request runs and for a short cooldown after each one. Wait for
     * it here instead of giving up: starting the whole widget over would request the counters that already arrived
     * again and never get past the cooldown. */
    const int64_t give_up_s = mono_s() + ET_GATE_WAIT_S;
    esp_err_t err;
    for (;;) {
        s_reply_ok = false;
        s_reply_parts = 0;
        xSemaphoreTake(s_reply, 0);
        const uint32_t generation = ++s_generation;
        err = send_channel_request(stat_id, start_iso, end_iso, generation);
        if (err != ESP_ERR_INVALID_STATE || mono_s() >= give_up_s || !ha_client_is_connected()) {
            if (err != ESP_OK) {
                return err;
            }
            if (xSemaphoreTake(s_reply, pdMS_TO_TICKS(ET_REPLY_TIMEOUT_MS)) != pdTRUE) {
                s_generation++; /* the late reply is ignored */
                return ESP_ERR_TIMEOUT;
            }
            return s_reply_ok ? ESP_OK : ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(ET_GATE_POLL_MS));
    }
}

static void publish(size_t slot, ha_energy_timeline_state_t state, int64_t now_epoch)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_slots[slot].snap->series = *s_work;
    s_slots[slot].snap->state = state;
    s_slots[slot].snap->updated_epoch = now_epoch;
    s_slots[slot].snap->version++;
    s_slots[slot].failures = 0;
    s_slots[slot].next_fetch_s = mono_s() + ET_REFRESH_S;
    xSemaphoreGive(s_mutex);
}

static void fetch_widget(size_t slot)
{
    char ids[ET_CH_COUNT][APP_MAX_ENTITY_ID_LEN];
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(ids, s_slots[slot].stat_id, sizeof(ids));
    xSemaphoreGive(s_mutex);

    const int64_t now = (int64_t)time(NULL);
    if (now < 1700000000) {
        return; /* the clock is not set yet */
    }
    bool configured[ET_CH_COUNT];
    for (int c = 0; c < ET_CH_COUNT; c++) {
        configured[c] = ids[c][0] != '\0';
    }
    et_series_init(s_work, now, configured);

    size_t import_parts = 0;
    for (int c = 0; c < ET_CH_COUNT; c++) {
        if (!configured[c]) {
            continue;
        }
        esp_err_t err = request_channel((et_channel_t)c, ids[c], s_work->window_start, now);
        if (err != ESP_OK) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            uint32_t failures = ++s_slots[slot].failures;
            int64_t wait = err == ESP_ERR_INVALID_STATE ? ET_BUSY_RETRY_S : (int64_t)ET_RETRY_S * failures;
            if (wait > ET_RETRY_MAX_S) wait = ET_RETRY_MAX_S;
            s_slots[slot].next_fetch_s = mono_s() + wait;
            xSemaphoreGive(s_mutex);
            ESP_LOGW(ET_TAG, "%s: request for %s failed (%s), retry in %lld s", s_slots[slot].widget_id, ids[c],
                     esp_err_to_name(err), (long long)wait);
            return;
        }
        if (c == ET_CH_GRID_IMPORT) {
            import_parts = s_reply_parts;
        }
    }
    publish(slot, import_parts > 0 ? HA_ET_READY : HA_ET_NO_STATISTICS, now);
    ESP_LOGI(ET_TAG, "%s: %u grid import parts", s_slots[slot].widget_id, (unsigned)import_parts);
}

static void timeline_task(void *arg)
{
    (void)arg;
    s_work = heap_caps_calloc(1, sizeof(*s_work), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_work == NULL) {
        s_work = heap_caps_calloc(1, sizeof(*s_work), MALLOC_CAP_8BIT);
    }
    if (s_work == NULL) {
        ESP_LOGE(ET_TAG, "no memory for the work buffer");
        vTaskDeleteWithCaps(NULL);
        return;
    }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!ha_client_is_connected() || ha_auth_state_blocked()) {
            continue;
        }
        for (size_t i = 0; i < HA_ET_MAX_WIDGETS; i++) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            bool due = s_slots[i].used && mono_s() >= s_slots[i].next_fetch_s;
            xSemaphoreGive(s_mutex);
            if (due) {
                fetch_widget(i);
            }
        }
    }
}

static bool valid_id(const char *id)
{
    return id != NULL && id[0] != '\0' && strlen(id) < APP_MAX_ENTITY_ID_LEN && strchr(id, '.') != NULL;
}

esp_err_t ha_energy_timeline_register(const char *widget_id, const ha_energy_timeline_config_t *config)
{
    if (widget_id == NULL || config == NULL || strlen(widget_id) >= APP_MAX_WIDGET_ID_LEN ||
        !valid_id(config->stat_id[ET_CH_GRID_IMPORT])) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        s_reply = xSemaphoreCreateBinary();
        if (s_mutex == NULL || s_reply == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t result = ESP_ERR_NO_MEM;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int free_slot = -1;
    for (int i = 0; i < HA_ET_MAX_WIDGETS; i++) {
        if (s_slots[i].used && strcmp(s_slots[i].widget_id, widget_id) == 0) {
            free_slot = i; /* registered again after a layout rebuild: keep the data, take the new ids */
            break;
        }
        if (!s_slots[i].used && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot >= 0) {
        tl_widget_t *s = &s_slots[free_slot];
        bool fresh = !s->used;
        bool changed = fresh;
        for (int c = 0; c < ET_CH_COUNT; c++) {
            const char *id = valid_id(config->stat_id[c]) ? config->stat_id[c] : "";
            changed = changed || strcmp(s->stat_id[c], id) != 0;
            strlcpy(s->stat_id[c], id, sizeof(s->stat_id[c]));
        }
        if (fresh) {
            if (s->snap == NULL) {
                s->snap = heap_caps_calloc(1, sizeof(*s->snap), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (s->snap == NULL) {
                    s->snap = heap_caps_calloc(1, sizeof(*s->snap), MALLOC_CAP_8BIT);
                }
            }
            if (s->snap == NULL) {
                xSemaphoreGive(s_mutex);
                return ESP_ERR_NO_MEM;
            }
            memset(s->snap, 0, sizeof(*s->snap));
            strlcpy(s->widget_id, widget_id, sizeof(s->widget_id));
        }
        if (changed) {
            s->snap->state = HA_ET_LOADING;
            s->snap->updated_epoch = 0;
            s->next_fetch_s = 0;
            s->failures = 0;
            s->snap->version++;
        }
        s->used = true;
        result = ESP_OK;
    }
    xSemaphoreGive(s_mutex);
    if (result == ESP_OK && s_task == NULL) {
        if (xTaskCreateWithCaps(timeline_task, "ha_energy_tl", ET_TASK_STACK, NULL, 4, &s_task,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) { /* stack in PSRAM: it used 2 of 5 KB */
            s_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return result;
}

void ha_energy_timeline_unregister(const char *widget_id)
{
    if (widget_id == NULL || s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < HA_ET_MAX_WIDGETS; i++) {
        if (s_slots[i].used && strcmp(s_slots[i].widget_id, widget_id) == 0) {
            s_slots[i].used = false;
        }
    }
    xSemaphoreGive(s_mutex);
}

bool ha_energy_timeline_get(const char *widget_id, ha_energy_timeline_snapshot_t *out)
{
    if (widget_id == NULL || out == NULL || s_mutex == NULL) {
        return false;
    }
    bool found = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < HA_ET_MAX_WIDGETS; i++) {
        if (s_slots[i].used && strcmp(s_slots[i].widget_id, widget_id) == 0) {
            *out = *s_slots[i].snap;
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_mutex);
    if (found && out->state == HA_ET_READY && (int64_t)time(NULL) - out->updated_epoch > ET_STALE_AFTER_S) {
        out->state = HA_ET_STALE;
    }
    return found;
}
