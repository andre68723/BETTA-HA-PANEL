/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "api/api_routes.h"

#include "cJSON.h"

#include "ha/ha_client.h"
#include "ha/ha_energy_model.h"

static void set_json_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

esp_err_t api_ha_energy_get_handler(httpd_req_t *req)
{
    ha_energy_snapshot_t snapshot = {0};
    bool available = ha_energy_model_get_snapshot(&snapshot);
    if (!available || !snapshot.available) {
        (void)ha_client_request_energy_refresh();
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_500(req);
    }

    cJSON_AddBoolToObject(root, "available", available && snapshot.available);
    cJSON_AddBoolToObject(root, "has_grid", snapshot.has_grid);
    cJSON_AddBoolToObject(root, "has_solar", snapshot.has_solar);
    cJSON_AddBoolToObject(root, "has_battery", snapshot.has_battery);
    cJSON_AddBoolToObject(root, "has_gas", snapshot.has_gas);
    cJSON_AddBoolToObject(root, "has_water", snapshot.has_water);
    cJSON_AddNumberToObject(root, "from_grid_kwh", (double)snapshot.from_grid_kwh);
    cJSON_AddNumberToObject(root, "to_grid_kwh", (double)snapshot.to_grid_kwh);
    cJSON_AddNumberToObject(root, "solar_kwh", (double)snapshot.solar_kwh);
    cJSON_AddNumberToObject(root, "to_battery_kwh", (double)snapshot.to_battery_kwh);
    cJSON_AddNumberToObject(root, "from_battery_kwh", (double)snapshot.from_battery_kwh);
    cJSON_AddNumberToObject(root, "gas_value", (double)snapshot.gas_value);
    cJSON_AddNumberToObject(root, "water_value", (double)snapshot.water_value);
    cJSON_AddStringToObject(root, "gas_unit", snapshot.gas_unit);
    cJSON_AddStringToObject(root, "water_unit", snapshot.water_unit);
    cJSON_AddNumberToObject(root, "updated_unix_ms", (double)snapshot.updated_unix_ms);
    cJSON_AddNumberToObject(root, "revision", (double)ha_energy_model_revision());

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
