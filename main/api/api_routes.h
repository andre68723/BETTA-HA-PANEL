/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t api_routes_register(httpd_handle_t server);

esp_err_t api_layout_get_handler(httpd_req_t *req);
esp_err_t api_layout_put_handler(httpd_req_t *req);
esp_err_t api_entities_get_handler(httpd_req_t *req);
esp_err_t api_light_entities_get_handler(httpd_req_t *req);
esp_err_t api_light_entities_delete_handler(httpd_req_t *req);
esp_err_t api_ha_energy_get_handler(httpd_req_t *req);
esp_err_t api_state_get_handler(httpd_req_t *req);
esp_err_t api_settings_get_handler(httpd_req_t *req);
esp_err_t api_settings_put_handler(httpd_req_t *req);
esp_err_t api_i18n_languages_get_handler(httpd_req_t *req);
esp_err_t api_i18n_effective_get_handler(httpd_req_t *req);
esp_err_t api_i18n_custom_put_handler(httpd_req_t *req);
esp_err_t api_wifi_scan_get_handler(httpd_req_t *req);
esp_err_t api_version_get_handler(httpd_req_t *req);
esp_err_t api_log_get_handler(httpd_req_t *req);
esp_err_t api_coredump_get_handler(httpd_req_t *req);
esp_err_t api_coredump_erase_post_handler(httpd_req_t *req);
esp_err_t api_navigate_post_handler(httpd_req_t *req);
esp_err_t api_ui_tap_post_handler(httpd_req_t *req);
esp_err_t api_reboot_log_get_handler(httpd_req_t *req);
esp_err_t api_sound_test_post_handler(httpd_req_t *req);
esp_err_t api_camera_test_post_handler(httpd_req_t *req);
esp_err_t api_display_test_get_handler(httpd_req_t *req);
esp_err_t api_display_test_post_handler(httpd_req_t *req);
esp_err_t api_factory_reset_post_handler(httpd_req_t *req);
esp_err_t api_restart_post_handler(httpd_req_t *req);
esp_err_t api_screenshot_bmp_get_handler(httpd_req_t *req);
esp_err_t api_ota_status_get_handler(httpd_req_t *req);
esp_err_t api_ota_url_post_handler(httpd_req_t *req);
esp_err_t api_ota_check_post_handler(httpd_req_t *req);
#if CONFIG_APP_AUTO_UPDATE_TEST_HOOK
esp_err_t api_ota_auto_test_post_handler(httpd_req_t *req); /* test builds only */
#endif
esp_err_t api_ota_upload_post_handler(httpd_req_t *req);
esp_err_t api_ha_diagnostics_get_handler(httpd_req_t *req);
esp_err_t api_ha_printers_get_handler(httpd_req_t *req);
esp_err_t api_ha_evcc_get_handler(httpd_req_t *req);
esp_err_t api_notifications_post_handler(httpd_req_t *req);

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
