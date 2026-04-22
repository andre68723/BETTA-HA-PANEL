/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t api_routes_register(httpd_handle_t server);

esp_err_t api_layout_get_handler(httpd_req_t *req);
esp_err_t api_layout_put_handler(httpd_req_t *req);
esp_err_t api_entities_get_handler(httpd_req_t *req);
esp_err_t api_light_entities_get_handler(httpd_req_t *req);
esp_err_t api_ha_energy_get_handler(httpd_req_t *req);
esp_err_t api_state_get_handler(httpd_req_t *req);
esp_err_t api_settings_get_handler(httpd_req_t *req);
esp_err_t api_settings_put_handler(httpd_req_t *req);
esp_err_t api_i18n_languages_get_handler(httpd_req_t *req);
esp_err_t api_i18n_effective_get_handler(httpd_req_t *req);
esp_err_t api_i18n_custom_put_handler(httpd_req_t *req);
esp_err_t api_wifi_scan_get_handler(httpd_req_t *req);
esp_err_t api_version_get_handler(httpd_req_t *req);
esp_err_t api_screenshot_bmp_get_handler(httpd_req_t *req);
esp_err_t api_ota_status_get_handler(httpd_req_t *req);
esp_err_t api_ota_url_post_handler(httpd_req_t *req);
esp_err_t api_ota_upload_post_handler(httpd_req_t *req);

esp_err_t api_themes_list_get_handler(httpd_req_t *req);
esp_err_t api_themes_active_get_handler(httpd_req_t *req);
esp_err_t api_themes_active_put_handler(httpd_req_t *req);
esp_err_t api_themes_get_handler(httpd_req_t *req);
esp_err_t api_themes_custom_put_handler(httpd_req_t *req);
esp_err_t api_themes_custom_delete_handler(httpd_req_t *req);

esp_err_t api_themes_list_get_handler(httpd_req_t *req);
esp_err_t api_themes_active_get_handler(httpd_req_t *req);
esp_err_t api_themes_active_put_handler(httpd_req_t *req);
esp_err_t api_themes_get_handler(httpd_req_t *req);
esp_err_t api_themes_custom_put_handler(httpd_req_t *req);
esp_err_t api_themes_custom_delete_handler(httpd_req_t *req);
