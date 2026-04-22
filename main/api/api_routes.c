/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "api/api_routes.h"
#include "api/http_guard.h"

#include "esp_check.h"

static esp_err_t guarded_api_layout_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_layout_get_handler);
}

static esp_err_t guarded_api_layout_put(httpd_req_t *req)
{
    return http_guard_handle(req, api_layout_put_handler);
}

static esp_err_t guarded_api_entities_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_entities_get_handler);
}

static esp_err_t guarded_api_light_entities_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_light_entities_get_handler);
}

static esp_err_t guarded_api_ha_energy_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_ha_energy_get_handler);
}

static esp_err_t guarded_api_state_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_state_get_handler);
}

static esp_err_t guarded_api_settings_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_settings_get_handler);
}

static esp_err_t guarded_api_settings_put(httpd_req_t *req)
{
    return http_guard_handle(req, api_settings_put_handler);
}

static esp_err_t guarded_api_i18n_languages_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_i18n_languages_get_handler);
}

static esp_err_t guarded_api_i18n_effective_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_i18n_effective_get_handler);
}

static esp_err_t guarded_api_i18n_custom_put(httpd_req_t *req)
{
    return http_guard_handle(req, api_i18n_custom_put_handler);
}

static esp_err_t guarded_api_wifi_scan_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_wifi_scan_get_handler);
}

static esp_err_t guarded_api_version_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_version_get_handler);
}

static esp_err_t guarded_api_screenshot_bmp_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_screenshot_bmp_get_handler);
}

static esp_err_t guarded_api_ota_status_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_ota_status_get_handler);
}

static esp_err_t guarded_api_ota_url_post(httpd_req_t *req)
{
    return http_guard_handle(req, api_ota_url_post_handler);
}

static esp_err_t guarded_api_ota_upload_post(httpd_req_t *req)
{
    return http_guard_handle(req, api_ota_upload_post_handler);
}

static esp_err_t guarded_api_themes_list_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_themes_list_get_handler);
}
static esp_err_t guarded_api_themes_active_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_themes_active_get_handler);
}
static esp_err_t guarded_api_themes_active_put(httpd_req_t *req)
{
    return http_guard_handle(req, api_themes_active_put_handler);
}
static esp_err_t guarded_api_themes_get(httpd_req_t *req)
{
    return http_guard_handle(req, api_themes_get_handler);
}
static esp_err_t guarded_api_themes_custom_put(httpd_req_t *req)
{
    return http_guard_handle(req, api_themes_custom_put_handler);
}
static esp_err_t guarded_api_themes_custom_delete(httpd_req_t *req)
{
    return http_guard_handle(req, api_themes_custom_delete_handler);
}

esp_err_t api_routes_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_uri_t get_layout = {
        .uri = "/api/layout",
        .method = HTTP_GET,
        .handler = guarded_api_layout_get,
        .user_ctx = NULL,
    };
    httpd_uri_t put_layout = {
        .uri = "/api/layout",
        .method = HTTP_PUT,
        .handler = guarded_api_layout_put,
        .user_ctx = NULL,
    };
    httpd_uri_t get_entities = {
        .uri = "/api/entities",
        .method = HTTP_GET,
        .handler = guarded_api_entities_get,
        .user_ctx = NULL,
    };
    httpd_uri_t get_light_entities = {
        .uri = "/api/ha/light_entities",
        .method = HTTP_GET,
        .handler = guarded_api_light_entities_get,
        .user_ctx = NULL,
    };
    httpd_uri_t get_ha_energy = {
        .uri = "/api/ha/energy",
        .method = HTTP_GET,
        .handler = guarded_api_ha_energy_get,
        .user_ctx = NULL,
    };
    httpd_uri_t get_state = {
        .uri = "/api/state",
        .method = HTTP_GET,
        .handler = guarded_api_state_get,
        .user_ctx = NULL,
    };
    httpd_uri_t get_settings = {
        .uri = "/api/settings",
        .method = HTTP_GET,
        .handler = guarded_api_settings_get,
        .user_ctx = NULL,
    };
    httpd_uri_t put_settings = {
        .uri = "/api/settings",
        .method = HTTP_PUT,
        .handler = guarded_api_settings_put,
        .user_ctx = NULL,
    };
    httpd_uri_t get_i18n_languages = {
        .uri = "/api/i18n/languages",
        .method = HTTP_GET,
        .handler = guarded_api_i18n_languages_get,
        .user_ctx = NULL,
    };
    httpd_uri_t get_i18n_effective = {
        .uri = "/api/i18n/effective",
        .method = HTTP_GET,
        .handler = guarded_api_i18n_effective_get,
        .user_ctx = NULL,
    };
    httpd_uri_t put_i18n_custom = {
        .uri = "/api/i18n/custom",
        .method = HTTP_PUT,
        .handler = guarded_api_i18n_custom_put,
        .user_ctx = NULL,
    };
    httpd_uri_t get_wifi_scan = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = guarded_api_wifi_scan_get,
        .user_ctx = NULL,
    };
    httpd_uri_t get_version = {
        .uri = "/api/version",
        .method = HTTP_GET,
        .handler = guarded_api_version_get,
        .user_ctx = NULL,
    };
    httpd_uri_t get_screenshot_bmp = {
        .uri = "/api/screenshot.bmp",
        .method = HTTP_GET,
        .handler = guarded_api_screenshot_bmp_get,
        .user_ctx = NULL,
    };
    httpd_uri_t get_ota_status = {
        .uri = "/api/ota/status",
        .method = HTTP_GET,
        .handler = guarded_api_ota_status_get,
        .user_ctx = NULL,
    };
    httpd_uri_t post_ota_url = {
        .uri = "/api/ota/url",
        .method = HTTP_POST,
        .handler = guarded_api_ota_url_post,
        .user_ctx = NULL,
    };
    httpd_uri_t post_ota_upload = {
        .uri = "/api/ota/upload",
        .method = HTTP_POST,
        .handler = guarded_api_ota_upload_post,
        .user_ctx = NULL,
    };

    httpd_uri_t get_themes_list = {
        .uri = "/api/themes",
        .method = HTTP_GET,
        .handler = guarded_api_themes_list_get,
        .user_ctx = NULL,
    };
    httpd_uri_t get_themes_active = {
        .uri = "/api/themes/active",
        .method = HTTP_GET,
        .handler = guarded_api_themes_active_get,
        .user_ctx = NULL,
    };
    httpd_uri_t put_themes_active = {
        .uri = "/api/themes/active",
        .method = HTTP_PUT,
        .handler = guarded_api_themes_active_put,
        .user_ctx = NULL,
    };
    httpd_uri_t get_themes_export = {
        .uri = "/api/themes/get",
        .method = HTTP_GET,
        .handler = guarded_api_themes_get,
        .user_ctx = NULL,
    };
    httpd_uri_t put_themes_custom = {
        .uri = "/api/themes/custom",
        .method = HTTP_PUT,
        .handler = guarded_api_themes_custom_put,
        .user_ctx = NULL,
    };
    httpd_uri_t delete_themes_custom = {
        .uri = "/api/themes/custom",
        .method = HTTP_DELETE,
        .handler = guarded_api_themes_custom_delete,
        .user_ctx = NULL,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_layout), "api_routes", "GET /api/layout");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &put_layout), "api_routes", "PUT /api/layout");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_entities), "api_routes", "GET /api/entities");
    ESP_RETURN_ON_ERROR(
        httpd_register_uri_handler(server, &get_light_entities), "api_routes", "GET /api/ha/light_entities");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_ha_energy), "api_routes", "GET /api/ha/energy");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_state), "api_routes", "GET /api/state");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_settings), "api_routes", "GET /api/settings");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &put_settings), "api_routes", "PUT /api/settings");
    ESP_RETURN_ON_ERROR(
        httpd_register_uri_handler(server, &get_i18n_languages), "api_routes", "GET /api/i18n/languages");
    ESP_RETURN_ON_ERROR(
        httpd_register_uri_handler(server, &get_i18n_effective), "api_routes", "GET /api/i18n/effective");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &put_i18n_custom), "api_routes", "PUT /api/i18n/custom");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_wifi_scan), "api_routes", "GET /api/wifi/scan");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_version), "api_routes", "GET /api/version");
    ESP_RETURN_ON_ERROR(
        httpd_register_uri_handler(server, &get_screenshot_bmp), "api_routes", "GET /api/screenshot.bmp");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_ota_status), "api_routes", "GET /api/ota/status");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &post_ota_url), "api_routes", "POST /api/ota/url");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &post_ota_upload), "api_routes", "POST /api/ota/upload");

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_themes_list), "api_routes", "GET /api/themes");
    ESP_RETURN_ON_ERROR(
        httpd_register_uri_handler(server, &get_themes_active), "api_routes", "GET /api/themes/active");
    ESP_RETURN_ON_ERROR(
        httpd_register_uri_handler(server, &put_themes_active), "api_routes", "PUT /api/themes/active");
    ESP_RETURN_ON_ERROR(
        httpd_register_uri_handler(server, &get_themes_export), "api_routes", "GET /api/themes/get");
    ESP_RETURN_ON_ERROR(
        httpd_register_uri_handler(server, &put_themes_custom), "api_routes", "PUT /api/themes/custom");
    ESP_RETURN_ON_ERROR(
        httpd_register_uri_handler(server, &delete_themes_custom), "api_routes", "DELETE /api/themes/custom");

    return ESP_OK;
}
