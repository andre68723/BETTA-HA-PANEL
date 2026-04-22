/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "api/api_routes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

#include "app_events.h"
#include "ui/theme/theme_palette.h"
#include "ui/theme/theme_store.h"

static const char *TAG = "api_theme";

static void api_theme_set_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

static esp_err_t api_theme_send_error(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_500(req);
    }
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", (message != NULL) ? message : "error");
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return httpd_resp_send_500(req);
    }
    api_theme_set_headers(req);
    if (status != NULL) {
        httpd_resp_set_status(req, status);
    }
    esp_err_t err = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);
    return err;
}

static bool api_theme_query_get_id(httpd_req_t *req, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return false;
    }
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0) {
        return false;
    }
    char *qbuf = calloc(qlen + 1U, sizeof(char));
    if (qbuf == NULL) {
        return false;
    }
    if (httpd_req_get_url_query_str(req, qbuf, qlen + 1U) != ESP_OK) {
        free(qbuf);
        return false;
    }
    esp_err_t err = httpd_query_key_value(qbuf, "id", out, out_len);
    free(qbuf);
    return err == ESP_OK && out[0] != '\0';
}

static char *api_theme_read_body(httpd_req_t *req, size_t max_len)
{
    if (req->content_len == 0 || req->content_len > max_len) {
        return NULL;
    }
    char *buf = calloc(req->content_len + 1U, sizeof(char));
    if (buf == NULL) {
        return NULL;
    }
    size_t total = 0;
    while (total < req->content_len) {
        int r = httpd_req_recv(req, buf + total, req->content_len - total);
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        total += (size_t)r;
    }
    buf[total] = '\0';
    return buf;
}

static cJSON *api_theme_entry_to_summary(const theme_entry_t *entry)
{
    cJSON *item = cJSON_CreateObject();
    if (item == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(item, "id", entry->id);
    cJSON_AddStringToObject(item, "name", entry->name);
    cJSON_AddBoolToObject(item, "builtin", entry->builtin);
    return item;
}

/* GET /api/themes
 * -> { themes:[{id,name,builtin}...], active_id:"..." } */
static esp_err_t api_themes_list_get(httpd_req_t *req)
{
    const theme_entry_t *builtins[16];
    size_t nbuilt = theme_palette_builtin_list(builtins);

    char custom_ids[APP_MAX_CUSTOM_THEMES][APP_MAX_THEME_ID_LEN];
    size_t ncustom = 0;
    (void)theme_store_list_custom(custom_ids, APP_MAX_CUSTOM_THEMES, &ncustom);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_500(req);
    }
    cJSON *arr = cJSON_AddArrayToObject(root, "themes");
    for (size_t i = 0; i < nbuilt; i++) {
        cJSON *item = api_theme_entry_to_summary(builtins[i]);
        if (item != NULL) {
            cJSON_AddItemToArray(arr, item);
        }
    }
    for (size_t i = 0; i < ncustom; i++) {
        theme_entry_t entry = {0};
        if (theme_store_load_custom(custom_ids[i], &entry) != ESP_OK) {
            continue;
        }
        cJSON *item = api_theme_entry_to_summary(&entry);
        if (item != NULL) {
            cJSON_AddItemToArray(arr, item);
        }
    }
    cJSON_AddStringToObject(root, "active_id", theme_palette_active_id());
    cJSON_AddStringToObject(root, "active_name", theme_palette_active_name());

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return httpd_resp_send_500(req);
    }
    api_theme_set_headers(req);
    esp_err_t err = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);
    return err;
}

/* GET /api/themes/active -> full active palette as exportable JSON */
static esp_err_t api_themes_active_get(httpd_req_t *req)
{
    theme_entry_t entry = {0};
    snprintf(entry.id, sizeof(entry.id), "%s", theme_palette_active_id());
    snprintf(entry.name, sizeof(entry.name), "%s", theme_palette_active_name());
    entry.builtin = theme_palette_find_builtin(entry.id) != NULL;
    entry.palette = *theme_palette_active();
    char *json = theme_palette_to_json(&entry);
    if (json == NULL) {
        return httpd_resp_send_500(req);
    }
    api_theme_set_headers(req);
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

/* PUT /api/themes/active  body: {"id":"..."} */
static esp_err_t api_themes_active_put(httpd_req_t *req)
{
    char *body = api_theme_read_body(req, APP_THEME_MAX_JSON_LEN);
    if (body == NULL) {
        return api_theme_send_error(req, "400 Bad Request", "invalid body");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) {
        return api_theme_send_error(req, "400 Bad Request", "invalid JSON");
    }
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (!cJSON_IsString(id) || id->valuestring == NULL || id->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return api_theme_send_error(req, "400 Bad Request", "id required");
    }
    char id_copy[APP_MAX_THEME_ID_LEN] = {0};
    snprintf(id_copy, sizeof(id_copy), "%s", id->valuestring);
    cJSON_Delete(root);

    esp_err_t err = theme_palette_activate_by_id(id_copy);
    if (err != ESP_OK) {
        return api_theme_send_error(req, "404 Not Found", "theme not found");
    }
    (void)theme_store_set_active_id(id_copy);
    {
        app_event_t event = {.type = EV_LAYOUT_UPDATED};
        (void)app_events_publish(&event, pdMS_TO_TICKS(20));
    }
    ESP_LOGI(TAG, "active theme set to %s", id_copy);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "active_id", theme_palette_active_id());
    char *payload = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    api_theme_set_headers(req);
    esp_err_t send_err = httpd_resp_sendstr(req, payload != NULL ? payload : "{\"ok\":true}");
    if (payload != NULL) {
        cJSON_free(payload);
    }
    return send_err;
}

/* GET /api/themes/get?id=...  -> full theme JSON for export */
static esp_err_t api_themes_get(httpd_req_t *req)
{
    char id[APP_MAX_THEME_ID_LEN] = {0};
    if (!api_theme_query_get_id(req, id, sizeof(id))) {
        return api_theme_send_error(req, "400 Bad Request", "id query required");
    }
    const theme_entry_t *builtin = theme_palette_find_builtin(id);
    theme_entry_t entry = {0};
    if (builtin != NULL) {
        entry = *builtin;
    } else if (theme_store_load_custom(id, &entry) != ESP_OK) {
        return api_theme_send_error(req, "404 Not Found", "theme not found");
    }
    char *json = theme_palette_to_json(&entry);
    if (json == NULL) {
        return httpd_resp_send_500(req);
    }
    api_theme_set_headers(req);
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

/* PUT /api/themes/custom  body: full theme JSON  (upsert) */
static esp_err_t api_themes_custom_put(httpd_req_t *req)
{
    char *body = api_theme_read_body(req, APP_THEME_MAX_JSON_LEN);
    if (body == NULL) {
        return api_theme_send_error(req, "400 Bad Request", "invalid body");
    }
    theme_entry_t entry = {0};
    bool ok = theme_palette_from_json(body, &entry);
    free(body);
    if (!ok) {
        return api_theme_send_error(req, "400 Bad Request", "invalid theme JSON");
    }
    if (theme_palette_find_builtin(entry.id) != NULL) {
        return api_theme_send_error(req, "409 Conflict", "id collides with builtin");
    }
    esp_err_t err = theme_store_save_custom(&entry);
    if (err != ESP_OK) {
        return api_theme_send_error(req,
            (err == ESP_ERR_NO_MEM) ? "507 Insufficient Storage" : "400 Bad Request",
            (err == ESP_ERR_NO_MEM) ? "too many custom themes" : "save failed");
    }
    ESP_LOGI(TAG, "custom theme saved: %s", entry.id);

    /* If the active theme is being updated, re-apply it. */
    if (strcmp(entry.id, theme_palette_active_id()) == 0) {
        theme_palette_set_active(&entry.palette, entry.id, entry.name);
        app_event_t event = {.type = EV_LAYOUT_UPDATED};
        (void)app_events_publish(&event, pdMS_TO_TICKS(20));
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "id", entry.id);
    char *payload = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    api_theme_set_headers(req);
    esp_err_t send_err = httpd_resp_sendstr(req, payload != NULL ? payload : "{\"ok\":true}");
    if (payload != NULL) {
        cJSON_free(payload);
    }
    return send_err;
}

/* DELETE /api/themes/custom?id=... */
static esp_err_t api_themes_custom_delete(httpd_req_t *req)
{
    char id[APP_MAX_THEME_ID_LEN] = {0};
    if (!api_theme_query_get_id(req, id, sizeof(id))) {
        return api_theme_send_error(req, "400 Bad Request", "id query required");
    }
    if (theme_palette_find_builtin(id) != NULL) {
        return api_theme_send_error(req, "409 Conflict", "cannot delete builtin");
    }
    esp_err_t err = theme_store_delete_custom(id);
    if (err == ESP_ERR_NOT_FOUND) {
        return api_theme_send_error(req, "404 Not Found", "theme not found");
    }
    if (err != ESP_OK) {
        return api_theme_send_error(req, "500 Internal Server Error", "delete failed");
    }
    /* If the deleted theme was active, fall back to the default. */
    if (strcmp(id, theme_palette_active_id()) == 0) {
        (void)theme_palette_activate_by_id("dark_v2");
        (void)theme_store_set_active_id("dark_v2");
        app_event_t event = {.type = EV_LAYOUT_UPDATED};
        (void)app_events_publish(&event, pdMS_TO_TICKS(20));
    }

    api_theme_set_headers(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* Single dispatcher for /api/themes/custom so we can serve PUT + DELETE
 * through the same registered URI (httpd registers one handler per
 * method; we expose both). */
esp_err_t api_themes_list_get_handler(httpd_req_t *req)
{
    return api_themes_list_get(req);
}
esp_err_t api_themes_active_get_handler(httpd_req_t *req)
{
    return api_themes_active_get(req);
}
esp_err_t api_themes_active_put_handler(httpd_req_t *req)
{
    return api_themes_active_put(req);
}
esp_err_t api_themes_get_handler(httpd_req_t *req)
{
    return api_themes_get(req);
}
esp_err_t api_themes_custom_put_handler(httpd_req_t *req)
{
    return api_themes_custom_put(req);
}
esp_err_t api_themes_custom_delete_handler(httpd_req_t *req)
{
    return api_themes_custom_delete(req);
}
