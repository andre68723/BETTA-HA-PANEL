/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ui/theme/theme_store.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"

#include "util/log_tags.h"

static const char *TAG = "theme_store";

static bool theme_store_is_valid_id(const char *id)
{
    if (id == NULL) {
        return false;
    }
    size_t len = strlen(id);
    if (len == 0 || len >= APP_MAX_THEME_ID_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool theme_store_ensure_dir(void)
{
    struct stat st = {0};
    if (stat(APP_THEME_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;
    }
    (void)mkdir(APP_THEME_DIR, 0775);
    return stat(APP_THEME_DIR, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool theme_store_build_path(const char *id, char *out, size_t out_len)
{
    if (id == NULL || out == NULL || out_len == 0) {
        return false;
    }
    int n = snprintf(out, out_len, "%s/%s.json", APP_THEME_DIR, id);
    return n > 0 && (size_t)n < out_len;
}

static esp_err_t theme_store_read_active_id(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    FILE *f = fopen(APP_THEME_ACTIVE_PATH, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t n = fread(out, 1U, out_len - 1U, f);
    fclose(f);
    out[n] = '\0';
    /* trim whitespace/newline */
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
        out[--n] = '\0';
    }
    return ESP_OK;
}

esp_err_t theme_store_set_active_id(const char *id)
{
    if (!theme_store_is_valid_id(id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!theme_store_ensure_dir()) {
        return ESP_FAIL;
    }
    FILE *f = fopen(APP_THEME_ACTIVE_PATH, "wb");
    if (f == NULL) {
        return ESP_FAIL;
    }
    size_t len = strlen(id);
    size_t w = fwrite(id, 1U, len, f);
    fclose(f);
    return (w == len) ? ESP_OK : ESP_FAIL;
}

esp_err_t theme_store_init(void)
{
    (void)theme_store_ensure_dir();

    char id[APP_MAX_THEME_ID_LEN] = {0};
    if (theme_store_read_active_id(id, sizeof(id)) == ESP_OK && id[0] != '\0') {
        esp_err_t err = theme_palette_activate_by_id(id);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "active theme: %s", id);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "persisted theme %s not found, using default", id);
    }
    /* default already active (dark_v2) */
    return ESP_OK;
}

esp_err_t theme_store_load_custom(const char *id, theme_entry_t *out_entry)
{
    if (!theme_store_is_valid_id(id) || out_entry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (theme_palette_find_builtin(id) != NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    char path[128] = {0};
    if (!theme_store_build_path(id, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    long size = ftell(f);
    if (size <= 0 || (size_t)size > APP_THEME_MAX_JSON_LEN) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(f);
    char *buf = calloc((size_t)size + 1U, sizeof(char));
    if (buf == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t r = fread(buf, 1U, (size_t)size, f);
    fclose(f);
    if (r != (size_t)size) {
        free(buf);
        return ESP_FAIL;
    }
    bool ok = theme_palette_from_json(buf, out_entry);
    free(buf);
    if (!ok) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Force the loaded id to match the file name. */
    snprintf(out_entry->id, sizeof(out_entry->id), "%s", id);
    out_entry->builtin = false;
    return ESP_OK;
}

esp_err_t theme_store_save_custom(const theme_entry_t *entry)
{
    if (entry == NULL || !theme_store_is_valid_id(entry->id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (theme_palette_find_builtin(entry->id) != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!theme_store_ensure_dir()) {
        return ESP_FAIL;
    }
    /* Enforce max count by listing existing ids */
    char existing[APP_MAX_CUSTOM_THEMES][APP_MAX_THEME_ID_LEN];
    size_t count = 0;
    (void)theme_store_list_custom(existing, APP_MAX_CUSTOM_THEMES, &count);
    bool found = false;
    for (size_t i = 0; i < count; i++) {
        if (strncmp(existing[i], entry->id, APP_MAX_THEME_ID_LEN) == 0) {
            found = true;
            break;
        }
    }
    if (!found && count >= APP_MAX_CUSTOM_THEMES) {
        return ESP_ERR_NO_MEM;
    }
    char *json = theme_palette_to_json(entry);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    char path[128] = {0};
    if (!theme_store_build_path(entry->id, path, sizeof(path))) {
        free(json);
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        free(json);
        return ESP_FAIL;
    }
    size_t len = strlen(json);
    size_t w = fwrite(json, 1U, len, f);
    fclose(f);
    free(json);
    return (w == len) ? ESP_OK : ESP_FAIL;
}

esp_err_t theme_store_delete_custom(const char *id)
{
    if (!theme_store_is_valid_id(id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (theme_palette_find_builtin(id) != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    char path[128] = {0};
    if (!theme_store_build_path(id, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (remove(path) != 0) {
        return (errno == ENOENT) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t theme_store_list_custom(
    char (*out_ids)[APP_MAX_THEME_ID_LEN], size_t max_ids, size_t *out_count)
{
    if (out_ids == NULL || max_ids == 0 || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (!theme_store_ensure_dir()) {
        return ESP_OK;
    }
    DIR *dir = opendir(APP_THEME_DIR);
    if (dir == NULL) {
        return (errno == ENOENT) ? ESP_OK : ESP_FAIL;
    }
    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL && *out_count < max_ids) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len <= 5 || strcmp(name + len - 5, ".json") != 0) {
            continue;
        }
        char id[APP_MAX_THEME_ID_LEN] = {0};
        size_t base = len - 5;
        if (base >= sizeof(id)) {
            continue;
        }
        memcpy(id, name, base);
        id[base] = '\0';
        if (!theme_store_is_valid_id(id)) {
            continue;
        }
        if (theme_palette_find_builtin(id) != NULL) {
            continue;
        }
        strlcpy(out_ids[*out_count], id, APP_MAX_THEME_ID_LEN);
        (*out_count)++;
    }
    closedir(dir);
    return ESP_OK;
}
