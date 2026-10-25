/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "api/api_routes.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "app_config.h"
#include "ha/ha_client.h"
#include "ha/ha_model.h"
#include "util/url_decode.h"

#define API_ENTITIES_MAX_ITEMS_DEFAULT 128U
#define API_ENTITIES_MAX_ITEMS_MIN 8U
#define API_ENTITIES_MAX_ITEMS_MAX 128U

static void set_json_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

/* The first item of a page; 0 for a missing or invalid value. */
static size_t parse_offset(const char *value)
{
    char *end_ptr = NULL;
    long parsed = value[0] != '\0' ? strtol(value, &end_ptr, 10) : 0;
    return (end_ptr != NULL && *end_ptr == '\0' && parsed > 0) ? (size_t)parsed : 0;
}

static size_t parse_max_items(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return API_ENTITIES_MAX_ITEMS_DEFAULT;
    }

    char *end_ptr = NULL;
    long parsed = strtol(value, &end_ptr, 10);
    if (end_ptr == value || *end_ptr != '\0' || parsed <= 0) {
        return API_ENTITIES_MAX_ITEMS_DEFAULT;
    }
    if ((size_t)parsed < API_ENTITIES_MAX_ITEMS_MIN) {
        return API_ENTITIES_MAX_ITEMS_MIN;
    }
    if ((size_t)parsed > API_ENTITIES_MAX_ITEMS_MAX) {
        return API_ENTITIES_MAX_ITEMS_MAX;
    }
    return (size_t)parsed;
}

esp_err_t api_entities_get_handler(httpd_req_t *req)
{
    char domain[APP_MAX_NAME_LEN] = {0};
    char search[APP_MAX_NAME_LEN] = {0};
    char limit_raw[12] = {0};
    char offset_raw[12] = {0};

    int query_len = httpd_req_get_url_query_len(req);
    if (query_len > 0) {
        char *query = calloc((size_t)query_len + 1U, sizeof(char));
        if (query == NULL) {
            return httpd_resp_send_500(req);
        }
        if (httpd_req_get_url_query_str(req, query, query_len + 1) == ESP_OK) {
            httpd_query_key_value(query, "domain", domain, sizeof(domain));
            httpd_query_key_value(query, "search", search, sizeof(search));
            url_decode_in_place(search);
            httpd_query_key_value(query, "limit", limit_raw, sizeof(limit_raw));
            httpd_query_key_value(query, "offset", offset_raw, sizeof(offset_raw));
        }
        free(query);
    }

    const size_t max_items = parse_max_items(limit_raw);
    ha_entity_info_t *items = calloc(max_items, sizeof(ha_entity_info_t));
    if (items == NULL) {
        return httpd_resp_send_500(req);
    }
    const size_t offset = parse_offset(offset_raw);
    size_t total = 0;
    size_t count = ha_model_list_entities_page(
        domain[0] ? domain : NULL, search[0] ? search : NULL, offset, items, max_items, &total);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (root == NULL || arr == NULL) {
        free(items);
        cJSON_Delete(root);
        cJSON_Delete(arr);
        return httpd_resp_send_500(req);
    }

    for (size_t i = 0; i < count; i++) {
        cJSON *it = cJSON_CreateObject();
        cJSON_AddStringToObject(it, "id", items[i].id);
        cJSON_AddStringToObject(it, "name", items[i].name);
        cJSON_AddStringToObject(it, "domain", items[i].domain);
        cJSON_AddStringToObject(it, "unit", items[i].unit);
        cJSON_AddStringToObject(it, "device_class", items[i].device_class);
        cJSON_AddStringToObject(it, "state_class", items[i].state_class);
        cJSON_AddBoolToObject(it, "has_price_series", items[i].has_price_series);
        cJSON_AddBoolToObject(it, "has_solar_forecast", items[i].has_solar_forecast);
        cJSON_AddNumberToObject(it, "supported_features", (double)items[i].supported_features);
        cJSON *capabilities = cJSON_CreateObject();
        if (capabilities != NULL) {
            cJSON_AddBoolToObject(capabilities, "dimming", items[i].supports_dimming);
            cJSON_AddBoolToObject(capabilities, "color", items[i].supports_color);
            cJSON_AddBoolToObject(capabilities, "color_temp", items[i].supports_color_temp);
            cJSON_AddItemToObject(it, "capabilities", capabilities);
        }
        cJSON_AddStringToObject(it, "icon", items[i].icon);
        cJSON_AddItemToArray(arr, it);
    }
    free(items);

    cJSON_AddItemToObject(root, "items", arr);
    cJSON_AddNumberToObject(root, "count", (double)count);
    cJSON_AddNumberToObject(root, "offset", (double)offset);
    cJSON_AddNumberToObject(root, "total", (double)total);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return httpd_resp_send_500(req);
    }

    set_json_headers(req);
    esp_err_t err = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);
    return err;
}

esp_err_t api_light_entities_get_handler(httpd_req_t *req)
{
    char refresh_raw[8] = {0};
    char domain[APP_HA_DISCOVERY_DOMAIN_MAX_LEN] = "light";
    char search[APP_HA_DISCOVERY_SEARCH_MAX_LEN] = {0};
    char areas_only_raw[8] = {0};

    int query_len = httpd_req_get_url_query_len(req);
    if (query_len > 0) {
        char *query = calloc((size_t)query_len + 1U, sizeof(char));
        if (query == NULL) {
            return httpd_resp_send_500(req);
        }
        if (httpd_req_get_url_query_str(req, query, query_len + 1) == ESP_OK) {
            httpd_query_key_value(query, "refresh", refresh_raw, sizeof(refresh_raw));
            httpd_query_key_value(query, "areas_only", areas_only_raw, sizeof(areas_only_raw));
            httpd_query_key_value(query, "domain", domain, sizeof(domain));
            httpd_query_key_value(query, "search", search, sizeof(search));
            url_decode_in_place(search);
        }
        free(query);
    }

    bool refresh = strcmp(refresh_raw, "1") == 0 || strcmp(refresh_raw, "true") == 0;
    /* domain=room&areas_only=1: just the area/floor registry, no entity discovery. */
    bool areas_only = strcmp(domain, "room") == 0 &&
        (strcmp(areas_only_raw, "1") == 0 || strcmp(areas_only_raw, "true") == 0);
    char *payload = NULL;
    esp_err_t err = areas_only ? ha_client_get_room_areas_json(refresh, &payload)
                               : ha_client_get_domain_entities_json(domain, search, refresh, &payload);
    if (err != ESP_OK || payload == NULL) {
        return httpd_resp_send_500(req);
    }

    set_json_headers(req);
    err = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);
    return err;
}

esp_err_t api_light_entities_delete_handler(httpd_req_t *req)
{
    char domain[APP_HA_DISCOVERY_DOMAIN_MAX_LEN] = "light";
    char search[APP_HA_DISCOVERY_SEARCH_MAX_LEN] = {0};

    int query_len = httpd_req_get_url_query_len(req);
    if (query_len > 0) {
        char *query = calloc((size_t)query_len + 1U, sizeof(char));
        if (query == NULL) {
            return httpd_resp_send_500(req);
        }
        if (httpd_req_get_url_query_str(req, query, query_len + 1) == ESP_OK) {
            httpd_query_key_value(query, "domain", domain, sizeof(domain));
            httpd_query_key_value(query, "search", search, sizeof(search));
            url_decode_in_place(search);
        }
        free(query);
    }

    esp_err_t err = ha_client_cancel_domain_entities(domain, search);
    if (err != ESP_OK) {
        return httpd_resp_send_500(req);
    }

    set_json_headers(req);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
