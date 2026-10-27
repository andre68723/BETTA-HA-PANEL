/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/theme/theme_store.h"
#include "ha/ha_client.h"
#include "ha/ha_auth_state.h"
#include "esp_timer.h"
#include "ha/ha_cover_fetcher.h"
#include "ha/ha_energy_model.h"
#include "ui/ui_bindings.h"
#include "util/flash_gate.h"
#include "host_adapters.h"
#include "src/libs/lodepng/lodepng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ha_state_t s_states[APP_HA_MAX_STATES];
static size_t s_state_count;

/* Host persistence has no shared flash mapping or OTA verification window. */
bool flash_gate_enter(uint32_t timeout_ms) { (void)timeout_ms; return true; }
void flash_gate_leave(void) {}

void sim_host_state_add(const ha_state_t *state)
{
    for (size_t i = 0; i < s_state_count; i++) {
        if (strcmp(s_states[i].entity_id, state->entity_id) == 0) {
            s_states[i] = *state;
            return;
        }
    }
    if (s_state_count < APP_HA_MAX_STATES) s_states[s_state_count++] = *state;
}

bool ha_model_get_state(const char *entity_id, ha_state_t *out_state)
{
    for (size_t i = 0; i < s_state_count; i++) {
        if (strcmp(s_states[i].entity_id, entity_id) == 0) {
            if (out_state != NULL) *out_state = s_states[i];
            return true;
        }
    }
    return false;
}

bool ha_model_get_entity_name(const char *entity_id, char *out, size_t out_len)
{
    for (size_t i = 0; i < s_state_count; i++) {
        if (strcmp(s_states[i].entity_id, entity_id) != 0) continue;
        cJSON *attrs = cJSON_Parse(s_states[i].attributes_json);
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(attrs, "friendly_name"));
        bool found = name != NULL && name[0] != '\0';
        if (found) snprintf(out, out_len, "%s", name);
        cJSON_Delete(attrs);
        return found;
    }
    return false;
}

esp_err_t ui_bindings_toggle_entity(const char *entity_id) { (void)entity_id; return ESP_OK; }
esp_err_t ui_bindings_set_entity_power(const char *entity_id, bool on)
{ (void)entity_id; (void)on; return ESP_OK; }
static unsigned s_slider_sets;
static int s_slider_last = -1;
unsigned sim_host_slider_sets(void) { return s_slider_sets; }
int sim_host_slider_last(void) { return s_slider_last; }
esp_err_t ui_bindings_set_slider_value(const char *entity_id, int value)
{ (void)entity_id; s_slider_sets++; s_slider_last = value; return ESP_OK; }
esp_err_t ui_bindings_set_climate_target_c(const char *entity_id, float celsius)
{ (void)entity_id; (void)celsius; return ESP_OK; }
esp_err_t ui_bindings_set_light_color_temp_kelvin(const char *entity_id, int kelvin)
{ (void)entity_id; (void)kelvin; return ESP_OK; }
esp_err_t ui_bindings_set_light_rgb_color(const char *entity_id, uint8_t r, uint8_t g, uint8_t b)
{ (void)entity_id; (void)r; (void)g; (void)b; return ESP_OK; }
esp_err_t ui_bindings_media_player_action(const char *entity_id, ui_bindings_media_action_t action)
{ (void)entity_id; (void)action; return ESP_OK; }
esp_err_t ui_bindings_media_player_set_shuffle(const char *entity_id, bool shuffle)
{ (void)entity_id; (void)shuffle; return ESP_OK; }
esp_err_t ui_bindings_media_player_set_repeat(const char *entity_id, const char *repeat_mode)
{ (void)entity_id; (void)repeat_mode; return ESP_OK; }
esp_err_t ui_bindings_media_player_play_favorite(const char *entity_id, const char *favorite_item_id)
{ (void)entity_id; (void)favorite_item_id; return ESP_OK; }

esp_err_t theme_store_load_custom(const char *id, theme_entry_t *out_entry)
{
    (void)id;
    (void)out_entry;
    return ESP_ERR_NOT_FOUND;
}

static bool s_online;

void sim_host_set_online(bool online) { s_online = online; }

bool ha_client_is_connected(void)
{
    return s_online;
}

bool ha_client_is_initial_sync_done(void) { return s_online; }
bool ha_client_reconnect_available(void)
{
    return (!s_online || ha_auth_state_blocked()) && ha_auth_state_retry_available(esp_timer_get_time() / 1000);
}
bool ha_client_request_reconnect(void)
{
    return ha_client_reconnect_available() && ha_auth_state_request_retry(esp_timer_get_time() / 1000);
}

static sim_service_calls_t s_calls;
static esp_err_t s_service_result = ESP_OK;

void sim_host_service_reset(esp_err_t result)
{
    memset(&s_calls, 0, sizeof(s_calls));
    s_service_result = result;
}

const sim_service_calls_t *sim_host_service_calls(void) { return &s_calls; }

esp_err_t ha_client_call_service(const char *domain, const char *service, const char *json_service_data)
{
    s_calls.count++;
    snprintf(s_calls.domain, sizeof(s_calls.domain), "%s", domain);
    snprintf(s_calls.service, sizeof(s_calls.service), "%s", service);
    snprintf(s_calls.data, sizeof(s_calls.data), "%s", json_service_data != NULL ? json_service_data : "");
    fprintf(stderr, "Simulator action: %s.%s (no Home Assistant call sent)\n", domain, service);
    return s_service_result;
}

static bool s_response_capture, s_response_sync_failure;
static ha_client_response_cb_t s_response_cb;
static void *s_response_user;
void sim_host_response_mode(bool capture, bool synchronous_failure)
{
    s_response_capture = capture;
    s_response_sync_failure = synchronous_failure;
}
void sim_host_response_deliver(bool success, cJSON *result)
{
    ha_client_response_cb_t cb = s_response_cb;
    void *user = s_response_user;
    s_response_cb = NULL; s_response_user = NULL;
    if (cb) cb(success, result, user);
}

esp_err_t ha_client_call_service_with_response(const char *domain, const char *service,
                                                const char *target_entity_id, const char *json_service_data,
                                                ha_client_response_cb_t cb, void *user)
{
    (void)domain;
    (void)service;
    (void)target_entity_id;
    (void)json_service_data;
    if (s_response_sync_failure) { cb(false, NULL, user); return ESP_FAIL; }
    if (s_response_capture && s_response_cb == NULL) {
        s_response_cb = cb; s_response_user = user;
        return ESP_OK;
    }
    return ESP_FAIL;
}

void ha_client_cancel_pending_responses_for_user(void *user)
{
    (void)user;
}

/* "file:<path>" pictures come from a local PNG (the gallery's fake doorbell camera), scaled to the
 * requested size and handed to the caller at once; anything else fails like an offline panel. */
esp_err_t ha_cover_fetcher_request(const char *entity_picture, int target_w, int target_h,
                                   ha_cover_cb_t cb, void *user)
{
    if (entity_picture == NULL || strncmp(entity_picture, "file:", 5) != 0 || cb == NULL ||
        target_w <= 0 || target_h <= 0) {
        return ESP_FAIL;
    }
    FILE *file = fopen(entity_picture + 5, "rb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *png = size > 0 ? malloc((size_t)size) : NULL;
    const bool read_ok = png != NULL && fread(png, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    /* LVGL's lodepng decodes into an lv_draw_buf_t (4 bytes per pixel, R first), not a raw buffer. */
    unsigned char *out = NULL;
    unsigned src_w = 0;
    unsigned src_h = 0;
    const bool decoded = read_ok && lodepng_decode32(&out, &src_w, &src_h, png, (size_t)size) == 0 && out != NULL;
    free(png);
    if (!decoded) {
        return ESP_FAIL;
    }
    lv_draw_buf_t *decoded_buf = (lv_draw_buf_t *)out;
    const uint8_t *rgba = decoded_buf->data;
    const uint32_t src_stride = decoded_buf->header.stride;
    /* RGB565, like the firmware's fetcher. */
    uint16_t *pixels = malloc((size_t)target_w * (size_t)target_h * 2);
    if (pixels == NULL) {
        lv_draw_buf_destroy(decoded_buf);
        return ESP_FAIL;
    }
    for (int y = 0; y < target_h; y++) {
        for (int x = 0; x < target_w; x++) {
            const uint8_t *s = rgba + (size_t)(y * (int)src_h / target_h) * src_stride +
                               (size_t)(x * (int)src_w / target_w) * 4;
            pixels[(size_t)y * (size_t)target_w + (size_t)x] =
                (uint16_t)(((s[0] >> 3) << 11) | ((s[1] >> 2) << 5) | (s[2] >> 3));
        }
    }
    lv_draw_buf_destroy(decoded_buf);
    ha_cover_result_t result = {0};
    result.image.header.magic = LV_IMAGE_HEADER_MAGIC;
    result.image.header.cf = LV_COLOR_FORMAT_RGB565;
    result.image.header.w = (uint32_t)target_w;
    result.image.header.h = (uint32_t)target_h;
    result.image.header.stride = (uint32_t)target_w * 2;
    result.image.data_size = (uint32_t)target_w * (uint32_t)target_h * 2;
    result.image.data = (const uint8_t *)pixels;
    result.source_w = src_w;
    result.source_h = src_h;
    result.valid = true;
    cb(user, &result);
    return ESP_OK;
}

void display_note_activity(void) {}
bool display_is_ready(void) { return true; }
bool display_lock(uint32_t timeout_ms) { (void)timeout_ms; return true; }
void display_unlock(void) {}

/* Optional fixture snapshot exercises the same generated HA Energy page as firmware. */
static ha_energy_snapshot_t s_energy;
void sim_host_energy_set(const ha_energy_snapshot_t *snapshot) { s_energy = *snapshot; }
bool ha_energy_model_get_snapshot(ha_energy_snapshot_t *out_snapshot)
{
    *out_snapshot = s_energy;
    return s_energy.available;
}

void ha_cover_fetcher_cancel(void *user) { (void)user; }
void ha_cover_result_release(ha_cover_result_t *result)
{
    if (result && result->valid) { free((void *)result->image.data); memset(result, 0, sizeof(*result)); }
}

/* Energy dashboard sources of the simulator: a gas and a water counter and two solar productions. */
size_t ha_client_energy_source_ids(ha_energy_resource_t resource, char ids[][APP_MAX_ENTITY_ID_LEN], size_t max_ids)
{
    static const char *const gas[] = {"sensor.sim_gas_total"};
    static const char *const water[] = {"sensor.sim_water_total"};
    static const char *const solar[] = {"sensor.sim_solar_a", "sensor.sim_solar_b"};
    const char *const *list = NULL;
    size_t count = 0;
    if (resource == HA_ENERGY_RES_GAS) { list = gas; count = 1; }
    else if (resource == HA_ENERGY_RES_WATER) { list = water; count = 1; }
    else if (resource == HA_ENERGY_RES_SOLAR) { list = solar; count = 2; }
    size_t n = 0;
    for (; n < count && n < max_ids; n++) snprintf(ids[n], APP_MAX_ENTITY_ID_LEN, "%s", list[n]);
    return n;
}
