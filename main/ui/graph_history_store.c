/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/graph_history_store.h"
#include "util/atomic_file.h"
#include "app_config.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define HISTORY_QUOTA_MAX (1024U * 1024U)
#define HISTORY_FREE_RESERVE (512U * 1024U)
#define HISTORY_PATH_MAX 160

void graph_history_store_id(const char *id, char *out, size_t capacity)
{
    if (out == NULL || capacity == 0) return;
    size_t n = 0;
    if (id != NULL) for (size_t i = 0; id[i] != '\0' && n + 1 < capacity; ++i) {
        char c = id[i];
        out[n++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') ? c : '_';
    }
    out[n] = '\0';
    if (n == 0) snprintf(out, capacity, "graph");
}

static bool valid_layout(const cJSON *layout)
{
    const cJSON *pages = cJSON_GetObjectItemCaseSensitive(layout, "pages");
    if (!cJSON_IsObject(layout) || !cJSON_IsArray(pages)) return false;
    const cJSON *page;
    cJSON_ArrayForEach(page, pages) {
        if (!cJSON_IsObject(page)) return false;
        const cJSON *widgets = cJSON_GetObjectItemCaseSensitive(page, "widgets");
        if (widgets != NULL && !cJSON_IsArray(widgets)) return false;
        const cJSON *widget;
        cJSON_ArrayForEach(widget, widgets) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(widget, "id");
            const cJSON *type = cJSON_GetObjectItemCaseSensitive(widget, "type");
            if (!cJSON_IsString(id) || !cJSON_IsString(type)) return false;
        }
    }
    return true;
}

static bool referenced(const cJSON *layout, const char *filename)
{
    const cJSON *page, *widget;
    cJSON_ArrayForEach(page, cJSON_GetObjectItemCaseSensitive(layout, "pages")) {
        cJSON_ArrayForEach(widget, cJSON_GetObjectItemCaseSensitive(page, "widgets")) {
            const cJSON *type = cJSON_GetObjectItemCaseSensitive(widget, "type");
            if (strcmp(type->valuestring, "graph") != 0 ||
                cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(widget, "graph_ha_history"))) continue;
            char id[APP_MAX_WIDGET_ID_LEN], name[APP_MAX_WIDGET_ID_LEN + 6];
            graph_history_store_id(cJSON_GetObjectItemCaseSensitive(widget, "id")->valuestring, id, sizeof(id));
            snprintf(name, sizeof(name), "%s.grph", id);
            if (strcmp(filename, name) == 0) return true;
        }
    }
    return false;
}

static bool history_name(const char *name)
{
    size_t n = strlen(name);
    return (n > 5 && strcmp(name + n - 5, ".grph") == 0) ||
           (n > 9 && strcmp(name + n - 9, ".grph.tmp") == 0);
}

esp_err_t graph_history_store_cleanup(const char *directory, const cJSON *layout)
{
    if (directory == NULL || !valid_layout(layout)) return ESP_ERR_INVALID_ARG;
    DIR *dir = opendir(directory);
    if (dir == NULL) return errno == ENOENT ? ESP_OK : ESP_FAIL;
    esp_err_t err = ESP_OK;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!history_name(entry->d_name) || referenced(layout, entry->d_name)) continue;
        char path[HISTORY_PATH_MAX];
        int n = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof(path)) { err = ESP_ERR_INVALID_SIZE; break; }
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (remove(path) != 0) { err = ESP_FAIL; break; }
    }
    closedir(dir);
    return err;
}

esp_err_t graph_history_store_save(const char *directory, const char *path, const cJSON *layout,
    const void *data, size_t length, size_t fs_total, size_t fs_used)
{
    if (directory == NULL || path == NULL || !valid_layout(layout)) return ESP_ERR_INVALID_ARG;
    size_t prefix = strlen(directory);
    if (strncmp(path, directory, prefix) != 0 || path[prefix] != '/' || strchr(path + prefix + 1, '/') != NULL ||
        !referenced(layout, path + prefix + 1)) return ESP_ERR_NOT_FOUND; /* stale queued job */
    size_t quota = fs_total / 4U;
    if (quota > HISTORY_QUOTA_MAX) quota = HISTORY_QUOTA_MAX;
    if (fs_used > fs_total || length > quota || fs_total - fs_used < HISTORY_FREE_RESERVE ||
        length > fs_total - fs_used - HISTORY_FREE_RESERVE) return ESP_ERR_NO_MEM;
    if (mkdir(directory, 0775) != 0 && errno != EEXIST) return ESP_FAIL;
    DIR *dir = opendir(directory);
    if (dir == NULL) return ESP_FAIL;
    size_t occupied = 0;
    esp_err_t err = ESP_OK;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!history_name(entry->d_name)) continue;
        char other[HISTORY_PATH_MAX];
        int n = snprintf(other, sizeof(other), "%s/%s", directory, entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof(other)) { err = ESP_ERR_INVALID_SIZE; break; }
        if (strcmp(other, path) == 0) continue;
        struct stat st;
        if (stat(other, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) { err = ESP_FAIL; break; }
        if ((size_t)st.st_size > quota - occupied) { err = ESP_ERR_NO_MEM; break; }
        occupied += (size_t)st.st_size;
    }
    closedir(dir);
    if (err != ESP_OK) return err;
    if (length > quota - occupied) return ESP_ERR_NO_MEM;
    return atomic_file_write_locked(path, data, length);
}
