/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "api/api_routes.h"

#include "cJSON.h"
#include "esp_app_desc.h"

#include "app_config.h"

static void set_json_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

esp_err_t api_version_get_handler(httpd_req_t *req)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    const char *version = (desc != NULL && desc->version[0] != '\0') ? desc->version : "unknown";
    const char *project_name = (desc != NULL && desc->project_name[0] != '\0') ? desc->project_name : APP_NAME;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_500(req);
    }

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "name", APP_NAME);
    cJSON_AddStringToObject(root, "project", project_name);
    cJSON_AddStringToObject(root, "version", version);
    /* Panel geometry for the web editor canvas (px) - lets the same
     * app.js scale itself for both the 4" 720x720 and 10.1" 1280x800
     * variants without a separate build. */
    cJSON_AddNumberToObject(root, "screen_w", APP_SCREEN_WIDTH);
    cJSON_AddNumberToObject(root, "screen_h", APP_SCREEN_HEIGHT);
    cJSON_AddNumberToObject(root, "canvas_w", APP_CONTENT_BOX_WIDTH);
    cJSON_AddNumberToObject(root, "canvas_h", APP_CONTENT_BOX_HEIGHT);

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
