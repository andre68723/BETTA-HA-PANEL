/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "layout/layout_store.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "app_config.h"
#include "layout/layout_validate.h"
#include "util/atomic_file.h"
#include "util/log_tags.h"

static const char *s_default_layout =
    "{"
    "\"version\":1,"
    "\"pages\":["
    "{"
    "\"id\":\"living\","
    "\"title\":\"Living Room\","
    "\"widgets\":[]"
    "}"
    "]"
    "}";

esp_err_t layout_store_save(const char *json)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = strlen(json);
    if (atomic_file_write(APP_LAYOUT_PATH, json, len) != ESP_OK) {
        ESP_LOGE(TAG_LAYOUT, "Failed to write layout file: %s", APP_LAYOUT_PATH);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_LAYOUT, "Saved layout (%u bytes)", (unsigned)len);
    return ESP_OK;
}

esp_err_t layout_store_load(char **json_out)
{
    if (json_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *json_out = NULL;

    FILE *f = fopen(APP_LAYOUT_PATH, "rb");
    if (f == NULL) {
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    long size = ftell(f);
    if (size <= 0 || size > APP_LAYOUT_MAX_JSON_LEN) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(f);

    char *buf = (char *)calloc((size_t)size + 1U, sizeof(char));
    if (buf == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(buf, 1U, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size) {
        free(buf);
        return ESP_FAIL;
    }

    *json_out = buf;
    return ESP_OK;
}

const char *layout_store_default_json(void)
{
    return s_default_layout;
}

esp_err_t layout_store_init(void)
{
    char *existing = NULL;
    esp_err_t err = layout_store_load(&existing);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG_LAYOUT, "Layout file absent, writing defaults");
        return layout_store_save(s_default_layout);
    }
    if (err != ESP_OK || existing == NULL) {
        ESP_LOGW(TAG_LAYOUT, "Layout read failed, preserving stored file");
        free(existing);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    layout_validation_result_t *validation = calloc(1, sizeof(layout_validation_result_t));
    if (validation == NULL) {
        free(existing);
        ESP_LOGW(TAG_LAYOUT, "Validation allocation failed, preserving existing layout");
        return ESP_OK;
    }

    bool valid = layout_validate_json(existing, validation);
    free(validation);
    free(existing);

    if (valid) {
        return ESP_OK;
    }

    /* A false validation result may also be caused by cJSON allocation failure.
     * Never destroy the source file as an implicit recovery action. */
    ESP_LOGW(TAG_LAYOUT, "Stored layout failed validation, preserving file");
    return ESP_OK;
}

cJSON *layout_store_load_parsed(void)
{
    char *json = NULL;
    if (layout_store_load(&json) != ESP_OK || json == NULL) {
        free(json);
        json = strdup(layout_store_default_json());
        if (json == NULL) {
            return NULL;
        }
    }
    cJSON *root = cJSON_Parse(json);
    free(json);
    return root;
}
