/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * GET /api/ha/evcc[?refresh=1]: the evcc loadpoints of Home Assistant with the entity of each widget binding
 * (ha/ha_evcc_lookup.h). The first call (or refresh=1) starts the lookup, the editor polls until status is ready.
 */
#include "api/api_routes.h"

#include <string.h>

#include "cJSON.h"

#include "ha/ha_client.h"
#include "ha/ha_evcc_lookup.h"

esp_err_t api_ha_evcc_get_handler(httpd_req_t *req)
{
    char query[32] = {0};
    bool refresh = false;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char value[8] = {0};
        refresh = httpd_query_key_value(query, "refresh", value, sizeof(value)) == ESP_OK &&
                  (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!ha_client_is_connected()) {
        return httpd_resp_sendstr(req, "{\"status\":\"disconnected\",\"loadpoints\":[]}");
    }
    if (refresh) {
        ha_evcc_lookup_request();
    }
    char *payload = ha_evcc_lookup_json();
    if (payload == NULL) {
        return httpd_resp_send_500(req);
    }
    const esp_err_t err = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);
    return err;
}
