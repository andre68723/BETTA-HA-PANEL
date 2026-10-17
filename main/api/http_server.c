/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "api/http_server.h"
#include "api/http_guard.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#include "api/api_routes.h"
#include "app_config.h"
#include "webui_assets.h"
#include "util/log_tags.h"

static httpd_handle_t s_server = NULL;

/* The web assets are embedded pre-compressed (see components/webui/CMakeLists.txt)
 * and always sent with Content-Encoding: gzip; every browser accepts it, there
 * is no uncompressed copy in flash. The blob is binary, so its length must not
 * be trimmed for a trailing NUL (the gzip trailer's last byte is usually 0). */
static esp_err_t send_embedded(
    httpd_req_t *req, const uint8_t *start, const uint8_t *end, const char *content_type, bool cache_assets)
{
    httpd_resp_set_type(req, content_type);
    if (strcmp(content_type, "text/html") == 0) {
        /* The editor loads only its own scripts (no inline script, no eval); styles are set inline by the code;
         * the only outside request is the GitHub API lookup of the latest release. The layout preview's
         * Poppins font is a data: URI inside the stylesheet (font-src data:), not a fourth request. */
        httpd_resp_set_hdr(req, "Content-Security-Policy",
            "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; font-src 'self' data:; "
            "connect-src 'self' https://api.github.com; object-src 'none'; base-uri 'none'; form-action 'self'; "
            "frame-ancestors 'none'");
    }
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    /* Fingerprinted JS/CSS URLs change with their contents. The entry HTML must
     * always be fetched again so an OTA update points at the new asset URLs. */
    httpd_resp_set_hdr(req, "Cache-Control",
                       cache_assets ? "private, max-age=31536000, immutable" : "no-store");
    return httpd_resp_send(req, (const char *)start, (ssize_t)(end - start));
}

/* One handler for every embedded editor file: the table entry comes in through user_ctx. */
static esp_err_t asset_get_handler_impl(httpd_req_t *req)
{
    const webui_asset_t *asset = (const webui_asset_t *)req->user_ctx;
    if (asset == NULL || asset->end <= asset->start) {
        return httpd_resp_send_404(req);
    }
    return send_embedded(req, asset->start, asset->end, asset->content_type,
                         strcmp(asset->content_type, "text/html") != 0);
}

static esp_err_t favicon_get_handler_impl(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t guarded_asset_get_handler(httpd_req_t *req)
{
    return http_guard_handle(req, asset_get_handler_impl);
}

static esp_err_t guarded_favicon_get_handler(httpd_req_t *req)
{
    return http_guard_handle(req, favicon_get_handler_impl);
}

esp_err_t http_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(http_guard_init(), TAG_HTTP, "init http guard");

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = APP_HTTP_PORT;
    cfg.stack_size = APP_HTTP_TASK_STACK;
    int http_task_prio = APP_UI_TASK_PRIO + 1;
    if (http_task_prio >= APP_HA_TASK_PRIO) {
        http_task_prio = APP_HA_TASK_PRIO - 1;
    }
    if (http_task_prio < 1) {
        http_task_prio = 1;
    }
    cfg.task_priority = http_task_prio;
    cfg.max_uri_handlers = 64;
    cfg.max_open_sockets = 12;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;
    cfg.backlog_conn = 8;
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
    cfg.core_id = 1;
#endif

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Failed to start HTTP server");
        return err;
    }

    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = guarded_favicon_get_handler,
        .user_ctx = NULL,
    };

    for (size_t i = 0; i < webui_asset_count; i++) {
        httpd_uri_t asset_uri = {
            .uri = webui_assets[i].uri,
            .method = HTTP_GET,
            .handler = guarded_asset_get_handler,
            .user_ctx = (void *)&webui_assets[i],
        };
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &asset_uri), TAG_HTTP, "register editor file");
        if (strcmp(webui_assets[i].uri, "/index.html") == 0) {
            asset_uri.uri = "/"; /* the editor is also served at the root */
            ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &asset_uri), TAG_HTTP, "register /");
        }
    }
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &favicon_uri), TAG_HTTP, "register /favicon.ico");
    ESP_RETURN_ON_ERROR(api_routes_register(s_server), TAG_HTTP, "register api routes");

    ESP_LOGI(TAG_HTTP, "HTTP server listening on port %d", APP_HTTP_PORT);
    return ESP_OK;
}

void http_server_stop(void)
{
    if (s_server == NULL) {
        return;
    }
    httpd_stop(s_server);
    s_server = NULL;
}

httpd_handle_t http_server_get_handle(void)
{
    return s_server;
}
