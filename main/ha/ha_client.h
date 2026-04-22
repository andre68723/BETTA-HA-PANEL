/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "cJSON.h"

/* Callback invoked from the WS RX task context when a response for a request
 * issued via ha_client_call_service_with_response() arrives. `result_payload`
 * is the value of the "result" object in HA's reply (or NULL on failure) and
 * is only valid during the callback — do NOT retain the pointer. */
typedef void (*ha_client_response_cb_t)(bool success, cJSON *result_payload, void *user);

typedef struct {
    const char *ws_url;
    const char *access_token;
    bool rest_enabled;
} ha_client_config_t;

esp_err_t ha_client_start(const ha_client_config_t *cfg);
void ha_client_stop(void);
bool ha_client_is_connected(void);
bool ha_client_is_initial_sync_done(void);
esp_err_t ha_client_call_service(const char *domain, const char *service, const char *json_service_data);
/* Like ha_client_call_service() but also requests return_response=true and
 * delivers the response via `cb` (invoked exactly once, either with
 * success=true and the `result` payload, or success=false on timeout/error).
 * `target_entity_id` is optional (NULL = no explicit target). `cb` MUST NOT
 * call back into ha_client directly and MUST NOT touch LVGL without proper
 * locking — it runs on the HA WS RX task. Typical usage: marshal work into
 * lv_async_call(). */
esp_err_t ha_client_call_service_with_response(
    const char *domain,
    const char *service,
    const char *target_entity_id,
    const char *json_service_data,
    ha_client_response_cb_t cb,
    void *user);
/* Cancel any pending response callbacks that match the given `user` pointer.
 * Must be called by any caller of ha_client_call_service_with_response()
 * before the object referenced by `user` is destroyed, to prevent the WS
 * RX task from invoking the callback with a dangling pointer.  Safe to call
 * from any task; never invokes the callback. */
void ha_client_cancel_pending_responses_for_user(void *user);
esp_err_t ha_client_notify_layout_updated(void);
esp_err_t ha_client_request_energy_refresh(void);
esp_err_t ha_client_get_domain_entities_json(const char *domain, const char *search, bool refresh, char **out_json);

/* HTTP-context snapshot used by auxiliary consumers (e.g. cover-art fetcher).
 * All fields are NUL-terminated. Returns false if the HA client is not yet
 * connected or the WS URL cannot be parsed. */
typedef struct {
    char base_url[256];            /* e.g. "https://1.2.3.4:8443" */
    char host_header[192];         /* e.g. "ha.example.com:8443" when base_url uses an IP */
    char cert_common_name[128];    /* TLS SNI/CN expected by mbedTLS bundle */
    char bearer_token[512];        /* Long-lived access token */
} ha_client_http_ctx_t;

bool ha_client_get_http_context(ha_client_http_ctx_t *out);

/* True while a "heavy" HA WS/TLS request (energy stats, weather forecast,
 * light discovery, call-with-response, etc.) is either in flight or still
 * cooling down.  Low-priority auxiliary HTTP consumers (cover-art fetcher,
 * background sync) must poll this and defer their own TLS traffic while it
 * returns true so the UI/WS path always has priority on the TLS engine. */
bool ha_client_heavy_gate_is_busy(void);
