/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "settings/runtime_secrets.h"
#include "util/sha256_soft.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"

#define SECRET_NAMESPACE "runtime_sec"
#define SECRET_RECORD_VERSION 1U

typedef struct {
    uint32_t version;
    uint32_t generation;
    uint8_t scope[32];
    char wifi_password[APP_WIFI_PASSWORD_MAX_LEN];
    char ha_access_token[APP_HA_ACCESS_TOKEN_MAX_LEN];
    char mqtt_password[APP_MQTT_PASSWORD_MAX_LEN];
} secret_record_t;

static const char *record_key(uint32_t generation)
{
    return (generation & 1U) != 0U ? "secrets_1" : "secrets_0";
}

static void secret_scope(const runtime_settings_t *settings, uint8_t out[32])
{
    sha256_soft_t hash;
    sha256_soft_init(&hash);
    const char *fields[] = {settings->wifi_ssid, settings->wifi_bssid, settings->ha_ws_url,
        settings->mqtt_broker_host, settings->mqtt_username};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        sha256_soft_update(&hash, (const uint8_t *)fields[i], strlen(fields[i]) + 1);
    char transport[24];
    snprintf(transport, sizeof(transport), "%d:%d", settings->mqtt_broker_port, settings->mqtt_tls_enabled ? 1 : 0);
    sha256_soft_update(&hash, (const uint8_t *)transport, strlen(transport) + 1);
    sha256_soft_final(&hash, out);
}

static esp_err_t legacy_string(nvs_handle_t handle, const char *key, char *out, size_t capacity, bool recover_retired)
{
    char value[APP_HA_ACCESS_TOKEN_MAX_LEN] = {0};
    if (capacity > sizeof(value)) return ESP_ERR_INVALID_SIZE;
    size_t length = capacity;
    esp_err_t err = nvs_get_str(handle, key, value, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) { out[0] = '\0'; return ESP_OK; }
    if (err != ESP_OK) return err;
    if (length == 0 || length > capacity || value[length - 1] != '\0') return ESP_ERR_INVALID_RESPONSE;
    /* Empty present keys are our migration tombstones. Older supported writers erase cleared keys. */
    if (!recover_retired || value[0] != '\0') strlcpy(out, value, capacity);
    return ESP_OK;
}

esp_err_t runtime_secrets_load(uint32_t generation, runtime_settings_t *settings)
{
    if (settings == NULL) return ESP_ERR_INVALID_ARG;
    secret_record_t *record = calloc(1, sizeof(*record));
    if (record == NULL) return ESP_ERR_NO_MEM;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SECRET_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND && generation == 0U) {
        settings->wifi_password[0] = settings->ha_access_token[0] = settings->mqtt_password[0] = '\0';
        free(record);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        free(record);
        return err;
    }
    size_t length = sizeof(*record);
    err = nvs_get_blob(handle, record_key(generation), record, &length);
    uint8_t scope[32];
    secret_scope(settings, scope);
    bool saved = err == ESP_OK;
    /* Alternating generations eventually reuse slot zero. It is then no legacy snapshot. */
    if (saved && generation == 0U && length == sizeof(*record) && record->version == SECRET_RECORD_VERSION && record->generation != 0U) {
        saved = false;
        err = ESP_ERR_NVS_NOT_FOUND;
    }
    if (saved && (length != sizeof(*record) || record->version != SECRET_RECORD_VERSION || record->generation != generation))
        err = ESP_ERR_INVALID_RESPONSE;
    bool matches = saved && err == ESP_OK && memcmp(scope, record->scope, sizeof(scope)) == 0;
    if (generation == 0U && (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_OK)) {
        /* A legacy writer may have replaced the public file after an OTA rollback. Its current keys win;
         * retired keys use the snapshot only when it belongs to these exact connection targets. */
        if (!matches) memset(record, 0, sizeof(*record));
        err = legacy_string(handle, "wifi_pwd", record->wifi_password, sizeof(record->wifi_password), matches);
        if (err == ESP_OK) err = legacy_string(handle, "ha_token", record->ha_access_token, sizeof(record->ha_access_token), matches);
        if (err == ESP_OK) err = legacy_string(handle, "mqtt_pwd", record->mqtt_password, sizeof(record->mqtt_password), matches);
    } else if (err == ESP_OK && !matches) err = ESP_ERR_INVALID_RESPONSE;
    nvs_close(handle);
    if (err == ESP_OK &&
        (memchr(record->wifi_password, '\0', sizeof(record->wifi_password)) == NULL ||
         memchr(record->ha_access_token, '\0', sizeof(record->ha_access_token)) == NULL ||
         memchr(record->mqtt_password, '\0', sizeof(record->mqtt_password)) == NULL)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        strlcpy(settings->wifi_password, record->wifi_password, sizeof(settings->wifi_password));
        strlcpy(settings->ha_access_token, record->ha_access_token, sizeof(settings->ha_access_token));
        strlcpy(settings->mqtt_password, record->mqtt_password, sizeof(settings->mqtt_password));
    }
    free(record);
    return err;
}

esp_err_t runtime_secrets_stage(uint32_t generation, const runtime_settings_t *settings)
{
    if (settings == NULL) return ESP_ERR_INVALID_ARG;
    secret_record_t *record = calloc(1, sizeof(*record));
    if (record == NULL) return ESP_ERR_NO_MEM;
    record->version = SECRET_RECORD_VERSION;
    record->generation = generation;
    secret_scope(settings, record->scope);
    strlcpy(record->wifi_password, settings->wifi_password, sizeof(record->wifi_password));
    strlcpy(record->ha_access_token, settings->ha_access_token, sizeof(record->ha_access_token));
    strlcpy(record->mqtt_password, settings->mqtt_password, sizeof(record->mqtt_password));
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SECRET_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, record_key(generation), record, sizeof(*record));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    free(record);
    return err;
}

esp_err_t runtime_secrets_retire_legacy(const runtime_settings_t *previous, const runtime_settings_t *next)
{
    if (next == NULL) return ESP_ERR_INVALID_ARG;
    bool changed[] = {
        previous == NULL || strcmp(previous->wifi_ssid, next->wifi_ssid) != 0 ||
            strcmp(previous->wifi_bssid, next->wifi_bssid) != 0 || strcmp(previous->wifi_password, next->wifi_password) != 0,
        previous == NULL || strcmp(previous->ha_ws_url, next->ha_ws_url) != 0 ||
            strcmp(previous->ha_access_token, next->ha_access_token) != 0,
        previous == NULL || strcmp(previous->mqtt_broker_host, next->mqtt_broker_host) != 0 ||
            previous->mqtt_broker_port != next->mqtt_broker_port || previous->mqtt_tls_enabled != next->mqtt_tls_enabled ||
            strcmp(previous->mqtt_username, next->mqtt_username) != 0 || strcmp(previous->mqtt_password, next->mqtt_password) != 0,
    };
    if (!changed[0] && !changed[1] && !changed[2]) return ESP_OK;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SECRET_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    const char *keys[] = {"wifi_pwd", "ha_token", "mqtt_pwd"};
    for (size_t i = 0; err == ESP_OK && i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if (changed[i]) err = nvs_set_str(handle, keys[i], "");
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
