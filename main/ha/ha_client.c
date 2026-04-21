/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ha/ha_client.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"

#include "app_config.h"
#include "app_events.h"
#include "ha/ha_energy_model.h"
#include "ha/ha_light_capabilities.h"
#include "ha/ha_model.h"
#include "ha/ha_ws.h"
#include "layout/layout_store.h"
#include "net/wifi_mgr.h"
#include "util/log_tags.h"

typedef struct {
    char *payload;
    int len;
} ha_ws_rx_msg_t;

typedef struct {
    bool active;
    uint32_t id;
    int64_t queued_unix_ms;
    int64_t sent_unix_ms;
    int64_t result_unix_ms;
    bool result_seen;
    bool result_success;
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char domain[24];
    char service[32];
    char expected_state[16];
} ha_service_trace_t;

typedef enum {
    HA_BG_BUDGET_NORMAL = 0,
    HA_BG_BUDGET_PRESSURE = 1,
    HA_BG_BUDGET_PROTECT = 2,
    HA_BG_BUDGET_CRITICAL = 3,
} ha_bg_budget_level_t;

#define HA_ENERGY_PAGE_ENTITY_MAX (APP_MAX_PAGES * 9)
#define HA_LAYOUT_ENTITY_MAX ((APP_MAX_WIDGETS_TOTAL * 2) + HA_ENERGY_PAGE_ENTITY_MAX)
#define HA_WS_ENTITIES_SUB_MAX HA_LAYOUT_ENTITY_MAX
#define HA_WS_ENTITIES_SUB_ID_BYTES ((size_t)HA_WS_ENTITIES_SUB_MAX * (size_t)APP_MAX_ENTITY_ID_LEN)
#define HA_WS_ENTITIES_SUB_REQ_BYTES ((size_t)HA_WS_ENTITIES_SUB_MAX * sizeof(uint32_t))
#define HA_SVC_TRACE_CAPACITY 48U
#define HA_ENERGY_STAT_REF_MAX 16U
/* Energy statistics are daily kWh aggregates that tick slowly.
 * 15 minutes is responsive enough for a wall panel and keeps TLS / WS
 * pressure low; the UI additionally forces a refresh whenever the user
 * navigates to the energy page (see ui_runtime energy page show hook). */
#define HA_ENERGY_SYNC_INTERVAL_MS (15 * 60 * 1000)
#define HA_ENERGY_SYNC_RETRY_MS (60 * 1000)

/* ------------------------------------------------------------------------
 *  Central WS/TLS send gate
 * ------------------------------------------------------------------------
 *  Single choke-point that prevents two "heavy" WebSocket requests (large
 *  JSON responses that spike the mbedTLS record decoder + cJSON heap) from
 *  overlapping.  This is the unified mechanism new WS features should dock
 *  into so the TLS stack on the ESP32-P4 is never overloaded.
 *
 *  Classes:
 *    HA_WS_SEND_LIGHT  auth, ping, pong, per-entity subscribe step,
 *                      trigger subscriptions.  Small payloads; never gated.
 *    HA_WS_SEND_HEAVY  energy prefs/stats, weather forecast, light
 *                      discovery phases.  Gated via existing *_req_inflight
 *                      flags plus a minimum inter-send cooldown.
 *
 *  Policy:
 *    - At most ONE heavy request in flight across the whole client
 *      (ha_client_heavy_in_flight_locked()).
 *    - After a heavy response has been fully parsed we arm a cooldown of
 *      HA_WS_HEAVY_MIN_GAP_MS before the next heavy send is permitted.
 *      This gives mbedTLS + cJSON a window to free the receive buffer
 *      before the next roundtrip starts.
 *    - Light sends are never blocked.
 *
 *  Adding a new heavy request type:
 *    1. Add `bool <name>_req_inflight;` to ha_client_state_t and include
 *       it in ha_client_heavy_in_flight_locked().
 *    2. At the send site (holding s_client.mutex):
 *         if (!ha_client_ws_send_gate_ok_locked(HA_WS_SEND_HEAVY, now_ms))
 *             break;   // or defer; try again on next task tick
 *         ... issue send ...
 *         s_client.<name>_req_inflight = true;
 *    3. In the matching RX handler, *before* clearing the inflight flag:
 *         ha_client_ws_send_gate_mark_heavy_done_locked(now_ms);
 *         s_client.<name>_req_inflight = false;
 *    4. Error/timeout/reconnect paths just clear the inflight flag; the
 *       gate opens automatically (no cooldown is armed in that case,
 *       which is correct: no big payload was processed).
 */
#define HA_WS_HEAVY_MIN_GAP_MS 400

typedef enum {
    HA_WS_SEND_LIGHT = 0,  /* small payload, never gated */
    HA_WS_SEND_HEAVY,      /* large response, single-in-flight + cooldown */
} ha_ws_send_class_t;

typedef enum {
    HA_LIGHT_DISCOVERY_PHASE_NONE = 0,
    HA_LIGHT_DISCOVERY_PHASE_TEMPLATE,
    HA_LIGHT_DISCOVERY_PHASE_ENTITY_DISPLAY,
    HA_LIGHT_DISCOVERY_PHASE_ENTITY_FULL,
    HA_LIGHT_DISCOVERY_PHASE_AREAS,
    HA_LIGHT_DISCOVERY_PHASE_DEVICES,
} ha_light_discovery_phase_t;

typedef enum {
    HA_ENERGY_STAT_GRID_FROM = 0,
    HA_ENERGY_STAT_GRID_TO,
    HA_ENERGY_STAT_SOLAR,
    HA_ENERGY_STAT_BATTERY_FROM,
    HA_ENERGY_STAT_BATTERY_TO,
    HA_ENERGY_STAT_GAS,
    HA_ENERGY_STAT_WATER,
} ha_energy_stat_kind_t;

typedef struct {
    char id[APP_MAX_ENTITY_ID_LEN];
    char unit[APP_MAX_UNIT_LEN];
    ha_energy_stat_kind_t kind;
} ha_energy_stat_ref_t;

typedef struct {
    char id[APP_MAX_ENTITY_ID_LEN];
    char name[APP_MAX_NAME_LEN];
    char room[APP_MAX_NAME_LEN];
    char area_id[APP_HA_DISCOVERY_ID_MAX_LEN];
    char device_id[APP_HA_DISCOVERY_ID_MAX_LEN];
    char icon[APP_MAX_ICON_LEN];
} ha_light_discovery_pending_item_t;

typedef struct {
    char id[APP_MAX_ENTITY_ID_LEN];
    char name[APP_MAX_NAME_LEN];
    char room[APP_MAX_NAME_LEN];
    char area_id[APP_HA_DISCOVERY_ID_MAX_LEN];
    char icon[APP_MAX_ICON_LEN];
} ha_light_discovery_item_t;

typedef struct {
    char id[APP_HA_DISCOVERY_ID_MAX_LEN];
    char name[APP_MAX_NAME_LEN];
} ha_light_discovery_area_t;

typedef struct {
    char id[APP_HA_DISCOVERY_ID_MAX_LEN];
    char area_id[APP_HA_DISCOVERY_ID_MAX_LEN];
} ha_light_discovery_device_t;

typedef struct {
    bool started;
    bool authenticated;
    bool published_disconnect;
    bool pending_send_auth;
    bool pending_initial_layout_sync;
    bool pending_send_pong;
    bool pending_subscribe;
    bool pending_get_states;
    bool initial_layout_sync_done;
    uint32_t pending_pong_id;
    bool ping_inflight;
    uint32_t ping_inflight_id;
    bool rest_enabled;
    char ws_url[256];
    char access_token[512];
    char http_base_url[256];
    char http_cert_common_name[128];
    char http_resolved_host[128];
    char http_resolved_ip[64];
    uint32_t next_message_id;
    uint32_t get_states_req_id;
    uint32_t trigger_sub_req_id;
    uint32_t entities_sub_req_id;
    bool sub_state_via_trigger;
    bool sub_state_via_entities;
    bool ws_entities_subscribe_supported;
    uint16_t entities_sub_target_count;
    uint16_t entities_sub_sent_count;
    uint16_t entities_sub_seen_count;
    int64_t next_entities_subscribe_unix_ms;
    char *entities_sub_targets;
    uint32_t *entities_sub_req_ids;
    char *entities_sub_seen;
    uint8_t ping_timeout_strikes;
    uint8_t ws_short_session_strikes;
    bool pending_force_wifi_recover;
    int64_t ping_sent_unix_ms;
    int64_t last_rx_unix_ms;
    int64_t ws_last_connected_unix_ms;
    int64_t next_auth_retry_unix_ms;
    int64_t next_initial_layout_sync_unix_ms;
    int64_t next_periodic_layout_sync_unix_ms;
    uint32_t initial_layout_sync_index;
    uint32_t initial_layout_sync_imported;
    uint32_t periodic_layout_sync_cursor;
    char priority_sync_entities[16][APP_MAX_ENTITY_ID_LEN];
    uint8_t priority_sync_head;
    uint8_t priority_sync_tail;
    uint8_t priority_sync_count;
    int64_t next_priority_sync_unix_ms;
    uint32_t ws_error_streak;
    ha_bg_budget_level_t bg_budget_level;
    int64_t bg_budget_level_since_unix_ms;
    int64_t bg_budget_last_log_unix_ms;
    uint32_t bg_budget_level_change_count;
    uint32_t http_open_count_window;
    uint32_t http_open_fail_count_window;
    uint8_t http_open_fail_streak;
    int64_t http_open_window_start_unix_ms;
    int64_t http_open_cooldown_until_unix_ms;
    int64_t next_weather_forecast_retry_unix_ms;
    bool layout_needs_weather_forecast;
    bool weather_ws_req_inflight;
    uint32_t weather_ws_req_id;
    char weather_ws_req_entity_id[APP_MAX_ENTITY_ID_LEN];
    bool layout_needs_ha_energy;
    bool pending_energy_prefs;
    bool pending_energy_stats;
    bool energy_prefs_req_inflight;
    bool energy_stats_req_inflight;
    uint32_t energy_prefs_req_id;
    uint32_t energy_stats_req_id;
    uint8_t energy_stats_batch_offset;
    int64_t next_energy_sync_unix_ms;
    /* Central WS/TLS gate: earliest ms at which the next HEAVY send may
     * leave.  See HA_WS_HEAVY_MIN_GAP_MS doc block. */
    int64_t heavy_ws_gate_next_allowed_unix_ms;
    ha_energy_stat_ref_t energy_stat_refs[HA_ENERGY_STAT_REF_MAX];
    uint8_t energy_stat_ref_count;
    ha_energy_snapshot_t energy_staging;
    uint32_t layout_entity_signature;
    uint16_t layout_entity_count;
    int64_t ws_priority_boost_until_unix_ms;
    int last_ws_tls_stack_err;
    esp_err_t last_ws_tls_esp_err;
    int last_ws_sock_errno;
    int64_t last_ws_error_unix_ms;
    int64_t last_ws_bad_input_unix_ms;
    int64_t ws_get_states_block_until_unix_ms;
    ha_service_trace_t service_traces[HA_SVC_TRACE_CAPACITY];
    bool light_discovery_requested;
    bool light_discovery_inflight;
    ha_light_discovery_phase_t light_discovery_phase;
    char light_discovery_domain[APP_HA_DISCOVERY_DOMAIN_MAX_LEN];
    char light_discovery_search[APP_HA_DISCOVERY_SEARCH_MAX_LEN];
    uint32_t light_discovery_req_id;
    int64_t light_discovery_started_unix_ms;
    int64_t light_discovery_updated_unix_ms;
    int64_t light_discovery_next_step_unix_ms;
    int64_t light_discovery_last_wait_log_unix_ms;
    uint16_t light_discovery_template_offset;
    uint16_t light_discovery_template_total;
    ha_light_discovery_item_t *light_discovery_items;
    uint16_t light_discovery_count;
    bool light_discovery_truncated;
    ha_light_discovery_pending_item_t *light_discovery_pending_items;
    uint16_t light_discovery_pending_count;
    bool light_discovery_pending_truncated;
    ha_light_discovery_area_t *light_discovery_areas;
    uint16_t light_discovery_area_count;
    bool light_discovery_area_truncated;
    ha_light_discovery_device_t *light_discovery_devices;
    uint16_t light_discovery_device_count;
    bool light_discovery_device_truncated;
    esp_http_client_handle_t http_client;
    QueueHandle_t ws_rx_queue;
    TaskHandle_t task_handle;
    SemaphoreHandle_t mutex;
} ha_client_state_t;

static ha_client_state_t s_client = {0};

/* ---- Central WS/TLS send gate helpers (caller must hold s_client.mutex) --- */

static inline bool ha_client_heavy_in_flight_locked(void)
{
    return s_client.weather_ws_req_inflight ||
           s_client.energy_prefs_req_inflight ||
           s_client.energy_stats_req_inflight ||
           s_client.light_discovery_inflight;
}

static inline bool ha_client_ws_send_gate_ok_locked(ha_ws_send_class_t cls, int64_t now_ms)
{
    if (cls == HA_WS_SEND_LIGHT) {
        return true;
    }
    if (ha_client_heavy_in_flight_locked()) {
        return false;
    }
    if (now_ms < s_client.heavy_ws_gate_next_allowed_unix_ms) {
        return false;
    }
    return true;
}

static inline void ha_client_ws_send_gate_mark_heavy_done_locked(int64_t now_ms)
{
    s_client.heavy_ws_gate_next_allowed_unix_ms = now_ms + HA_WS_HEAVY_MIN_GAP_MS;
}
static const int HA_WEATHER_COMPACT_FORECAST_MAX_ITEMS = 4;
static const int64_t HA_WS_RESTART_INTERVAL_MS = 12000;
static const int64_t HA_WS_RESTART_INTERVAL_MAX_MS = 30000;
static const int64_t HA_WS_RESTART_JITTER_MS = 1000;
static const int64_t HA_WS_CONNECT_GRACE_MS = 15000;
static const int64_t HA_WS_SHORT_SESSION_MS = 180000;
static const uint8_t HA_WS_SHORT_SESSION_STRIKES_TO_WIFI_RECOVER = 4;
static const uint8_t HA_WS_SHORT_SESSION_STRIKES_TO_TRANSPORT_RECOVER = 6;
static const uint32_t HA_WS_ERROR_STREAK_WIFI_RECOVER_THRESHOLD = 3;
static const uint32_t HA_WS_ERROR_STREAK_TRANSPORT_RECOVER_THRESHOLD = 4;
static const int64_t HA_WS_PING_INTERVAL_MIN_MS = 30000;
static const int64_t HA_WS_PING_TIMEOUT_MIN_MS = 45000;
static const int64_t HA_WIFI_DOWN_RECOVERY_MS = 45000;
static const int64_t HA_WIFI_FORCE_RECOVER_COOLDOWN_MS = 30000;
static const int64_t HA_AUTH_RETRY_INTERVAL_MS = 1000;
static const int64_t HA_INITIAL_LAYOUT_SYNC_RETRY_INTERVAL_MS = 6000;
static const int64_t HA_PERIODIC_LAYOUT_SYNC_RETRY_INTERVAL_MS = 120000;
static const int64_t HA_PRIORITY_SYNC_RETRY_INTERVAL_MS = 1500;
static const size_t HA_TRIGGER_SUBSCRIBE_MAX_ENTITIES = 64;
static const int HA_WS_RX_DRAIN_BUDGET = 32;
static const uint8_t HA_PING_TIMEOUT_STRIKES_TO_RECONNECT = 2;
static const bool HA_USE_TRIGGER_SUBSCRIPTION = true;
static const bool HA_USE_WS_ENTITIES_SUBSCRIPTION = (APP_HA_USE_WS_ENTITIES_SUBSCRIPTION != 0);
static const TickType_t HA_CLIENT_TASK_DELAY_TICKS = pdMS_TO_TICKS(30);
/* Weather forecast is a HEAVY WS request; concurrent execution with energy
 * stats / light discovery is prevented by the central send gate (see
 * HA_WS_HEAVY_MIN_GAP_MS).  This grace only exists to let the WS session
 * itself settle (handshake, auth, first subscribe burst) before we pile on
 * large responses.  Observed boot timing: auth ~100 ms, 11 subscribes
 * ~1.7 s after connect — 2 s is enough with margin. */
static const int64_t HA_WS_WEATHER_PRIORITY_GRACE_MS = 2000;
static const int HA_WS_TLS_ERR_BAD_INPUT_DATA = 0x7100;
static const int64_t HA_WS_GET_STATES_MIN_SESSION_MS = 3000;
static const int64_t HA_WS_GET_STATES_POST_SUBSCRIBE_DELAY_MS = 1200;
static const int64_t HA_WS_GET_STATES_BAD_INPUT_COOLDOWN_MS = 60000;
/* If true, repeated WS failures while Wi-Fi is up can escalate to Wi-Fi/C6 recover.
   Keep disabled so intentional HA downtime does not trigger transport recovery loops. */
static const bool HA_WS_ESCALATE_RECOVER_WHEN_WIFI_UP = false;
/* Per-entity subscribe step cadence.  Subscribes are LIGHT sends (tiny
 * request + tiny ack), so this is only about not flooding the WS send
 * queue, not about TLS heap pressure.  150 ms paces ~6 subscribes/s which
 * the esp_websocket_client TX path handles comfortably. */
static const int64_t HA_WS_ENTITIES_SUBSCRIBE_STEP_DELAY_MS = 150;
static const int64_t HA_LIGHT_DISCOVERY_WS_STABLE_DELAY_MS = 45000;
static const int64_t HA_LIGHT_DISCOVERY_TEMPLATE_STABLE_DELAY_MS = 12000;
static const int64_t HA_LIGHT_DISCOVERY_PAGE_STEP_DELAY_MS = 700;
static const int64_t HA_LIGHT_DISCOVERY_RETRY_DELAY_MS = 6000;
static const int64_t HA_LIGHT_DISCOVERY_WAIT_LOG_INTERVAL_MS = 5000;
static const size_t HA_LIGHT_DISCOVERY_MIN_INTERNAL_FREE_BYTES = (180U * 1024U);
static const size_t HA_LIGHT_DISCOVERY_MIN_INTERNAL_LARGEST_BYTES = (80U * 1024U);
static const size_t HA_LIGHT_DISCOVERY_TEMPLATE_MIN_INTERNAL_FREE_BYTES = (120U * 1024U);
static const size_t HA_LIGHT_DISCOVERY_TEMPLATE_MIN_INTERNAL_LARGEST_BYTES = (48U * 1024U);
/* Internal heap on ESP32-P4 can be low in normal operation due to DMA/internal reservations.
   Tune thresholds to avoid permanent "protect" on healthy WS-only idle. */
static const size_t HA_BG_HEAP_PRESSURE_BYTES = (12U * 1024U);
static const size_t HA_BG_HEAP_PROTECT_BYTES = (8U * 1024U);
static const size_t HA_BG_HEAP_CRITICAL_BYTES = (5U * 1024U);
static const uint8_t HA_BG_WS_Q_PRESSURE_PCT = 25;
static const uint8_t HA_BG_WS_Q_PROTECT_PCT = 50;
static const uint8_t HA_BG_WS_Q_CRITICAL_PCT = 75;
static const int64_t HA_BG_INTERVAL_INITIAL_NORMAL_MS = 200;
static const int64_t HA_BG_INTERVAL_INITIAL_PRESSURE_MS = 500;
static const int64_t HA_BG_INTERVAL_INITIAL_PROTECT_MS = 1500;
static const int64_t HA_BG_INTERVAL_INITIAL_CRITICAL_MS = 3000;
static const int64_t HA_BG_INTERVAL_PRIORITY_NORMAL_MS = 300;
static const int64_t HA_BG_INTERVAL_PRIORITY_PRESSURE_MS = 700;
static const int64_t HA_BG_INTERVAL_PRIORITY_PROTECT_MS = 1500;
static const int64_t HA_BG_INTERVAL_PRIORITY_CRITICAL_MS = 3000;
static const int64_t HA_BG_INTERVAL_PERIODIC_NORMAL_MS = 1800000;
static const int64_t HA_BG_INTERVAL_PERIODIC_PRESSURE_MS = 2700000;
static const int64_t HA_BG_INTERVAL_PERIODIC_PROTECT_MS = 3600000;
static const int64_t HA_BG_INTERVAL_PERIODIC_CRITICAL_MS = 5400000;
static const int64_t HA_HTTP_BUDGET_WINDOW_MS = 60000;
static const int64_t HA_HTTP_BUDGET_LOG_INTERVAL_MS = 300000;
static const int64_t HA_BG_BUDGET_CHANGE_LOG_MIN_MS = 10000;
/* Short "quiet window" after a user-initiated service call during which
 * background heavy requests (weather forecast, energy prefs/stats) are held
 * back so the service-call round-trip (call -> result -> state_changed)
 * completes cleanly.  With the central WS/TLS send gate now serializing
 * heavy requests (HA_WS_HEAVY_MIN_GAP_MS) a long boost is no longer needed;
 * 1500 ms covers the typical svc RTT + state_changed without piling up when
 * the user toggles multiple entities in rapid succession. */
static const int64_t HA_WS_PRIORITY_BOOST_MS = 1500;
static const int64_t HA_WEATHER_FORECAST_RETRY_MIN_MS = 300000;
static const int64_t HA_SVC_LATENCY_INFO_MS = 0;
static const int64_t HA_SVC_LATENCY_WARN_MS = 500;
static const int64_t HA_SVC_TRACE_MAX_AGE_MS = 5000;
static void ha_client_handle_text_message(const char *data, int len);
static bool ha_client_import_state_object(cJSON *state_obj);
static void ha_client_publish_event(app_event_type_t type, const char *entity_id);
static int64_t ha_client_ping_interval_ms_effective(void);
static esp_err_t ha_client_sync_layout_entity_step(bool is_initial, uint32_t *io_index, uint32_t *out_count,
    uint32_t *io_imported, bool *out_done, bool allow_http_when_rest_disabled);
static void safe_copy_cstr(char *dst, size_t dst_size, const char *src);
static bool ha_client_is_tls_bad_input_data(int tls_stack_err);
static void ha_client_priority_sync_queue_push_locked(const char *entity_id);
static size_t ha_client_collect_layout_entity_ids(char *entity_ids, size_t max_count, bool *out_need_weather_forecast);
static bool ha_client_layout_needs_ha_energy(void);
static bool ha_client_entity_is_weather(const char *entity_id);
static bool ha_client_entity_id_in_list(const char *entity_ids, size_t entity_count, const char *entity_id);
static void ha_client_queue_weather_priority_sync_from_layout(int64_t now_ms);
static ha_bg_budget_level_t ha_client_eval_bg_budget_level(
    size_t free_internal, uint8_t ws_q_fill_pct, uint32_t ws_error_streak);
static int64_t ha_client_interval_initial_step_ms(ha_bg_budget_level_t level);
static int64_t ha_client_interval_priority_step_ms(ha_bg_budget_level_t level);
static int64_t ha_client_interval_periodic_step_ms(ha_bg_budget_level_t level);
static void ha_client_update_bg_budget_state(
    ha_bg_budget_level_t level, size_t free_internal, uint8_t ws_q_fill_pct, uint32_t ws_error_streak, int64_t now_ms);
static bool ha_client_should_defer_bg_http(ha_bg_budget_level_t level, int64_t now_ms, int64_t *out_wait_ms);
static esp_err_t ha_client_http_open_budgeted(esp_http_client_handle_t client, int write_len, const char *reason);
static void ha_client_refresh_layout_capabilities(void);
static bool ha_client_capture_layout_snapshot(
    uint32_t *out_signature, uint16_t *out_count, bool *out_need_weather_forecast);
static bool ha_client_rest_enabled(void);
static esp_err_t ha_client_send_subscribe_single_entity(const char *entity_id, uint32_t *out_req_id);
static esp_err_t ha_client_send_weather_daily_forecast_ws(const char *entity_id, uint32_t *out_req_id);
static esp_err_t ha_client_send_energy_prefs_ws(uint32_t *out_req_id);
static esp_err_t ha_client_send_energy_stats_ws(uint32_t *out_req_id);
static esp_err_t ha_client_send_light_discovery_request(ha_light_discovery_phase_t phase);
static void ha_client_mark_entities_seen(const char *entity_id);
static esp_err_t ha_client_ensure_entities_sub_buffers(void);
static void ha_client_clear_entities_sub_buffers(void);
static void ha_client_free_entities_sub_buffers(void);
static char *ha_client_entities_sub_target_at(uint16_t idx);
static char *ha_client_entities_sub_seen_at(uint16_t idx);
static bool ha_client_light_discovery_has_ready_cache_locked(void);
static bool ha_client_light_discovery_start_locked(const char *domain, const char *search, int64_t now_ms);
static bool ha_client_light_discovery_handle_result(uint32_t msg_id, cJSON *root, cJSON *success_item);
static int ha_client_light_discovery_item_cmp(const void *lhs, const void *rhs);
static esp_err_t ha_client_light_discovery_template_step(uint16_t offset, uint16_t page_size);
static uint16_t ha_client_prepare_entities_resubscribe_locked(int64_t now_ms);
static const size_t HA_WS_RX_ASSEMBLY_BUF_SIZE = 65536U;
static char *s_ws_rx_buf = NULL;
static size_t s_ws_rx_buf_cap = 0;
static int s_ws_rx_len = 0;
static int s_ws_rx_expected_len = 0;
static bool s_ws_rx_overflow = false;

static esp_err_t ha_client_force_recover_with_escalation(bool prefer_transport, const char *reason, bool *out_used_transport)
{
    if (out_used_transport != NULL) {
        *out_used_transport = prefer_transport;
    }

    if (prefer_transport) {
        return wifi_mgr_force_transport_recover();
    }

    esp_err_t err = wifi_mgr_force_reconnect();
    if (err == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGW(TAG_HA_CLIENT,
        "Wi-Fi reconnect recover failed (%s): %s, escalating to C6 transport recover",
        (reason != NULL) ? reason : "unknown",
        esp_err_to_name(err));

    esp_err_t transport_err = wifi_mgr_force_transport_recover();
    if (transport_err == ESP_OK) {
        if (out_used_transport != NULL) {
            *out_used_transport = true;
        }
        return ESP_OK;
    }

    ESP_LOGW(TAG_HA_CLIENT,
        "C6 transport recover failed after reconnect failure (%s): %s",
        (reason != NULL) ? reason : "unknown",
        esp_err_to_name(transport_err));
    return transport_err;
}

static void ha_client_free_ws_msg(ha_ws_rx_msg_t *msg)
{
    if (msg == NULL) {
        return;
    }
    if (msg->payload != NULL) {
        free(msg->payload);
        msg->payload = NULL;
    }
    msg->len = 0;
}

static void ha_client_flush_ws_rx_queue(void)
{
    if (s_client.ws_rx_queue == NULL) {
        return;
    }
    ha_ws_rx_msg_t msg = {0};
    while (xQueueReceive(s_client.ws_rx_queue, &msg, 0) == pdTRUE) {
        ha_client_free_ws_msg(&msg);
    }
}

static void ha_client_enqueue_ws_text(const char *data, int len)
{
    if (data == NULL || len <= 0 || s_client.ws_rx_queue == NULL) {
        return;
    }

    ha_ws_rx_msg_t msg = {0};
    /* Keep internal heap headroom for TLS by preferring PSRAM for queued WS payloads. */
    msg.payload = (char *)heap_caps_malloc((size_t)len + 1U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (msg.payload == NULL) {
        msg.payload = (char *)malloc((size_t)len + 1U);
    }
    if (msg.payload == NULL) {
        ESP_LOGW(TAG_HA_CLIENT, "Drop WS message: out of memory (len=%d)", len);
        return;
    }
    memcpy(msg.payload, data, (size_t)len);
    msg.payload[len] = '\0';
    msg.len = len;

    if (xQueueSend(s_client.ws_rx_queue, &msg, 0) != pdTRUE) {
        /* Keep freshest state changes: drop oldest queued message and retry once. */
        ha_ws_rx_msg_t dropped = {0};
        if (xQueueReceive(s_client.ws_rx_queue, &dropped, 0) == pdTRUE) {
            ha_client_free_ws_msg(&dropped);
            if (xQueueSend(s_client.ws_rx_queue, &msg, 0) == pdTRUE) {
                ESP_LOGW(TAG_HA_CLIENT, "WS rx queue full: dropped oldest message to keep latest (len=%d)", len);
                return;
            }
        }
        ESP_LOGW(TAG_HA_CLIENT, "Drop WS message: rx queue full (len=%d)", len);
        ha_client_free_ws_msg(&msg);
    }
}

static int64_t ha_client_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void ha_client_log_mem_snapshot(const char *phase, bool warn_level)
{
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t min_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t free_heap8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    if (warn_level || free_internal < HA_BG_HEAP_PRESSURE_BYTES || largest_internal < (6U * 1024U)) {
        ESP_LOGW(TAG_HA_CLIENT,
            "mem[%s] int_free=%u int_largest=%u int_min=%u heap8_free=%u psram_free=%u",
            (phase != NULL) ? phase : "n/a",
            (unsigned)free_internal,
            (unsigned)largest_internal,
            (unsigned)min_internal,
            (unsigned)free_heap8,
            (unsigned)free_psram);
    } else {
        ESP_LOGI(TAG_HA_CLIENT,
            "mem[%s] int_free=%u int_largest=%u int_min=%u heap8_free=%u psram_free=%u",
            (phase != NULL) ? phase : "n/a",
            (unsigned)free_internal,
            (unsigned)largest_internal,
            (unsigned)min_internal,
            (unsigned)free_heap8,
            (unsigned)free_psram);
    }
}

static int64_t ha_client_ping_interval_ms_effective(void)
{
    if ((int64_t)APP_HA_PING_INTERVAL_MS < HA_WS_PING_INTERVAL_MIN_MS) {
        return HA_WS_PING_INTERVAL_MIN_MS;
    }
    return (int64_t)APP_HA_PING_INTERVAL_MS;
}

static bool ha_client_is_tls_bad_input_data(int tls_stack_err)
{
    return (tls_stack_err == HA_WS_TLS_ERR_BAD_INPUT_DATA) ||
           (tls_stack_err == -HA_WS_TLS_ERR_BAD_INPUT_DATA);
}

static int64_t ha_client_ping_timeout_ms(void)
{
    int64_t timeout_ms = ha_client_ping_interval_ms_effective() * 4;
    if (timeout_ms < HA_WS_PING_TIMEOUT_MIN_MS) {
        timeout_ms = HA_WS_PING_TIMEOUT_MIN_MS;
    }
    return timeout_ms;
}

static const char *ha_client_bg_budget_level_name(ha_bg_budget_level_t level)
{
    switch (level) {
    case HA_BG_BUDGET_NORMAL:
        return "normal";
    case HA_BG_BUDGET_PRESSURE:
        return "pressure";
    case HA_BG_BUDGET_PROTECT:
        return "protect";
    case HA_BG_BUDGET_CRITICAL:
        return "critical";
    default:
        return "unknown";
    }
}

static ha_bg_budget_level_t ha_client_eval_bg_budget_level(
    size_t free_internal, uint8_t ws_q_fill_pct, uint32_t ws_error_streak)
{
    ha_bg_budget_level_t level = HA_BG_BUDGET_NORMAL;

    if (free_internal < HA_BG_HEAP_CRITICAL_BYTES || ws_q_fill_pct >= HA_BG_WS_Q_CRITICAL_PCT) {
        level = HA_BG_BUDGET_CRITICAL;
    } else if (free_internal < HA_BG_HEAP_PROTECT_BYTES || ws_q_fill_pct >= HA_BG_WS_Q_PROTECT_PCT) {
        level = HA_BG_BUDGET_PROTECT;
    } else if (free_internal < HA_BG_HEAP_PRESSURE_BYTES || ws_q_fill_pct >= HA_BG_WS_Q_PRESSURE_PCT) {
        level = HA_BG_BUDGET_PRESSURE;
    }

    if (ws_error_streak >= HA_WS_ERROR_STREAK_TRANSPORT_RECOVER_THRESHOLD) {
        if (level < HA_BG_BUDGET_CRITICAL) {
            level = HA_BG_BUDGET_CRITICAL;
        }
    } else if (ws_error_streak >= HA_WS_ERROR_STREAK_WIFI_RECOVER_THRESHOLD) {
        if (level < HA_BG_BUDGET_PROTECT) {
            level = HA_BG_BUDGET_PROTECT;
        }
    }

    return level;
}

static int64_t ha_client_interval_initial_step_ms(ha_bg_budget_level_t level)
{
    switch (level) {
    case HA_BG_BUDGET_PRESSURE:
        return HA_BG_INTERVAL_INITIAL_PRESSURE_MS;
    case HA_BG_BUDGET_PROTECT:
        return HA_BG_INTERVAL_INITIAL_PROTECT_MS;
    case HA_BG_BUDGET_CRITICAL:
        return HA_BG_INTERVAL_INITIAL_CRITICAL_MS;
    case HA_BG_BUDGET_NORMAL:
    default:
        return HA_BG_INTERVAL_INITIAL_NORMAL_MS;
    }
}

static int64_t ha_client_interval_priority_step_ms(ha_bg_budget_level_t level)
{
    switch (level) {
    case HA_BG_BUDGET_PRESSURE:
        return HA_BG_INTERVAL_PRIORITY_PRESSURE_MS;
    case HA_BG_BUDGET_PROTECT:
        return HA_BG_INTERVAL_PRIORITY_PROTECT_MS;
    case HA_BG_BUDGET_CRITICAL:
        return HA_BG_INTERVAL_PRIORITY_CRITICAL_MS;
    case HA_BG_BUDGET_NORMAL:
    default:
        return HA_BG_INTERVAL_PRIORITY_NORMAL_MS;
    }
}

static int64_t ha_client_interval_periodic_step_ms(ha_bg_budget_level_t level)
{
    switch (level) {
    case HA_BG_BUDGET_PRESSURE:
        return HA_BG_INTERVAL_PERIODIC_PRESSURE_MS;
    case HA_BG_BUDGET_PROTECT:
        return HA_BG_INTERVAL_PERIODIC_PROTECT_MS;
    case HA_BG_BUDGET_CRITICAL:
        return HA_BG_INTERVAL_PERIODIC_CRITICAL_MS;
    case HA_BG_BUDGET_NORMAL:
    default:
        return HA_BG_INTERVAL_PERIODIC_NORMAL_MS;
    }
}

static uint32_t ha_client_http_open_budget_per_minute(ha_bg_budget_level_t level)
{
    switch (level) {
    case HA_BG_BUDGET_PRESSURE:
        return 40U;
    case HA_BG_BUDGET_PROTECT:
        return 12U;
    case HA_BG_BUDGET_CRITICAL:
        return 4U;
    case HA_BG_BUDGET_NORMAL:
    default:
        return 120U;
    }
}

static void ha_client_update_bg_budget_state(
    ha_bg_budget_level_t level, size_t free_internal, uint8_t ws_q_fill_pct, uint32_t ws_error_streak, int64_t now_ms)
{
    if (s_client.mutex == NULL) {
        return;
    }

    bool changed = false;
    bool should_log = false;
    int64_t last_log_ms = 0;
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    if (s_client.bg_budget_level != level) {
        s_client.bg_budget_level = level;
        s_client.bg_budget_level_since_unix_ms = now_ms;
        s_client.bg_budget_level_change_count++;
        changed = true;
    }
    last_log_ms = s_client.bg_budget_last_log_unix_ms;
    if ((changed && (now_ms - last_log_ms) >= HA_BG_BUDGET_CHANGE_LOG_MIN_MS) ||
        (level != HA_BG_BUDGET_NORMAL && (now_ms - last_log_ms) >= HA_HTTP_BUDGET_LOG_INTERVAL_MS)) {
        s_client.bg_budget_last_log_unix_ms = now_ms;
        should_log = true;
    }
    xSemaphoreGive(s_client.mutex);

    if (should_log) {
        size_t q_used = 0;
        size_t q_cap = APP_HA_QUEUE_LENGTH;
        if (s_client.ws_rx_queue != NULL) {
            q_used = (size_t)uxQueueMessagesWaiting(s_client.ws_rx_queue);
        }
        ESP_LOGW(TAG_HA_CLIENT,
            "BG budget=%s free_internal=%u ws_q=%u/%u (%u%%) ws_err_streak=%u",
            ha_client_bg_budget_level_name(level),
            (unsigned)free_internal,
            (unsigned)q_used,
            (unsigned)q_cap,
            (unsigned)ws_q_fill_pct,
            (unsigned)ws_error_streak);
    }
}

static bool ha_client_should_defer_bg_http(ha_bg_budget_level_t level, int64_t now_ms, int64_t *out_wait_ms)
{
    int64_t wait_ms = 0;

    if (s_client.mutex != NULL) {
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        if (s_client.http_open_window_start_unix_ms == 0 ||
            (now_ms - s_client.http_open_window_start_unix_ms) >= HA_HTTP_BUDGET_WINDOW_MS) {
            s_client.http_open_window_start_unix_ms = now_ms;
            s_client.http_open_count_window = 0;
            s_client.http_open_fail_count_window = 0;
        }

        uint32_t budget = ha_client_http_open_budget_per_minute(level);
        if (budget == 0U) {
            if (ha_client_interval_initial_step_ms(level) > wait_ms) {
                wait_ms = ha_client_interval_initial_step_ms(level);
            }
        } else if (s_client.http_open_count_window >= budget) {
            int64_t budget_wait = HA_HTTP_BUDGET_WINDOW_MS - (now_ms - s_client.http_open_window_start_unix_ms);
            if (budget_wait < 250) {
                budget_wait = 250;
            }
            if (budget_wait > wait_ms) {
                wait_ms = budget_wait;
            }
        }

        if (s_client.http_open_cooldown_until_unix_ms > now_ms) {
            int64_t cooldown_wait = s_client.http_open_cooldown_until_unix_ms - now_ms;
            if (cooldown_wait > wait_ms) {
                wait_ms = cooldown_wait;
            }
        }
        xSemaphoreGive(s_client.mutex);
    }

    if (wait_ms > 0) {
        if (out_wait_ms != NULL) {
            *out_wait_ms = wait_ms;
        }
        return true;
    }
    return false;
}

static void ha_client_queue_weather_priority_sync_from_layout(int64_t now_ms)
{
    if (now_ms <= 0) {
        now_ms = ha_client_now_ms();
    }

    size_t max_entities = (size_t)HA_LAYOUT_ENTITY_MAX;
    char *entity_ids = calloc(max_entities, APP_MAX_ENTITY_ID_LEN);
    if (entity_ids == NULL) {
        return;
    }

    bool need_weather_forecast = false;
    size_t entity_count = ha_client_collect_layout_entity_ids(entity_ids, max_entities, &need_weather_forecast);
    if (!need_weather_forecast || entity_count == 0) {
        free(entity_ids);
        return;
    }

    uint32_t queued_count = 0;
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    for (size_t i = 0; i < entity_count; i++) {
        const char *entity_id = entity_ids + (i * APP_MAX_ENTITY_ID_LEN);
        if (!ha_client_entity_is_weather(entity_id)) {
            continue;
        }
        ha_client_priority_sync_queue_push_locked(entity_id);
        queued_count++;
    }

    int64_t ready_ms = now_ms;
    if (s_client.ws_last_connected_unix_ms > 0) {
        int64_t grace_until = s_client.ws_last_connected_unix_ms + HA_WS_WEATHER_PRIORITY_GRACE_MS;
        if (ready_ms < grace_until) {
            ready_ms = grace_until;
        }
    }
    if (s_client.priority_sync_count > 0 &&
        (s_client.next_priority_sync_unix_ms == 0 || s_client.next_priority_sync_unix_ms > ready_ms)) {
        s_client.next_priority_sync_unix_ms = ready_ms;
    }
    if (s_client.next_weather_forecast_retry_unix_ms < now_ms) {
        s_client.next_weather_forecast_retry_unix_ms = now_ms + HA_WEATHER_FORECAST_RETRY_MIN_MS;
    }
    xSemaphoreGive(s_client.mutex);

    if (queued_count > 0) {
        ESP_LOGI(TAG_HA_CLIENT, "Queued weather WS forecast sync for %u layout entities", (unsigned)queued_count);
    }

    free(entity_ids);
}

static esp_err_t ha_client_http_open_budgeted(esp_http_client_handle_t client, int write_len, const char *reason)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t now_ms = ha_client_now_ms();
    ha_bg_budget_level_t level = HA_BG_BUDGET_NORMAL;
    uint32_t open_budget = 0;
    uint32_t open_count = 0;
    int64_t wait_ms = 0;
    bool allowed = true;

    if (s_client.mutex != NULL) {
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        level = s_client.bg_budget_level;

        open_budget = ha_client_http_open_budget_per_minute(level);

        if (s_client.http_open_window_start_unix_ms == 0 ||
            (now_ms - s_client.http_open_window_start_unix_ms) >= HA_HTTP_BUDGET_WINDOW_MS) {
            s_client.http_open_window_start_unix_ms = now_ms;
            s_client.http_open_count_window = 0;
            s_client.http_open_fail_count_window = 0;
        }

        if (s_client.http_open_cooldown_until_unix_ms > now_ms) {
            allowed = false;
            wait_ms = s_client.http_open_cooldown_until_unix_ms - now_ms;
        } else {
            open_count = s_client.http_open_count_window;
            if (open_budget == 0U || open_count >= open_budget) {
                allowed = false;
                wait_ms = HA_HTTP_BUDGET_WINDOW_MS - (now_ms - s_client.http_open_window_start_unix_ms);
                if (wait_ms < 250) {
                    wait_ms = 250;
                }
                s_client.http_open_cooldown_until_unix_ms = now_ms + wait_ms;
            } else {
                s_client.http_open_count_window++;
            }
        }
        xSemaphoreGive(s_client.mutex);
    }

    if (!allowed) {
        ESP_LOGW(TAG_HA_CLIENT,
            "HTTP open budget blocked (%s): budget=%u/min level=%s wait=%" PRId64 " ms",
            (reason != NULL) ? reason : "n/a",
            (unsigned)open_budget,
            ha_client_bg_budget_level_name(level),
            wait_ms);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = esp_http_client_open(client, write_len);
    if (s_client.mutex != NULL) {
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        if (err == ESP_OK) {
            s_client.http_open_fail_streak = 0;
        } else {
            if (s_client.http_open_fail_count_window < UINT32_MAX) {
                s_client.http_open_fail_count_window++;
            }
            if (s_client.http_open_fail_streak < UINT8_MAX) {
                s_client.http_open_fail_streak++;
            }

            int64_t cooldown_ms = 0;
            if (s_client.http_open_fail_streak >= 4) {
                cooldown_ms = 20000;
            } else if (s_client.http_open_fail_streak >= 3) {
                cooldown_ms = 10000;
            }
            if (cooldown_ms > 0) {
                int64_t until_ms = now_ms + cooldown_ms;
                if (until_ms > s_client.http_open_cooldown_until_unix_ms) {
                    s_client.http_open_cooldown_until_unix_ms = until_ms;
                }
            }
        }
        xSemaphoreGive(s_client.mutex);
    }

    return err;
}

static char *ha_client_entities_sub_target_at(uint16_t idx)
{
    if (s_client.entities_sub_targets == NULL || idx >= HA_WS_ENTITIES_SUB_MAX) {
        return NULL;
    }
    return s_client.entities_sub_targets + ((size_t)idx * (size_t)APP_MAX_ENTITY_ID_LEN);
}

static char *ha_client_entities_sub_seen_at(uint16_t idx)
{
    if (s_client.entities_sub_seen == NULL || idx >= HA_WS_ENTITIES_SUB_MAX) {
        return NULL;
    }
    return s_client.entities_sub_seen + ((size_t)idx * (size_t)APP_MAX_ENTITY_ID_LEN);
}

static void ha_client_clear_entities_sub_buffers(void)
{
    if (s_client.entities_sub_targets != NULL) {
        memset(s_client.entities_sub_targets, 0, HA_WS_ENTITIES_SUB_ID_BYTES);
    }
    if (s_client.entities_sub_req_ids != NULL) {
        memset(s_client.entities_sub_req_ids, 0, HA_WS_ENTITIES_SUB_REQ_BYTES);
    }
    if (s_client.entities_sub_seen != NULL) {
        memset(s_client.entities_sub_seen, 0, HA_WS_ENTITIES_SUB_ID_BYTES);
    }
}

static uint16_t ha_client_prepare_entities_resubscribe_locked(int64_t now_ms)
{
    size_t max_entities = (size_t)HA_LAYOUT_ENTITY_MAX;
    char *entity_ids = calloc(max_entities, APP_MAX_ENTITY_ID_LEN);
    size_t entity_count = 0;

    if (entity_ids != NULL) {
        bool need_weather_forecast = false;
        entity_count = ha_client_collect_layout_entity_ids(entity_ids, max_entities, &need_weather_forecast);
        s_client.layout_needs_weather_forecast = need_weather_forecast;
    }

    uint16_t target_count =
        (entity_count > HA_WS_ENTITIES_SUB_MAX) ? HA_WS_ENTITIES_SUB_MAX : (uint16_t)entity_count;
    ha_client_clear_entities_sub_buffers();
    for (uint16_t i = 0; i < target_count; i++) {
        char *target = ha_client_entities_sub_target_at(i);
        if (target != NULL) {
            safe_copy_cstr(target, APP_MAX_ENTITY_ID_LEN, entity_ids + ((size_t)i * APP_MAX_ENTITY_ID_LEN));
        }
    }
    s_client.entities_sub_target_count = target_count;
    s_client.entities_sub_sent_count = 0;
    s_client.entities_sub_seen_count = 0;
    s_client.next_entities_subscribe_unix_ms = now_ms;
    s_client.sub_state_via_entities = false;
    s_client.entities_sub_req_id = 0;
    s_client.pending_subscribe = APP_HA_SUBSCRIBE_STATE_CHANGED && (target_count > 0);

    if (entity_ids != NULL) {
        free(entity_ids);
    }

    return target_count;
}

static void ha_client_free_entities_sub_buffers(void)
{
    if (s_client.entities_sub_targets != NULL) {
        heap_caps_free(s_client.entities_sub_targets);
        s_client.entities_sub_targets = NULL;
    }
    if (s_client.entities_sub_req_ids != NULL) {
        heap_caps_free(s_client.entities_sub_req_ids);
        s_client.entities_sub_req_ids = NULL;
    }
    if (s_client.entities_sub_seen != NULL) {
        heap_caps_free(s_client.entities_sub_seen);
        s_client.entities_sub_seen = NULL;
    }
}

static esp_err_t ha_client_ensure_entities_sub_buffers(void)
{
    if (s_client.entities_sub_targets != NULL &&
        s_client.entities_sub_req_ids != NULL &&
        s_client.entities_sub_seen != NULL) {
        return ESP_OK;
    }

    char *targets = s_client.entities_sub_targets;
    uint32_t *req_ids = s_client.entities_sub_req_ids;
    char *seen = s_client.entities_sub_seen;
    bool alloc_targets = false;
    bool alloc_req_ids = false;
    bool alloc_seen = false;

    if (targets == NULL) {
        targets = (char *)heap_caps_malloc(HA_WS_ENTITIES_SUB_ID_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (targets == NULL) {
            targets = (char *)heap_caps_malloc(HA_WS_ENTITIES_SUB_ID_BYTES, MALLOC_CAP_8BIT);
        }
        if (targets == NULL) {
            goto fail;
        }
        alloc_targets = true;
    }
    if (req_ids == NULL) {
        req_ids = (uint32_t *)heap_caps_malloc(HA_WS_ENTITIES_SUB_REQ_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (req_ids == NULL) {
            req_ids = (uint32_t *)heap_caps_malloc(HA_WS_ENTITIES_SUB_REQ_BYTES, MALLOC_CAP_8BIT);
        }
        if (req_ids == NULL) {
            goto fail;
        }
        alloc_req_ids = true;
    }
    if (seen == NULL) {
        seen = (char *)heap_caps_malloc(HA_WS_ENTITIES_SUB_ID_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (seen == NULL) {
            seen = (char *)heap_caps_malloc(HA_WS_ENTITIES_SUB_ID_BYTES, MALLOC_CAP_8BIT);
        }
        if (seen == NULL) {
            goto fail;
        }
        alloc_seen = true;
    }

    s_client.entities_sub_targets = targets;
    s_client.entities_sub_req_ids = req_ids;
    s_client.entities_sub_seen = seen;
    ha_client_clear_entities_sub_buffers();
    return ESP_OK;

fail:
    if (alloc_targets && targets != NULL) {
        heap_caps_free(targets);
    }
    if (alloc_req_ids && req_ids != NULL) {
        heap_caps_free(req_ids);
    }
    if (alloc_seen && seen != NULL) {
        heap_caps_free(seen);
    }
    ESP_LOGE(TAG_HA_CLIENT, "Failed to allocate subscribe_entities buffers");
    return ESP_ERR_NO_MEM;
}

static esp_err_t ha_client_ensure_ws_rx_buffer(void)
{
    if (s_ws_rx_buf != NULL && s_ws_rx_buf_cap > 0U) {
        return ESP_OK;
    }

    char *buf = (char *)heap_caps_malloc(HA_WS_RX_ASSEMBLY_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        /* Fallback only if PSRAM allocation is unavailable. */
        buf = (char *)heap_caps_malloc(HA_WS_RX_ASSEMBLY_BUF_SIZE, MALLOC_CAP_8BIT);
    }
    if (buf == NULL) {
        ESP_LOGE(TAG_HA_CLIENT, "Failed to allocate WS RX assembly buffer (%u bytes)", (unsigned)HA_WS_RX_ASSEMBLY_BUF_SIZE);
        return ESP_ERR_NO_MEM;
    }

    s_ws_rx_buf = buf;
    s_ws_rx_buf_cap = HA_WS_RX_ASSEMBLY_BUF_SIZE;
    s_ws_rx_buf[0] = '\0';
    return ESP_OK;
}

static void ha_client_reset_ws_rx_assembly(void)
{
    s_ws_rx_len = 0;
    s_ws_rx_expected_len = 0;
    s_ws_rx_overflow = false;
    if (s_ws_rx_buf != NULL && s_ws_rx_buf_cap > 0U) {
        s_ws_rx_buf[0] = '\0';
    }
}

static void ha_client_handle_text_chunk(const ha_ws_event_t *event)
{
    if (event == NULL) {
        return;
    }

    if (ha_client_ensure_ws_rx_buffer() != ESP_OK) {
        return;
    }

    int chunk_len = event->data_len;
    if (chunk_len < 0) {
        ESP_LOGW(TAG_HA_CLIENT, "Dropped WS chunk with invalid len=%d", event->data_len);
        return;
    }

    if (event->payload_offset == 0) {
        ha_client_reset_ws_rx_assembly();
        if (event->payload_len > 0) {
            s_ws_rx_expected_len = event->payload_len;
        } else {
            s_ws_rx_expected_len = chunk_len;
        }
    } else if (s_ws_rx_len == 0 && s_ws_rx_expected_len == 0) {
        ESP_LOGW(TAG_HA_CLIENT, "Dropped orphan WS chunk (offset=%d len=%d)", event->payload_offset, chunk_len);
        return;
    }

    if (chunk_len > 0) {
        if (event->data == NULL) {
            s_ws_rx_overflow = true;
            ESP_LOGW(TAG_HA_CLIENT, "WS chunk payload missing (offset=%d len=%d), dropping message",
                event->payload_offset, chunk_len);
        } else if (!s_ws_rx_overflow) {
            int space = (int)s_ws_rx_buf_cap - 1 - s_ws_rx_len;
            if (space < chunk_len) {
                s_ws_rx_overflow = true;
                ESP_LOGW(TAG_HA_CLIENT, "WS message too large for buffer (%d > %d), dropping fragmented message",
                    s_ws_rx_len + chunk_len, (int)s_ws_rx_buf_cap - 1);
            } else {
                memcpy(&s_ws_rx_buf[s_ws_rx_len], event->data, (size_t)chunk_len);
                s_ws_rx_len += chunk_len;
                s_ws_rx_buf[s_ws_rx_len] = '\0';
            }
        }
    }

    bool complete = false;
    if (event->fin) {
        complete = true;
    } else if (event->payload_len > 0 && (event->payload_offset + chunk_len) >= event->payload_len) {
        complete = true;
    } else if (s_ws_rx_expected_len > 0 && (event->payload_offset + chunk_len) >= s_ws_rx_expected_len) {
        complete = true;
    }

    if (!complete) {
        return;
    }

    if (!s_ws_rx_overflow && s_ws_rx_len > 0) {
        ha_client_enqueue_ws_text(s_ws_rx_buf, s_ws_rx_len);
    }
    ha_client_reset_ws_rx_assembly();
}

static bool ha_client_rest_enabled(void)
{
    bool enabled = false;
    if (s_client.mutex != NULL) {
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        enabled = s_client.rest_enabled;
        xSemaphoreGive(s_client.mutex);
    }
    return enabled;
}

static void safe_copy_cstr(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = strnlen(src, dst_size - 1U);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool ha_client_entity_is_weather(const char *entity_id)
{
    if (entity_id == NULL) {
        return false;
    }
    return strncmp(entity_id, "weather.", 8) == 0;
}

static bool ha_client_entity_is_light(const char *entity_id)
{
    if (entity_id == NULL) {
        return false;
    }
    return strncmp(entity_id, "light.", 6) == 0;
}

static bool ha_client_discovery_domain_supported(const char *domain)
{
    if (domain == NULL) {
        return false;
    }
    return strcmp(domain, "light") == 0 ||
           strcmp(domain, "sensor") == 0 ||
           strcmp(domain, "switch") == 0 ||
           strcmp(domain, "weather") == 0 ||
           strcmp(domain, "climate") == 0;
}

static const char *ha_client_discovery_domain_or_default(const char *domain)
{
    return ha_client_discovery_domain_supported(domain) ? domain : "light";
}

static bool ha_client_entity_matches_domain(const char *entity_id, const char *domain)
{
    const char *checked_domain = ha_client_discovery_domain_or_default(domain);
    if (entity_id == NULL || entity_id[0] == '\0') {
        return false;
    }
    size_t domain_len = strlen(checked_domain);
    return strncmp(entity_id, checked_domain, domain_len) == 0 && entity_id[domain_len] == '.';
}

static void ha_client_discovery_normalize_search(const char *src, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }
    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') {
        src++;
    }
    safe_copy_cstr(dst, dst_size, src);
    size_t len = strlen(dst);
    while (len > 0) {
        char c = dst[len - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        dst[--len] = '\0';
    }
}

static bool ha_client_entity_is_climate(const char *entity_id)
{
    if (entity_id == NULL) {
        return false;
    }
    return strncmp(entity_id, "climate.", 8) == 0;
}

static bool ha_client_entity_is_media_player(const char *entity_id)
{
    if (entity_id == NULL) {
        return false;
    }
    return strncmp(entity_id, "media_player.", 13) == 0;
}

static bool ha_client_json_string_value(cJSON *obj, const char *key, char *dst, size_t dst_size)
{
    if (!cJSON_IsObject(obj) || key == NULL || dst == NULL || dst_size == 0) {
        return false;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0') {
        return false;
    }
    safe_copy_cstr(dst, dst_size, item->valuestring);
    return true;
}

static void *ha_client_light_discovery_calloc(size_t count, size_t size)
{
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_calloc(count, size, MALLOC_CAP_8BIT);
    }
    return ptr;
}

static bool ha_client_light_discovery_ensure_cache_locked(void)
{
    if (s_client.light_discovery_items == NULL) {
        s_client.light_discovery_items = (ha_light_discovery_item_t *)ha_client_light_discovery_calloc(
            APP_HA_LIGHT_DISCOVERY_MAX_ITEMS, sizeof(ha_light_discovery_item_t));
    }
    return s_client.light_discovery_items != NULL;
}

static bool ha_client_light_discovery_ensure_buffers_locked(void)
{
    if (!ha_client_light_discovery_ensure_cache_locked()) {
        return false;
    }
    if (s_client.light_discovery_pending_items == NULL) {
        s_client.light_discovery_pending_items =
            (ha_light_discovery_pending_item_t *)ha_client_light_discovery_calloc(
                APP_HA_LIGHT_DISCOVERY_MAX_ITEMS, sizeof(ha_light_discovery_pending_item_t));
    }
    if (s_client.light_discovery_areas == NULL) {
        s_client.light_discovery_areas = (ha_light_discovery_area_t *)ha_client_light_discovery_calloc(
            APP_HA_LIGHT_DISCOVERY_MAX_AREAS, sizeof(ha_light_discovery_area_t));
    }
    if (s_client.light_discovery_devices == NULL) {
        s_client.light_discovery_devices = (ha_light_discovery_device_t *)ha_client_light_discovery_calloc(
            APP_HA_LIGHT_DISCOVERY_MAX_DEVICES, sizeof(ha_light_discovery_device_t));
    }

    return s_client.light_discovery_items != NULL &&
           s_client.light_discovery_pending_items != NULL &&
           s_client.light_discovery_areas != NULL &&
           s_client.light_discovery_devices != NULL;
}

static bool ha_client_light_discovery_ensure_template_buffers_locked(void)
{
    if (!ha_client_light_discovery_ensure_cache_locked()) {
        return false;
    }
    if (s_client.light_discovery_pending_items == NULL) {
        s_client.light_discovery_pending_items =
            (ha_light_discovery_pending_item_t *)ha_client_light_discovery_calloc(
                APP_HA_LIGHT_DISCOVERY_MAX_ITEMS, sizeof(ha_light_discovery_pending_item_t));
    }
    return s_client.light_discovery_pending_items != NULL;
}

static void ha_client_light_discovery_reset_pending_locked(void)
{
    if (s_client.light_discovery_pending_items != NULL) {
        memset(s_client.light_discovery_pending_items, 0,
            sizeof(ha_light_discovery_pending_item_t) * APP_HA_LIGHT_DISCOVERY_MAX_ITEMS);
    }
    if (s_client.light_discovery_areas != NULL) {
        memset(s_client.light_discovery_areas, 0,
            sizeof(ha_light_discovery_area_t) * APP_HA_LIGHT_DISCOVERY_MAX_AREAS);
    }
    if (s_client.light_discovery_devices != NULL) {
        memset(s_client.light_discovery_devices, 0,
            sizeof(ha_light_discovery_device_t) * APP_HA_LIGHT_DISCOVERY_MAX_DEVICES);
    }
    s_client.light_discovery_pending_count = 0;
    s_client.light_discovery_pending_truncated = false;
    s_client.light_discovery_area_count = 0;
    s_client.light_discovery_area_truncated = false;
    s_client.light_discovery_device_count = 0;
    s_client.light_discovery_device_truncated = false;
    s_client.light_discovery_last_wait_log_unix_ms = 0;
    s_client.light_discovery_template_offset = 0;
}

static void ha_client_light_discovery_free_work_buffers_locked(void)
{
    if (s_client.light_discovery_pending_items != NULL) {
        heap_caps_free(s_client.light_discovery_pending_items);
        s_client.light_discovery_pending_items = NULL;
    }
    if (s_client.light_discovery_areas != NULL) {
        heap_caps_free(s_client.light_discovery_areas);
        s_client.light_discovery_areas = NULL;
    }
    if (s_client.light_discovery_devices != NULL) {
        heap_caps_free(s_client.light_discovery_devices);
        s_client.light_discovery_devices = NULL;
    }
    s_client.light_discovery_pending_count = 0;
    s_client.light_discovery_pending_truncated = false;
    s_client.light_discovery_area_count = 0;
    s_client.light_discovery_area_truncated = false;
    s_client.light_discovery_device_count = 0;
    s_client.light_discovery_device_truncated = false;
    s_client.light_discovery_last_wait_log_unix_ms = 0;
    s_client.light_discovery_template_offset = 0;
}

static bool ha_client_light_discovery_has_ready_cache_locked(void)
{
    return s_client.light_discovery_items != NULL &&
           s_client.light_discovery_updated_unix_ms > 0;
}

static bool ha_client_light_discovery_start_locked(const char *domain, const char *search, int64_t now_ms)
{
    const char *checked_domain = ha_client_discovery_domain_or_default(domain);
    char checked_search[APP_HA_DISCOVERY_SEARCH_MAX_LEN] = {0};
    ha_client_discovery_normalize_search(search, checked_search, sizeof(checked_search));
    bool same_domain =
        strncmp(s_client.light_discovery_domain, checked_domain, sizeof(s_client.light_discovery_domain)) == 0 &&
        strncmp(s_client.light_discovery_search, checked_search, sizeof(s_client.light_discovery_search)) == 0;
    bool have_buffers = APP_HA_LIGHT_DISCOVERY_TEMPLATE_ENABLED
        ? ha_client_light_discovery_ensure_template_buffers_locked()
        : ha_client_light_discovery_ensure_buffers_locked();
    if (!have_buffers) {
        ESP_LOGW(TAG_HA_CLIENT, "Entity discovery buffer allocation failed");
        ha_client_light_discovery_free_work_buffers_locked();
        return false;
    }
    if (!same_domain) {
        if (s_client.light_discovery_items != NULL) {
            memset(s_client.light_discovery_items, 0,
                sizeof(ha_light_discovery_item_t) * APP_HA_LIGHT_DISCOVERY_MAX_ITEMS);
        }
        s_client.light_discovery_count = 0;
        s_client.light_discovery_truncated = false;
        s_client.light_discovery_updated_unix_ms = 0;
    }
    safe_copy_cstr(s_client.light_discovery_domain, sizeof(s_client.light_discovery_domain), checked_domain);
    safe_copy_cstr(s_client.light_discovery_search, sizeof(s_client.light_discovery_search), checked_search);
    ha_client_light_discovery_reset_pending_locked();
    s_client.light_discovery_requested = true;
    s_client.light_discovery_inflight = false;
    s_client.light_discovery_phase = APP_HA_LIGHT_DISCOVERY_TEMPLATE_ENABLED
        ? HA_LIGHT_DISCOVERY_PHASE_TEMPLATE
        : HA_LIGHT_DISCOVERY_PHASE_ENTITY_DISPLAY;
    s_client.light_discovery_req_id = 0;
    s_client.light_discovery_started_unix_ms = now_ms;
    s_client.light_discovery_next_step_unix_ms = now_ms;
    s_client.light_discovery_template_offset = 0;
    s_client.light_discovery_template_total = 0;
    s_client.light_discovery_last_wait_log_unix_ms = 0;
    ESP_LOGI(TAG_HA_CLIENT, "Entity discovery queued: domain=%s search=\"%s\" mode=%s page_size=%u",
        s_client.light_discovery_domain,
        s_client.light_discovery_search,
        APP_HA_LIGHT_DISCOVERY_TEMPLATE_ENABLED ? "template-pages" : "registry-ws",
        (unsigned)APP_HA_LIGHT_DISCOVERY_PAGE_SIZE);
    return true;
}

static const char *ha_client_light_discovery_phase_name(ha_light_discovery_phase_t phase)
{
    switch (phase) {
    case HA_LIGHT_DISCOVERY_PHASE_TEMPLATE:
        return "template";
    case HA_LIGHT_DISCOVERY_PHASE_ENTITY_DISPLAY:
        return "entity_display";
    case HA_LIGHT_DISCOVERY_PHASE_ENTITY_FULL:
        return "entity_full";
    case HA_LIGHT_DISCOVERY_PHASE_AREAS:
        return "areas";
    case HA_LIGHT_DISCOVERY_PHASE_DEVICES:
        return "devices";
    default:
        return "idle";
    }
}

static bool ha_client_light_discovery_pending_contains_locked(const char *entity_id)
{
    if (entity_id == NULL || entity_id[0] == '\0' || s_client.light_discovery_pending_items == NULL) {
        return false;
    }
    for (uint16_t i = 0; i < s_client.light_discovery_pending_count; i++) {
        if (strncmp(s_client.light_discovery_pending_items[i].id, entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            return true;
        }
    }
    return false;
}

static void ha_client_light_discovery_add_pending_locked(const ha_light_discovery_pending_item_t *item)
{
    if (item == NULL || item->id[0] == '\0' ||
        !ha_client_entity_matches_domain(item->id, s_client.light_discovery_domain)) {
        return;
    }
    if (ha_client_light_discovery_pending_contains_locked(item->id)) {
        return;
    }
    if (s_client.light_discovery_pending_count >= APP_HA_LIGHT_DISCOVERY_MAX_ITEMS) {
        s_client.light_discovery_pending_truncated = true;
        return;
    }
    s_client.light_discovery_pending_items[s_client.light_discovery_pending_count++] = *item;
}

static void ha_client_light_discovery_parse_display_entities_locked(cJSON *result)
{
    cJSON *entities = cJSON_IsObject(result) ? cJSON_GetObjectItemCaseSensitive(result, "entities") : result;
    if (!cJSON_IsArray(entities)) {
        return;
    }

    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, entities)
    {
        if (!cJSON_IsObject(entry)) {
            continue;
        }
        cJSON *hidden_by = cJSON_GetObjectItemCaseSensitive(entry, "hb");
        if (cJSON_IsTrue(hidden_by)) {
            continue;
        }
        ha_light_discovery_pending_item_t item = {0};
        if (!ha_client_json_string_value(entry, "ei", item.id, sizeof(item.id)) ||
            !ha_client_entity_matches_domain(item.id, s_client.light_discovery_domain)) {
            continue;
        }
        if (!ha_client_json_string_value(entry, "en", item.name, sizeof(item.name))) {
            safe_copy_cstr(item.name, sizeof(item.name), item.id);
        }
        (void)ha_client_json_string_value(entry, "ai", item.area_id, sizeof(item.area_id));
        (void)ha_client_json_string_value(entry, "di", item.device_id, sizeof(item.device_id));
        (void)ha_client_json_string_value(entry, "ic", item.icon, sizeof(item.icon));
        ha_client_light_discovery_add_pending_locked(&item);
    }
}

static void ha_client_light_discovery_parse_full_entities_locked(cJSON *result)
{
    if (!cJSON_IsArray(result)) {
        return;
    }

    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, result)
    {
        if (!cJSON_IsObject(entry)) {
            continue;
        }
        cJSON *disabled_by = cJSON_GetObjectItemCaseSensitive(entry, "disabled_by");
        cJSON *hidden_by = cJSON_GetObjectItemCaseSensitive(entry, "hidden_by");
        if ((cJSON_IsString(disabled_by) && disabled_by->valuestring != NULL && disabled_by->valuestring[0] != '\0') ||
            (cJSON_IsString(hidden_by) && hidden_by->valuestring != NULL && hidden_by->valuestring[0] != '\0')) {
            continue;
        }
        ha_light_discovery_pending_item_t item = {0};
        if (!ha_client_json_string_value(entry, "entity_id", item.id, sizeof(item.id)) ||
            !ha_client_entity_matches_domain(item.id, s_client.light_discovery_domain)) {
            continue;
        }
        if (!ha_client_json_string_value(entry, "name", item.name, sizeof(item.name)) &&
            !ha_client_json_string_value(entry, "original_name", item.name, sizeof(item.name))) {
            safe_copy_cstr(item.name, sizeof(item.name), item.id);
        }
        (void)ha_client_json_string_value(entry, "area_id", item.area_id, sizeof(item.area_id));
        (void)ha_client_json_string_value(entry, "device_id", item.device_id, sizeof(item.device_id));
        (void)ha_client_json_string_value(entry, "icon", item.icon, sizeof(item.icon));
        ha_client_light_discovery_add_pending_locked(&item);
    }
}

static void ha_client_light_discovery_parse_areas_locked(cJSON *result)
{
    if (!cJSON_IsArray(result)) {
        return;
    }

    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, result)
    {
        if (!cJSON_IsObject(entry)) {
            continue;
        }
        if (s_client.light_discovery_area_count >= APP_HA_LIGHT_DISCOVERY_MAX_AREAS) {
            s_client.light_discovery_area_truncated = true;
            return;
        }
        ha_light_discovery_area_t area = {0};
        if (!ha_client_json_string_value(entry, "area_id", area.id, sizeof(area.id))) {
            continue;
        }
        if (!ha_client_json_string_value(entry, "name", area.name, sizeof(area.name))) {
            safe_copy_cstr(area.name, sizeof(area.name), area.id);
        }
        s_client.light_discovery_areas[s_client.light_discovery_area_count++] = area;
    }
}

static void ha_client_light_discovery_parse_devices_locked(cJSON *result)
{
    if (!cJSON_IsArray(result)) {
        return;
    }

    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, result)
    {
        if (!cJSON_IsObject(entry)) {
            continue;
        }
        if (s_client.light_discovery_device_count >= APP_HA_LIGHT_DISCOVERY_MAX_DEVICES) {
            s_client.light_discovery_device_truncated = true;
            return;
        }
        ha_light_discovery_device_t device = {0};
        if (!ha_client_json_string_value(entry, "id", device.id, sizeof(device.id))) {
            continue;
        }
        (void)ha_client_json_string_value(entry, "area_id", device.area_id, sizeof(device.area_id));
        s_client.light_discovery_devices[s_client.light_discovery_device_count++] = device;
    }
}

static const char *ha_client_light_discovery_area_name_locked(const char *area_id)
{
    if (area_id == NULL || area_id[0] == '\0' || s_client.light_discovery_areas == NULL) {
        return "";
    }
    for (uint16_t i = 0; i < s_client.light_discovery_area_count; i++) {
        const ha_light_discovery_area_t *area = &s_client.light_discovery_areas[i];
        if (strncmp(area->id, area_id, sizeof(area->id)) == 0) {
            return area->name[0] != '\0' ? area->name : area->id;
        }
    }
    return area_id;
}

static const char *ha_client_light_discovery_device_area_id_locked(const char *device_id)
{
    if (device_id == NULL || device_id[0] == '\0' || s_client.light_discovery_devices == NULL) {
        return "";
    }
    for (uint16_t i = 0; i < s_client.light_discovery_device_count; i++) {
        const ha_light_discovery_device_t *device = &s_client.light_discovery_devices[i];
        if (strncmp(device->id, device_id, sizeof(device->id)) == 0) {
            return device->area_id;
        }
    }
    return "";
}

static int ha_client_light_discovery_item_cmp(const void *lhs, const void *rhs)
{
    const ha_light_discovery_item_t *a = (const ha_light_discovery_item_t *)lhs;
    const ha_light_discovery_item_t *b = (const ha_light_discovery_item_t *)rhs;
    int room_cmp = strncmp(a->room, b->room, APP_MAX_NAME_LEN);
    if (room_cmp != 0) {
        if (a->room[0] == '\0') {
            return 1;
        }
        if (b->room[0] == '\0') {
            return -1;
        }
        return room_cmp;
    }
    int name_cmp = strncmp(a->name, b->name, APP_MAX_NAME_LEN);
    if (name_cmp != 0) {
        return name_cmp;
    }
    return strncmp(a->id, b->id, APP_MAX_ENTITY_ID_LEN);
}

static void ha_client_light_discovery_finish_locked(int64_t now_ms)
{
    if (!ha_client_light_discovery_ensure_cache_locked()) {
        return;
    }

    memset(s_client.light_discovery_items, 0,
        sizeof(ha_light_discovery_item_t) * APP_HA_LIGHT_DISCOVERY_MAX_ITEMS);
    s_client.light_discovery_count = 0;
    s_client.light_discovery_truncated =
        s_client.light_discovery_pending_truncated ||
        s_client.light_discovery_area_truncated ||
        s_client.light_discovery_device_truncated;

    for (uint16_t i = 0; i < s_client.light_discovery_pending_count; i++) {
        if (s_client.light_discovery_count >= APP_HA_LIGHT_DISCOVERY_MAX_ITEMS) {
            s_client.light_discovery_truncated = true;
            break;
        }
        const ha_light_discovery_pending_item_t *src = &s_client.light_discovery_pending_items[i];
        ha_light_discovery_item_t *dst = &s_client.light_discovery_items[s_client.light_discovery_count++];
        safe_copy_cstr(dst->id, sizeof(dst->id), src->id);
        safe_copy_cstr(dst->name, sizeof(dst->name), src->name[0] != '\0' ? src->name : src->id);
        safe_copy_cstr(dst->icon, sizeof(dst->icon), src->icon);

        const char *area_id = src->area_id;
        if (area_id[0] == '\0') {
            area_id = ha_client_light_discovery_device_area_id_locked(src->device_id);
        }
        safe_copy_cstr(dst->area_id, sizeof(dst->area_id), area_id);
        if (src->room[0] != '\0') {
            safe_copy_cstr(dst->room, sizeof(dst->room), src->room);
        } else {
            safe_copy_cstr(dst->room, sizeof(dst->room), ha_client_light_discovery_area_name_locked(area_id));
        }
    }

    if (s_client.light_discovery_count > 1) {
        qsort(s_client.light_discovery_items, s_client.light_discovery_count,
            sizeof(ha_light_discovery_item_t), ha_client_light_discovery_item_cmp);
    }

    s_client.light_discovery_updated_unix_ms = now_ms;
    s_client.light_discovery_requested = false;
    s_client.light_discovery_inflight = false;
    s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_NONE;
    s_client.light_discovery_req_id = 0;
    s_client.light_discovery_next_step_unix_ms = 0;
    s_client.light_discovery_last_wait_log_unix_ms = 0;
    s_client.light_discovery_template_offset = 0;
    ha_client_light_discovery_free_work_buffers_locked();
    ESP_LOGI(TAG_HA_CLIENT, "Entity discovery ready: domain=%s count=%u%s",
        s_client.light_discovery_domain,
        (unsigned)s_client.light_discovery_count,
        s_client.light_discovery_truncated ? " (truncated)" : "");
}

static void ha_client_light_discovery_abort_locked(void)
{
    s_client.light_discovery_requested = false;
    s_client.light_discovery_inflight = false;
    s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_NONE;
    s_client.light_discovery_domain[0] = '\0';
    s_client.light_discovery_search[0] = '\0';
    s_client.light_discovery_req_id = 0;
    s_client.light_discovery_next_step_unix_ms = 0;
    s_client.light_discovery_last_wait_log_unix_ms = 0;
    s_client.light_discovery_template_offset = 0;
    s_client.light_discovery_template_total = 0;
    ha_client_light_discovery_free_work_buffers_locked();
}

static esp_err_t ha_client_light_discovery_refresh_from_model(const char *domain, const char *search, int64_t now_ms)
{
    const char *checked_domain = ha_client_discovery_domain_or_default(domain);
    char checked_search[APP_HA_DISCOVERY_SEARCH_MAX_LEN] = {0};
    ha_client_discovery_normalize_search(search, checked_search, sizeof(checked_search));
    ha_entity_info_t *model_items = (ha_entity_info_t *)ha_client_light_discovery_calloc(
        APP_HA_LIGHT_DISCOVERY_MAX_ITEMS, sizeof(ha_entity_info_t));
    if (model_items == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t model_count = ha_model_list_entities(
        checked_domain, checked_search[0] != '\0' ? checked_search : NULL, model_items, APP_HA_LIGHT_DISCOVERY_MAX_ITEMS);
    if (model_count > APP_HA_LIGHT_DISCOVERY_MAX_ITEMS) {
        model_count = APP_HA_LIGHT_DISCOVERY_MAX_ITEMS;
    }

    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    if (!ha_client_light_discovery_ensure_cache_locked()) {
        xSemaphoreGive(s_client.mutex);
        free(model_items);
        return ESP_ERR_NO_MEM;
    }

    safe_copy_cstr(s_client.light_discovery_domain, sizeof(s_client.light_discovery_domain), checked_domain);
    safe_copy_cstr(s_client.light_discovery_search, sizeof(s_client.light_discovery_search), checked_search);
    memset(s_client.light_discovery_items, 0,
        sizeof(ha_light_discovery_item_t) * APP_HA_LIGHT_DISCOVERY_MAX_ITEMS);
    s_client.light_discovery_count = 0;
    s_client.light_discovery_truncated = (model_count >= APP_HA_LIGHT_DISCOVERY_MAX_ITEMS);
    for (size_t i = 0; i < model_count; i++) {
        const ha_entity_info_t *src = &model_items[i];
        if (src->id[0] == '\0' || !ha_client_entity_matches_domain(src->id, checked_domain)) {
            continue;
        }
        if (s_client.light_discovery_count >= APP_HA_LIGHT_DISCOVERY_MAX_ITEMS) {
            s_client.light_discovery_truncated = true;
            break;
        }
        ha_light_discovery_item_t *dst = &s_client.light_discovery_items[s_client.light_discovery_count++];
        safe_copy_cstr(dst->id, sizeof(dst->id), src->id);
        safe_copy_cstr(dst->name, sizeof(dst->name), src->name[0] != '\0' ? src->name : src->id);
        safe_copy_cstr(dst->icon, sizeof(dst->icon), src->icon);
    }
    if (s_client.light_discovery_count > 1) {
        qsort(s_client.light_discovery_items, s_client.light_discovery_count,
            sizeof(ha_light_discovery_item_t), ha_client_light_discovery_item_cmp);
    }

    s_client.light_discovery_updated_unix_ms = now_ms;
    s_client.light_discovery_requested = false;
    s_client.light_discovery_inflight = false;
    s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_NONE;
    s_client.light_discovery_req_id = 0;
    s_client.light_discovery_next_step_unix_ms = 0;
    s_client.light_discovery_last_wait_log_unix_ms = 0;
    s_client.light_discovery_template_offset = 0;
    s_client.light_discovery_template_total = (uint16_t)s_client.light_discovery_count;
    ha_client_light_discovery_free_work_buffers_locked();
    xSemaphoreGive(s_client.mutex);

    free(model_items);
    ESP_LOGI(TAG_HA_CLIENT, "Entity discovery served from HA model cache: domain=%s search=\"%s\" count=%u%s",
        checked_domain,
        checked_search,
        (unsigned)s_client.light_discovery_count,
        s_client.light_discovery_truncated ? " (truncated)" : "");
    return ESP_OK;
}

static bool ha_client_media_player_attr_key_is_tracked(const char *key)
{
    if (key == NULL || key[0] == '\0') {
        return false;
    }
    return (strcmp(key, "volume_level") == 0) || (strcmp(key, "is_volume_muted") == 0);
}

static bool ha_client_ws_media_player_change_can_skip(cJSON *changed_entry)
{
    if (!cJSON_IsObject(changed_entry)) {
        return false;
    }

    cJSON *plus_obj = cJSON_GetObjectItemCaseSensitive(changed_entry, "+");
    if (cJSON_IsObject(plus_obj)) {
        /* Any explicit state patch must be imported. */
        if (cJSON_GetObjectItemCaseSensitive(plus_obj, "s") != NULL) {
            return false;
        }

        cJSON *plus_attrs = cJSON_GetObjectItemCaseSensitive(plus_obj, "a");
        if (cJSON_IsObject(plus_attrs)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, plus_attrs)
            {
                if (item->string != NULL && ha_client_media_player_attr_key_is_tracked(item->string)) {
                    return false;
                }
            }
        }
    }

    cJSON *minus_obj = cJSON_GetObjectItemCaseSensitive(changed_entry, "-");
    if (cJSON_IsObject(minus_obj)) {
        if (cJSON_GetObjectItemCaseSensitive(minus_obj, "s") != NULL) {
            return false;
        }

        cJSON *minus_attrs = cJSON_GetObjectItemCaseSensitive(minus_obj, "a");
        if (cJSON_IsArray(minus_attrs)) {
            cJSON *key = NULL;
            cJSON_ArrayForEach(key, minus_attrs)
            {
                if (cJSON_IsString(key) && key->valuestring != NULL &&
                    ha_client_media_player_attr_key_is_tracked(key->valuestring)) {
                    return false;
                }
            }
        }
    }

    /* Remaining media_player attribute patches are currently not used by widgets. */
    return true;
}

static bool ha_client_entity_should_use_trigger_subscription(const char *entity_id)
{
    if (entity_id == NULL || entity_id[0] == '\0') {
        return false;
    }
    /* Media player entities can emit high-rate, large state_changed payloads.
       Keep them off WS triggers; they are handled via REST sync/service paths. */
    if (ha_client_entity_is_media_player(entity_id)) {
        return false;
    }
    return true;
}

static bool ha_client_copy_attr_dup(cJSON *dst_obj, const char *dst_key, cJSON *src_obj, const char *src_key)
{
    if (!cJSON_IsObject(dst_obj) || dst_key == NULL || !cJSON_IsObject(src_obj) || src_key == NULL) {
        return false;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(src_obj, src_key);
    if (item == NULL) {
        return false;
    }

    cJSON *dup = cJSON_Duplicate(item, true);
    if (dup == NULL) {
        return false;
    }

    cJSON_AddItemToObject(dst_obj, dst_key, dup);
    return true;
}

static cJSON *ha_client_build_compact_forecast_array(cJSON *src_forecast)
{
    if (!cJSON_IsArray(src_forecast)) {
        return NULL;
    }

    cJSON *dst_forecast = cJSON_CreateArray();
    if (dst_forecast == NULL) {
        return NULL;
    }

    int count = cJSON_GetArraySize(src_forecast);
    for (int i = 0; i < count && i < HA_WEATHER_COMPACT_FORECAST_MAX_ITEMS; i++) {
        cJSON *src_item = cJSON_GetArrayItem(src_forecast, i);
        if (!cJSON_IsObject(src_item)) {
            continue;
        }

        cJSON *dst_item = cJSON_CreateObject();
        if (dst_item == NULL) {
            continue;
        }

        bool copied = false;
        bool has_datetime = ha_client_copy_attr_dup(dst_item, "datetime", src_item, "datetime");
        if (!has_datetime) {
            has_datetime = ha_client_copy_attr_dup(dst_item, "datetime", src_item, "date");
        }
        copied |= has_datetime;
        copied |= ha_client_copy_attr_dup(dst_item, "condition", src_item, "condition");

        if (!ha_client_copy_attr_dup(dst_item, "temperature", src_item, "temperature")) {
            copied |= ha_client_copy_attr_dup(dst_item, "temperature", src_item, "native_temperature");
        } else {
            copied = true;
        }
        if (!ha_client_copy_attr_dup(dst_item, "templow", src_item, "templow")) {
            copied |= ha_client_copy_attr_dup(dst_item, "templow", src_item, "native_templow");
        } else {
            copied = true;
        }

        if (copied) {
            cJSON_AddItemToArray(dst_forecast, dst_item);
        } else {
            cJSON_Delete(dst_item);
        }
    }

    if (cJSON_GetArraySize(dst_forecast) == 0) {
        cJSON_Delete(dst_forecast);
        return NULL;
    }

    return dst_forecast;
}

static void ha_client_compact_weather_forecast(cJSON *dst_attrs, cJSON *src_attrs)
{
    if (!cJSON_IsObject(dst_attrs) || !cJSON_IsObject(src_attrs)) {
        return;
    }

    cJSON *src_forecast = cJSON_GetObjectItemCaseSensitive(src_attrs, "forecast");
    if (!cJSON_IsArray(src_forecast)) {
        src_forecast = cJSON_GetObjectItemCaseSensitive(src_attrs, "forecast_daily");
    }
    if (!cJSON_IsArray(src_forecast)) {
        return;
    }

    cJSON *dst_forecast = ha_client_build_compact_forecast_array(src_forecast);
    if (dst_forecast != NULL) {
        cJSON_AddItemToObject(dst_attrs, "forecast", dst_forecast);
    }
}

static cJSON *ha_client_find_compact_forecast_in_entity_obj(cJSON *entity_obj)
{
    if (!cJSON_IsObject(entity_obj)) {
        return NULL;
    }

    cJSON *forecast = cJSON_GetObjectItemCaseSensitive(entity_obj, "forecast");
    cJSON *compact = ha_client_build_compact_forecast_array(forecast);
    if (compact != NULL) {
        return compact;
    }

    cJSON *forecast_daily = cJSON_GetObjectItemCaseSensitive(entity_obj, "forecast_daily");
    return ha_client_build_compact_forecast_array(forecast_daily);
}

static cJSON *ha_client_find_compact_forecast_in_container(cJSON *container, const char *entity_id)
{
    if (!cJSON_IsObject(container) || entity_id == NULL || entity_id[0] == '\0') {
        return NULL;
    }

    cJSON *compact =
        ha_client_find_compact_forecast_in_entity_obj(cJSON_GetObjectItemCaseSensitive(container, entity_id));
    if (compact != NULL) {
        return compact;
    }

    cJSON *service_response = cJSON_GetObjectItemCaseSensitive(container, "service_response");
    if (cJSON_IsObject(service_response)) {
        compact = ha_client_find_compact_forecast_in_entity_obj(
            cJSON_GetObjectItemCaseSensitive(service_response, entity_id));
        if (compact != NULL) {
            return compact;
        }
    }

    cJSON *response = cJSON_GetObjectItemCaseSensitive(container, "response");
    if (cJSON_IsObject(response)) {
        compact = ha_client_find_compact_forecast_in_container(response, entity_id);
        if (compact != NULL) {
            return compact;
        }
    }

    return NULL;
}

static cJSON *ha_client_find_compact_forecast_recursive(cJSON *node, int depth)
{
    if (node == NULL || depth > 10) {
        return NULL;
    }

    if (cJSON_IsObject(node)) {
        cJSON *compact = ha_client_find_compact_forecast_in_entity_obj(node);
        if (compact != NULL) {
            return compact;
        }

        for (cJSON *child = node->child; child != NULL; child = child->next) {
            compact = ha_client_find_compact_forecast_recursive(child, depth + 1);
            if (compact != NULL) {
                return compact;
            }
        }
        return NULL;
    }

    if (cJSON_IsArray(node)) {
        int n = cJSON_GetArraySize(node);
        for (int i = 0; i < n; i++) {
            cJSON *compact = ha_client_find_compact_forecast_recursive(cJSON_GetArrayItem(node, i), depth + 1);
            if (compact != NULL) {
                return compact;
            }
        }
    }

    return NULL;
}

static cJSON *ha_client_find_compact_weather_forecast(cJSON *node, const char *entity_id)
{
    cJSON *compact = ha_client_find_compact_forecast_in_container(node, entity_id);
    if (compact != NULL) {
        return compact;
    }

    return ha_client_find_compact_forecast_recursive(node, 0);
}

static esp_err_t ha_client_fetch_weather_daily_forecast_http(
    const char *base_url, const char *host_header, const char *entity_id, cJSON **out_forecast)
{
    if (base_url == NULL || entity_id == NULL || out_forecast == NULL || entity_id[0] == '\0' || s_client.http_client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_forecast = NULL;

    char url[384] = {0};
    int url_len = snprintf(url, sizeof(url), "%s/api/services/weather/get_forecasts?return_response", base_url);
    if (url_len <= 0 || (size_t)url_len >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char body[256] = {0};
    int body_len = snprintf(body, sizeof(body), "{\"type\":\"daily\",\"entity_id\":\"%s\"}", entity_id);
    if (body_len <= 0 || (size_t)body_len >= sizeof(body)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char auth_header[640] = {0};
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_client.access_token);
    esp_http_client_set_url(s_client.http_client, url);
    esp_http_client_set_method(s_client.http_client, HTTP_METHOD_POST);
    esp_http_client_set_header(s_client.http_client, "Authorization", auth_header);
    esp_http_client_set_header(s_client.http_client, "Accept", "application/json");
    esp_http_client_set_header(s_client.http_client, "Content-Type", "application/json");
    if (host_header != NULL && host_header[0] != '\0') {
        esp_http_client_set_header(s_client.http_client, "Host", host_header);
    }

    esp_err_t err = ha_client_http_open_budgeted(s_client.http_client, body_len, "forecast");
    if (err != ESP_OK) {
        return err;
    }

    int written = esp_http_client_write(s_client.http_client, body, body_len);
    if (written < body_len) {
        esp_http_client_close(s_client.http_client);
        return ESP_FAIL;
    }

    int64_t content_length = esp_http_client_fetch_headers(s_client.http_client);
    size_t payload_cap = 12288;
    if (content_length > 0 && content_length < 65536) {
        payload_cap = (size_t)content_length + 1U;
    }

    char *payload = calloc(payload_cap, sizeof(char));
    if (payload == NULL) {
        esp_http_client_close(s_client.http_client);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    while (total_read < (int)payload_cap - 1) {
        int read =
            esp_http_client_read(s_client.http_client, payload + total_read, (int)payload_cap - 1 - total_read);
        if (read < 0) {
            err = ESP_FAIL;
            break;
        }
        if (read == 0) {
            break;
        }
        total_read += read;
    }
    payload[total_read] = '\0';

    int status = esp_http_client_get_status_code(s_client.http_client);
    esp_http_client_close(s_client.http_client);

    if (err != ESP_OK) {
        free(payload);
        return err;
    }
    if (status != 200 && status != 201) {
        free(payload);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *compact_forecast = ha_client_find_compact_weather_forecast(root, entity_id);
    cJSON_Delete(root);
    if (compact_forecast == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    *out_forecast = compact_forecast;
    return ESP_OK;
}

static bool ha_client_serialize_weather_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    bool has_temperature = ha_client_copy_attr_dup(compact, "temperature", src_attrs, "temperature");
    if (!has_temperature) {
        has_temperature = ha_client_copy_attr_dup(compact, "temperature", src_attrs, "native_temperature");
    }
    any |= has_temperature;
    any |= ha_client_copy_attr_dup(compact, "current_temperature", src_attrs, "current_temperature");
    any |= ha_client_copy_attr_dup(compact, "native_temperature", src_attrs, "native_temperature");
    bool has_temperature_unit = ha_client_copy_attr_dup(compact, "temperature_unit", src_attrs, "temperature_unit");
    if (!has_temperature_unit) {
        has_temperature_unit = ha_client_copy_attr_dup(compact, "temperature_unit", src_attrs, "native_temperature_unit");
    }
    any |= has_temperature_unit;
    any |= ha_client_copy_attr_dup(compact, "native_temperature_unit", src_attrs, "native_temperature_unit");
    any |= ha_client_copy_attr_dup(compact, "humidity", src_attrs, "humidity");

    ha_client_compact_weather_forecast(compact, src_attrs);
    if (cJSON_GetObjectItemCaseSensitive(compact, "forecast") != NULL) {
        any = true;
    }

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = (len < out_json_size);
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    }
    cJSON_free(compact_json);
    return fits;
}

static bool ha_client_serialize_climate_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    bool has_target_temp = ha_client_copy_attr_dup(compact, "temperature", src_attrs, "temperature");
    if (!has_target_temp) {
        has_target_temp = ha_client_copy_attr_dup(compact, "temperature", src_attrs, "target_temperature");
    }
    if (!has_target_temp) {
        has_target_temp = ha_client_copy_attr_dup(compact, "temperature", src_attrs, "target_temp");
    }
    any |= has_target_temp;

    any |= ha_client_copy_attr_dup(compact, "current_temperature", src_attrs, "current_temperature");
    any |= ha_client_copy_attr_dup(compact, "temperature_unit", src_attrs, "temperature_unit");
    any |= ha_client_copy_attr_dup(compact, "hvac_action", src_attrs, "hvac_action");
    any |= ha_client_copy_attr_dup(compact, "hvac_mode", src_attrs, "hvac_mode");
    any |= ha_client_copy_attr_dup(compact, "preset_mode", src_attrs, "preset_mode");
    any |= ha_client_copy_attr_dup(compact, "min_temp", src_attrs, "min_temp");
    any |= ha_client_copy_attr_dup(compact, "max_temp", src_attrs, "max_temp");
    any |= ha_client_copy_attr_dup(compact, "target_temp_low", src_attrs, "target_temp_low");
    any |= ha_client_copy_attr_dup(compact, "target_temp_high", src_attrs, "target_temp_high");
    any |= ha_client_copy_attr_dup(compact, "humidity", src_attrs, "humidity");

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = (len < out_json_size);
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    }
    cJSON_free(compact_json);
    return fits;
}

static bool ha_client_serialize_media_player_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    cJSON *volume_level = cJSON_GetObjectItemCaseSensitive(src_attrs, "volume_level");
    if (cJSON_IsNumber(volume_level)) {
        double volume = volume_level->valuedouble;
        if (volume < 0.0) {
            volume = 0.0;
        } else if (volume > 1.0) {
            volume = 1.0;
        }
        cJSON_AddNumberToObject(compact, "volume_level", volume);
        any = true;
    }

    cJSON *is_volume_muted = cJSON_GetObjectItemCaseSensitive(src_attrs, "is_volume_muted");
    if (cJSON_IsBool(is_volume_muted)) {
        cJSON_AddBoolToObject(compact, "is_volume_muted", cJSON_IsTrue(is_volume_muted));
        any = true;
    }

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = (len < out_json_size);
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    }
    cJSON_free(compact_json);
    return fits;
}

static bool ha_client_serialize_light_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    any |= ha_client_copy_attr_dup(compact, "supported_color_modes", src_attrs, "supported_color_modes");
    any |= ha_client_copy_attr_dup(compact, "color_mode", src_attrs, "color_mode");
    any |= ha_client_copy_attr_dup(compact, "brightness", src_attrs, "brightness");
    any |= ha_client_copy_attr_dup(compact, "brightness_pct", src_attrs, "brightness_pct");
    any |= ha_client_copy_attr_dup(compact, "rgb_color", src_attrs, "rgb_color");
    any |= ha_client_copy_attr_dup(compact, "hs_color", src_attrs, "hs_color");
    any |= ha_client_copy_attr_dup(compact, "xy_color", src_attrs, "xy_color");
    any |= ha_client_copy_attr_dup(compact, "color_temp", src_attrs, "color_temp");
    any |= ha_client_copy_attr_dup(compact, "color_temp_kelvin", src_attrs, "color_temp_kelvin");
    any |= ha_client_copy_attr_dup(compact, "min_color_temp_kelvin", src_attrs, "min_color_temp_kelvin");
    any |= ha_client_copy_attr_dup(compact, "max_color_temp_kelvin", src_attrs, "max_color_temp_kelvin");
    any |= ha_client_copy_attr_dup(compact, "min_mireds", src_attrs, "min_mireds");
    any |= ha_client_copy_attr_dup(compact, "max_mireds", src_attrs, "max_mireds");
    any |= ha_client_copy_attr_dup(compact, "supported_features", src_attrs, "supported_features");

    cJSON *effect_list = cJSON_GetObjectItemCaseSensitive(src_attrs, "effect_list");
    if (cJSON_IsArray(effect_list)) {
        cJSON *effect_marker = cJSON_CreateArray();
        if (effect_marker != NULL) {
            cJSON_AddItemToObject(compact, "effect_list", effect_marker);
            any = true;
        }
    }

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = (len < out_json_size);
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    }
    cJSON_free(compact_json);
    return fits;
}

static cJSON *ha_client_extract_compact_forecast_from_attrs_json(const char *attrs_json)
{
    if (attrs_json == NULL || attrs_json[0] == '\0') {
        return NULL;
    }

    cJSON *attrs = cJSON_Parse(attrs_json);
    if (!cJSON_IsObject(attrs)) {
        cJSON_Delete(attrs);
        return NULL;
    }

    cJSON *forecast = cJSON_GetObjectItemCaseSensitive(attrs, "forecast");
    if (!cJSON_IsArray(forecast)) {
        forecast = cJSON_GetObjectItemCaseSensitive(attrs, "forecast_daily");
    }

    cJSON *compact = ha_client_build_compact_forecast_array(forecast);
    cJSON_Delete(attrs);
    return compact;
}

static bool ha_client_weather_attrs_has_forecast_json(const char *attrs_json)
{
    cJSON *compact = ha_client_extract_compact_forecast_from_attrs_json(attrs_json);
    if (compact == NULL) {
        return false;
    }
    cJSON_Delete(compact);
    return true;
}

static bool ha_client_append_compact_forecast_to_attrs_json(char *attrs_json, size_t attrs_json_size, cJSON *forecast)
{
    if (attrs_json == NULL || attrs_json_size == 0 || !cJSON_IsArray(forecast)) {
        cJSON_Delete(forecast);
        return false;
    }

    cJSON *attrs = cJSON_Parse(attrs_json);
    if (!cJSON_IsObject(attrs)) {
        cJSON_Delete(attrs);
        cJSON_Delete(forecast);
        return false;
    }

    cJSON_DeleteItemFromObjectCaseSensitive(attrs, "forecast");
    cJSON_AddItemToObject(attrs, "forecast", forecast);

    char *merged_json = cJSON_PrintUnformatted(attrs);
    cJSON_Delete(attrs);
    if (merged_json == NULL) {
        return false;
    }

    size_t len = strlen(merged_json);
    bool fits = (len < attrs_json_size);
    if (fits) {
        memcpy(attrs_json, merged_json, len + 1U);
    }
    cJSON_free(merged_json);
    return fits;
}

static bool ha_client_priority_sync_queue_contains_locked(const char *entity_id)
{
    if (entity_id == NULL || entity_id[0] == '\0') {
        return false;
    }
    for (uint8_t i = 0; i < s_client.priority_sync_count; i++) {
        uint8_t idx = (uint8_t)((s_client.priority_sync_head + i) % (uint8_t)(sizeof(s_client.priority_sync_entities) /
                                                                                sizeof(s_client.priority_sync_entities[0])));
        if (strncmp(s_client.priority_sync_entities[idx], entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            return true;
        }
    }
    return false;
}

static void ha_client_priority_sync_queue_push_locked(const char *entity_id)
{
    if (entity_id == NULL || entity_id[0] == '\0') {
        return;
    }
    if (ha_client_priority_sync_queue_contains_locked(entity_id)) {
        return;
    }

    const uint8_t queue_len =
        (uint8_t)(sizeof(s_client.priority_sync_entities) / sizeof(s_client.priority_sync_entities[0]));
    if (s_client.priority_sync_count >= queue_len) {
        /* Keep freshest work first under sustained churn. */
        s_client.priority_sync_head = (uint8_t)((s_client.priority_sync_head + 1) % queue_len);
        s_client.priority_sync_count--;
    }

    safe_copy_cstr(s_client.priority_sync_entities[s_client.priority_sync_tail], APP_MAX_ENTITY_ID_LEN, entity_id);
    s_client.priority_sync_tail = (uint8_t)((s_client.priority_sync_tail + 1) % queue_len);
    s_client.priority_sync_count++;
}

static bool ha_client_priority_sync_queue_pop_locked(char *out_entity_id, size_t out_entity_id_len)
{
    if (out_entity_id == NULL || out_entity_id_len == 0 || s_client.priority_sync_count == 0) {
        return false;
    }
    const uint8_t queue_len =
        (uint8_t)(sizeof(s_client.priority_sync_entities) / sizeof(s_client.priority_sync_entities[0]));

    safe_copy_cstr(out_entity_id, out_entity_id_len, s_client.priority_sync_entities[s_client.priority_sync_head]);
    s_client.priority_sync_entities[s_client.priority_sync_head][0] = '\0';
    s_client.priority_sync_head = (uint8_t)((s_client.priority_sync_head + 1) % queue_len);
    s_client.priority_sync_count--;
    if (s_client.priority_sync_count == 0) {
        s_client.priority_sync_tail = s_client.priority_sync_head;
    }
    return true;
}

static int ha_client_service_trace_find_by_id_locked(uint32_t id)
{
    const size_t count = sizeof(s_client.service_traces) / sizeof(s_client.service_traces[0]);
    for (size_t i = 0; i < count; i++) {
        if (s_client.service_traces[i].active && s_client.service_traces[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static int ha_client_service_trace_alloc_locked(void)
{
    const size_t count = sizeof(s_client.service_traces) / sizeof(s_client.service_traces[0]);
    int oldest_idx = 0;
    int64_t oldest_ts = INT64_MAX;
    for (size_t i = 0; i < count; i++) {
        if (!s_client.service_traces[i].active) {
            return (int)i;
        }
        if (s_client.service_traces[i].queued_unix_ms < oldest_ts) {
            oldest_ts = s_client.service_traces[i].queued_unix_ms;
            oldest_idx = (int)i;
        }
    }
    return oldest_idx;
}

static void ha_client_service_trace_expire_locked(int64_t now_ms)
{
    const size_t count = sizeof(s_client.service_traces) / sizeof(s_client.service_traces[0]);
    for (size_t i = 0; i < count; i++) {
        ha_service_trace_t *trace = &s_client.service_traces[i];
        if (!trace->active || trace->queued_unix_ms <= 0) {
            continue;
        }
        if ((now_ms - trace->queued_unix_ms) > HA_SVC_TRACE_MAX_AGE_MS) {
            trace->active = false;
        }
    }
}

static const char *ha_client_expected_state_from_service(
    const char *service, const char *entity_id, const char *current_state)
{
    if (service == NULL || service[0] == '\0') {
        return NULL;
    }

    if (strcmp(service, "turn_on") == 0) {
        return "on";
    }
    if (strcmp(service, "turn_off") == 0) {
        return "off";
    }
    if (strcmp(service, "open_cover") == 0) {
        return "open";
    }
    if (strcmp(service, "close_cover") == 0) {
        return "closed";
    }
    if (strcmp(service, "toggle") == 0 && entity_id != NULL && entity_id[0] != '\0') {
        if (current_state != NULL && current_state[0] != '\0') {
            if (strcmp(current_state, "on") == 0) {
                return "off";
            }
            if (strcmp(current_state, "off") == 0) {
                return "on";
            }
        }
    }
    return NULL;
}

static void ha_client_trace_service_queued(uint32_t id, const char *domain, const char *service, const char *entity_id,
    const char *expected_state)
{
    if (s_client.mutex == NULL) {
        return;
    }

    int64_t now_ms = ha_client_now_ms();
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    ha_client_service_trace_expire_locked(now_ms);
    int idx = ha_client_service_trace_alloc_locked();
    ha_service_trace_t *trace = &s_client.service_traces[idx];
    memset(trace, 0, sizeof(*trace));
    trace->active = true;
    trace->id = id;
    trace->queued_unix_ms = now_ms;
    safe_copy_cstr(trace->entity_id, sizeof(trace->entity_id), entity_id);
    safe_copy_cstr(trace->domain, sizeof(trace->domain), domain);
    safe_copy_cstr(trace->service, sizeof(trace->service), service);
    safe_copy_cstr(trace->expected_state, sizeof(trace->expected_state), expected_state);
    xSemaphoreGive(s_client.mutex);

    ESP_LOGD(TAG_HA_CLIENT, "svc[%u] queued %s.%s entity=%s", (unsigned)id,
        (domain != NULL && domain[0] != '\0') ? domain : "?",
        (service != NULL && service[0] != '\0') ? service : "?",
        (entity_id != NULL && entity_id[0] != '\0') ? entity_id : "?");
}

static void ha_client_trace_service_sent(uint32_t id, esp_err_t err)
{
    if (s_client.mutex == NULL) {
        return;
    }

    int64_t now_ms = ha_client_now_ms();
    int64_t queued_ms = 0;
    char entity_id[APP_MAX_ENTITY_ID_LEN] = {0};
    bool found = false;

    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    int idx = ha_client_service_trace_find_by_id_locked(id);
    if (idx >= 0) {
        ha_service_trace_t *trace = &s_client.service_traces[idx];
        found = true;
        queued_ms = trace->queued_unix_ms;
        safe_copy_cstr(entity_id, sizeof(entity_id), trace->entity_id);
        if (err == ESP_OK) {
            trace->sent_unix_ms = now_ms;
        } else {
            trace->active = false;
        }
    }
    xSemaphoreGive(s_client.mutex);

    if (!found) {
        return;
    }

    int64_t queue_to_send_ms = (queued_ms > 0 && now_ms >= queued_ms) ? (now_ms - queued_ms) : 0;
    if (err == ESP_OK) {
        if (queue_to_send_ms >= HA_SVC_LATENCY_INFO_MS) {
            ESP_LOGI(TAG_HA_CLIENT, "svc[%u] sent entity=%s queue->send=%" PRId64 " ms", (unsigned)id,
                (entity_id[0] != '\0') ? entity_id : "?", queue_to_send_ms);
        } else {
            ESP_LOGD(TAG_HA_CLIENT, "svc[%u] sent entity=%s queue->send=%" PRId64 " ms", (unsigned)id,
                (entity_id[0] != '\0') ? entity_id : "?", queue_to_send_ms);
        }
    } else {
        ESP_LOGW(TAG_HA_CLIENT, "svc[%u] send failed (%s) entity=%s queue->fail=%" PRId64 " ms", (unsigned)id,
            esp_err_to_name(err), (entity_id[0] != '\0') ? entity_id : "?", queue_to_send_ms);
    }
}

static void ha_client_trace_service_result(uint32_t id, bool success, const char *error_text)
{
    if (s_client.mutex == NULL) {
        return;
    }

    int64_t now_ms = ha_client_now_ms();
    int64_t queued_ms = 0;
    int64_t sent_ms = 0;
    char entity_id[APP_MAX_ENTITY_ID_LEN] = {0};
    char domain[24] = {0};
    char service[32] = {0};
    bool found = false;

    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    int idx = ha_client_service_trace_find_by_id_locked(id);
    if (idx >= 0) {
        ha_service_trace_t *trace = &s_client.service_traces[idx];
        found = true;
        trace->result_seen = true;
        trace->result_success = success;
        trace->result_unix_ms = now_ms;
        queued_ms = trace->queued_unix_ms;
        sent_ms = trace->sent_unix_ms;
        safe_copy_cstr(entity_id, sizeof(entity_id), trace->entity_id);
        safe_copy_cstr(domain, sizeof(domain), trace->domain);
        safe_copy_cstr(service, sizeof(service), trace->service);
        if (!success) {
            trace->active = false;
        }
    }
    xSemaphoreGive(s_client.mutex);

    if (!found) {
        return;
    }

    int64_t queue_to_result_ms = (queued_ms > 0 && now_ms >= queued_ms) ? (now_ms - queued_ms) : 0;
    int64_t send_to_result_ms = (sent_ms > 0 && now_ms >= sent_ms) ? (now_ms - sent_ms) : -1;
    if (success) {
        if (queue_to_result_ms >= HA_SVC_LATENCY_INFO_MS || send_to_result_ms >= HA_SVC_LATENCY_INFO_MS) {
            ESP_LOGI(TAG_HA_CLIENT,
                "svc[%u] result ok %s.%s entity=%s queue->result=%" PRId64 " ms send->result=%" PRId64 " ms",
                (unsigned)id,
                (domain[0] != '\0') ? domain : "?",
                (service[0] != '\0') ? service : "?",
                (entity_id[0] != '\0') ? entity_id : "?",
                queue_to_result_ms, send_to_result_ms);
        } else {
            ESP_LOGD(TAG_HA_CLIENT,
                "svc[%u] result ok %s.%s entity=%s queue->result=%" PRId64 " ms send->result=%" PRId64 " ms",
                (unsigned)id,
                (domain[0] != '\0') ? domain : "?",
                (service[0] != '\0') ? service : "?",
                (entity_id[0] != '\0') ? entity_id : "?",
                queue_to_result_ms, send_to_result_ms);
        }
    } else {
        ESP_LOGW(TAG_HA_CLIENT,
            "svc[%u] result failed %s.%s entity=%s queue->result=%" PRId64 " ms send->result=%" PRId64 " ms error=%s",
            (unsigned)id,
            (domain[0] != '\0') ? domain : "?",
            (service[0] != '\0') ? service : "?",
            (entity_id[0] != '\0') ? entity_id : "?",
            queue_to_result_ms, send_to_result_ms,
            (error_text != NULL && error_text[0] != '\0') ? error_text : "-");
    }
}

static void ha_client_trace_service_state_changed(const char *entity_id, const char *new_state)
{
    if (entity_id == NULL || entity_id[0] == '\0' || s_client.mutex == NULL) {
        return;
    }

    int64_t now_ms = ha_client_now_ms();
    uint32_t id = 0;
    int64_t queued_ms = 0;
    int64_t sent_ms = 0;
    int64_t result_ms = 0;
    bool result_seen = false;
    bool result_success = false;
    char domain[24] = {0};
    char service[32] = {0};
    bool has_new_state = (new_state != NULL && new_state[0] != '\0');
    int best_score = INT_MAX;

    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    ha_client_service_trace_expire_locked(now_ms);
    const size_t count = sizeof(s_client.service_traces) / sizeof(s_client.service_traces[0]);
    int best_idx = -1;
    int64_t best_ts = INT64_MIN;
    for (size_t i = 0; i < count; i++) {
        ha_service_trace_t *trace = &s_client.service_traces[i];
        if (!trace->active) {
            continue;
        }
        if (strncmp(trace->entity_id, entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
            continue;
        }

        int score = 1;
        if (has_new_state) {
            if (trace->expected_state[0] == '\0') {
                score = 1;
            } else if (strcmp(trace->expected_state, new_state) == 0) {
                score = 0;
            } else {
                score = 2;
            }
        }

        int64_t candidate_ts = (trace->sent_unix_ms > 0) ? trace->sent_unix_ms : trace->queued_unix_ms;
        if (score < best_score || (score == best_score && candidate_ts >= best_ts)) {
            best_score = score;
            best_ts = candidate_ts;
            best_idx = (int)i;
        }
    }

    if (best_idx >= 0 && !(has_new_state && best_score >= 2)) {
        ha_service_trace_t *trace = &s_client.service_traces[best_idx];
        id = trace->id;
        queued_ms = trace->queued_unix_ms;
        sent_ms = trace->sent_unix_ms;
        result_ms = trace->result_unix_ms;
        result_seen = trace->result_seen;
        result_success = trace->result_success;
        safe_copy_cstr(domain, sizeof(domain), trace->domain);
        safe_copy_cstr(service, sizeof(service), trace->service);
        trace->active = false;
    }
    xSemaphoreGive(s_client.mutex);

    if (best_idx < 0) {
#if APP_HA_ROUTE_TRACE_LOG
        ESP_LOGI(TAG_HA_CLIENT, "route ws->panel entity=%s state=%s source=external", entity_id,
            (new_state != NULL && new_state[0] != '\0') ? new_state : "?");
#endif
        return;
    }

#if APP_HA_ROUTE_TRACE_LOG
    ESP_LOGI(TAG_HA_CLIENT, "route ws->panel entity=%s state=%s source=matched_service svc=%u", entity_id,
        (new_state != NULL && new_state[0] != '\0') ? new_state : "?", (unsigned)id);
#endif

    int64_t queue_to_state_ms = (queued_ms > 0 && now_ms >= queued_ms) ? (now_ms - queued_ms) : 0;
    int64_t send_to_state_ms = (sent_ms > 0 && now_ms >= sent_ms) ? (now_ms - sent_ms) : -1;
    int64_t result_to_state_ms = (result_ms > 0 && now_ms >= result_ms) ? (now_ms - result_ms) : -1;
    if (queue_to_state_ms >= HA_SVC_LATENCY_WARN_MS || send_to_state_ms >= HA_SVC_LATENCY_WARN_MS) {
        ESP_LOGW(TAG_HA_CLIENT,
            "svc[%u] slow state_changed %s.%s entity=%s queue->state=%" PRId64 " ms send->state=%" PRId64
            " ms result_seen=%d result_ok=%d result->state=%" PRId64 " ms",
            (unsigned)id,
            (domain[0] != '\0') ? domain : "?",
            (service[0] != '\0') ? service : "?",
            entity_id,
            queue_to_state_ms, send_to_state_ms,
            result_seen ? 1 : 0,
            result_success ? 1 : 0,
            result_to_state_ms);
    } else if (queue_to_state_ms >= HA_SVC_LATENCY_INFO_MS || send_to_state_ms >= HA_SVC_LATENCY_INFO_MS) {
        ESP_LOGI(TAG_HA_CLIENT,
            "svc[%u] state_changed %s.%s entity=%s queue->state=%" PRId64 " ms send->state=%" PRId64
            " ms result_seen=%d result_ok=%d result->state=%" PRId64 " ms",
            (unsigned)id,
            (domain[0] != '\0') ? domain : "?",
            (service[0] != '\0') ? service : "?",
            entity_id,
            queue_to_state_ms, send_to_state_ms,
            result_seen ? 1 : 0,
            result_success ? 1 : 0,
            result_to_state_ms);
    } else {
        ESP_LOGD(TAG_HA_CLIENT,
            "svc[%u] state_changed %s.%s entity=%s queue->state=%" PRId64 " ms send->state=%" PRId64
            " ms result_seen=%d result_ok=%d result->state=%" PRId64 " ms",
            (unsigned)id,
            (domain[0] != '\0') ? domain : "?",
            (service[0] != '\0') ? service : "?",
            entity_id,
            queue_to_state_ms, send_to_state_ms,
            result_seen ? 1 : 0,
            result_success ? 1 : 0,
            result_to_state_ms);
    }
}

static bool ha_client_parse_ws_endpoint(const char *ws_url, bool *secure, char *host, size_t host_size, int *port)
{
    if (ws_url == NULL || secure == NULL || host == NULL || host_size == 0 || port == NULL) {
        return false;
    }

    const char *p = NULL;
    if (strncmp(ws_url, "wss://", 6) == 0) {
        *secure = true;
        *port = 443;
        p = ws_url + 6;
    } else if (strncmp(ws_url, "ws://", 5) == 0) {
        *secure = false;
        *port = 80;
        p = ws_url + 5;
    } else {
        return false;
    }

    const char *path = strchr(p, '/');
    size_t authority_len = (path != NULL) ? (size_t)(path - p) : strlen(p);
    if (authority_len == 0) {
        return false;
    }

    const char *last_colon = NULL;
    for (size_t i = 0; i < authority_len; i++) {
        if (p[i] == ':') {
            last_colon = &p[i];
        }
    }

    size_t host_len = authority_len;
    if (last_colon != NULL) {
        host_len = (size_t)(last_colon - p);
        const char *port_str = last_colon + 1;
        if (port_str < (p + authority_len)) {
            int parsed_port = atoi(port_str);
            if (parsed_port > 0 && parsed_port <= 65535) {
                *port = parsed_port;
            }
        }
    }

    if (host_len == 0 || host_len >= host_size) {
        return false;
    }

    memcpy(host, p, host_len);
    host[host_len] = '\0';
    return true;
}

static bool ha_client_resolve_ipv4_with_cache(const char *host, char *ip_out, size_t ip_out_size)
{
    if (host == NULL || host[0] == '\0' || ip_out == NULL || ip_out_size == 0) {
        return false;
    }

    struct in_addr addr4 = {0};
    if (inet_pton(AF_INET, host, &addr4) == 1) {
        safe_copy_cstr(ip_out, ip_out_size, host);
        safe_copy_cstr(s_client.http_resolved_host, sizeof(s_client.http_resolved_host), host);
        safe_copy_cstr(s_client.http_resolved_ip, sizeof(s_client.http_resolved_ip), host);
        return true;
    }

    if (s_client.http_resolved_host[0] != '\0' && s_client.http_resolved_ip[0] != '\0' &&
        strncmp(s_client.http_resolved_host, host, sizeof(s_client.http_resolved_host)) == 0) {
        safe_copy_cstr(ip_out, ip_out_size, s_client.http_resolved_ip);
        return true;
    }

    char ws_host[128] = {0};
    char ws_ip[64] = {0};
    if (ha_ws_get_cached_resolved_ipv4(ws_host, sizeof(ws_host), ws_ip, sizeof(ws_ip)) &&
        ws_host[0] != '\0' && ws_ip[0] != '\0' && strncmp(ws_host, host, sizeof(ws_host)) == 0) {
        safe_copy_cstr(ip_out, ip_out_size, ws_ip);
        safe_copy_cstr(s_client.http_resolved_host, sizeof(s_client.http_resolved_host), host);
        safe_copy_cstr(s_client.http_resolved_ip, sizeof(s_client.http_resolved_ip), ws_ip);
        return true;
    }

    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, NULL, &hints, &result);
    if (rc == 0 && result != NULL && result->ai_family == AF_INET &&
        result->ai_addrlen >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in *sa = (struct sockaddr_in *)result->ai_addr;
        char ip_resolved[64] = {0};
        if (inet_ntop(AF_INET, &sa->sin_addr, ip_resolved, sizeof(ip_resolved)) != NULL) {
            safe_copy_cstr(ip_out, ip_out_size, ip_resolved);
            safe_copy_cstr(s_client.http_resolved_host, sizeof(s_client.http_resolved_host), host);
            safe_copy_cstr(s_client.http_resolved_ip, sizeof(s_client.http_resolved_ip), ip_resolved);
            freeaddrinfo(result);
            return true;
        }
    }
    if (result != NULL) {
        freeaddrinfo(result);
    }

    return false;
}

static bool ha_client_build_http_request_context(const char *ws_url, char *out_base_url, size_t out_base_url_size,
    char *out_host_header, size_t out_host_header_size, char *out_cert_common_name, size_t out_cert_common_name_size)
{
    if (ws_url == NULL || out_base_url == NULL || out_base_url_size == 0 || out_host_header == NULL ||
        out_host_header_size == 0 || out_cert_common_name == NULL || out_cert_common_name_size == 0) {
        return false;
    }

    bool secure = false;
    int port = 0;
    char host[128] = {0};
    if (!ha_client_parse_ws_endpoint(ws_url, &secure, host, sizeof(host), &port)) {
        return false;
    }

    char ip[64] = {0};
    bool has_resolved_ip = ha_client_resolve_ipv4_with_cache(host, ip, sizeof(ip));
    const char *connect_host = has_resolved_ip ? ip : host;

    const char *scheme = secure ? "https" : "http";
    int base_written = snprintf(out_base_url, out_base_url_size, "%s://%s:%d", scheme, connect_host, port);
    if (base_written <= 0 || (size_t)base_written >= out_base_url_size) {
        return false;
    }

    out_host_header[0] = '\0';
    out_cert_common_name[0] = '\0';

    if (has_resolved_ip) {
        int host_written = snprintf(out_host_header, out_host_header_size, "%s:%d", host, port);
        if (host_written <= 0 || (size_t)host_written >= out_host_header_size) {
            return false;
        }
        if (secure) {
            safe_copy_cstr(out_cert_common_name, out_cert_common_name_size, host);
        }
    }

    return true;
}

static void ha_client_reset_http_client(void)
{
    if (s_client.http_client != NULL) {
        esp_http_client_cleanup(s_client.http_client);
        s_client.http_client = NULL;
    }
    s_client.http_base_url[0] = '\0';
    s_client.http_cert_common_name[0] = '\0';
}

static esp_err_t ha_client_ensure_http_client(const char *base_url, const char *cert_common_name)
{
    if (base_url == NULL || base_url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    bool same_cn = false;
    if (cert_common_name == NULL || cert_common_name[0] == '\0') {
        same_cn = (s_client.http_cert_common_name[0] == '\0');
    } else {
        same_cn = (strncmp(s_client.http_cert_common_name, cert_common_name, sizeof(s_client.http_cert_common_name)) == 0);
    }

    if (s_client.http_client != NULL &&
        strncmp(s_client.http_base_url, base_url, sizeof(s_client.http_base_url)) == 0 && same_cn) {
        return ESP_OK;
    }

    ha_client_reset_http_client();

    esp_http_client_config_t http_cfg = {
        .url = base_url,
        /* Background REST sync must never stall WS handling for many seconds. */
        .timeout_ms = 2500,
        .keep_alive_enable = true,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    if (strncmp(base_url, "https://", 8) == 0) {
        http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }
#endif
    if (cert_common_name != NULL && cert_common_name[0] != '\0') {
        http_cfg.common_name = cert_common_name;
    }

    s_client.http_client = esp_http_client_init(&http_cfg);
    if (s_client.http_client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    safe_copy_cstr(s_client.http_base_url, sizeof(s_client.http_base_url), base_url);
    safe_copy_cstr(s_client.http_cert_common_name, sizeof(s_client.http_cert_common_name), cert_common_name);
    return ESP_OK;
}

static esp_err_t ha_client_light_discovery_template_step(uint16_t offset, uint16_t page_size)
{
    if (page_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char domain[APP_HA_DISCOVERY_DOMAIN_MAX_LEN] = {0};
    char search[APP_HA_DISCOVERY_SEARCH_MAX_LEN] = {0};
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    safe_copy_cstr(domain, sizeof(domain), ha_client_discovery_domain_or_default(s_client.light_discovery_domain));
    safe_copy_cstr(s_client.light_discovery_domain, sizeof(s_client.light_discovery_domain), domain);
    safe_copy_cstr(search, sizeof(search), s_client.light_discovery_search);
    s_client.light_discovery_requested = false;
    s_client.light_discovery_inflight = true;
    s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_TEMPLATE;
    xSemaphoreGive(s_client.mutex);

    char base_url[256] = {0};
    char host_header[192] = {0};
    char cert_common_name[128] = {0};
    esp_err_t err = ESP_OK;
    if (!ha_client_build_http_request_context(s_client.ws_url, base_url, sizeof(base_url), host_header,
            sizeof(host_header), cert_common_name, sizeof(cert_common_name))) {
        err = ESP_ERR_HTTP_CONNECT;
        goto fail;
    }

    char url[384] = {0};
    int url_len = snprintf(url, sizeof(url), "%s/api/template", base_url);
    if (url_len <= 0 || (size_t)url_len >= sizeof(url)) {
        err = ESP_ERR_INVALID_SIZE;
        goto fail;
    }

    ESP_LOGI(TAG_HA_CLIENT,
        "Requesting HA entity discovery page via template: domain=%s search=\"%s\" offset=%u limit=%u",
        domain, search, (unsigned)offset, (unsigned)page_size);

    cJSON *search_json = cJSON_CreateString(search);
    if (search_json == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    char *search_literal = cJSON_PrintUnformatted(search_json);
    cJSON_Delete(search_json);
    if (search_literal == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    char template_text[2048] = {0};
    int template_len = snprintf(template_text, sizeof(template_text),
        "{%% set offset = %u %%}"
        "{%% set limit = %u %%}"
        "{%% set q = (%s|lower) %%}"
        "{%% set ns = namespace(items=[], total=0) %%}"
        "{%% for s in states.%s|sort(attribute='entity_id') %%}"
        "{%% set room = area_name(s.entity_id) or '' %%}"
        "{%% set hay = (s.entity_id ~ ' ' ~ s.name ~ ' ' ~ room)|lower %%}"
        "{%% if q == '' or q in hay %%}"
        "{%% set idx = ns.total %%}"
        "{%% set ns.total = ns.total + 1 %%}"
        "{%% if idx >= offset and idx < offset + limit %%}"
        "{%% set ns.items = ns.items + [dict(id=s.entity_id, name=s.name, room=room, icon=(s.attributes.icon|default('', true)))] %%}"
        "{%% endif %%}"
        "{%% endif %%}"
        "{%% endfor %%}"
        "{{ {'total': ns.total, 'items': ns.items}|to_json }}",
        (unsigned)offset, (unsigned)page_size, search_literal, domain);
    cJSON_free(search_literal);
    if (template_len <= 0 || (size_t)template_len >= sizeof(template_text)) {
        err = ESP_ERR_INVALID_SIZE;
        goto fail;
    }

    cJSON *body_root = cJSON_CreateObject();
    if (body_root == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    cJSON_AddStringToObject(body_root, "template", template_text);
    char *body = cJSON_PrintUnformatted(body_root);
    cJSON_Delete(body_root);
    if (body == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    err = ha_client_ensure_http_client(base_url, cert_common_name);
    if (err != ESP_OK) {
        cJSON_free(body);
        goto fail;
    }

    char auth_header[640] = {0};
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_client.access_token);
    esp_http_client_set_url(s_client.http_client, url);
    esp_http_client_set_method(s_client.http_client, HTTP_METHOD_POST);
    esp_http_client_set_header(s_client.http_client, "Authorization", auth_header);
    esp_http_client_set_header(s_client.http_client, "Accept", "text/plain");
    esp_http_client_set_header(s_client.http_client, "Content-Type", "application/json");
    if (host_header[0] != '\0') {
        esp_http_client_set_header(s_client.http_client, "Host", host_header);
    }

    int body_len = (int)strlen(body);
    err = ha_client_http_open_budgeted(s_client.http_client, body_len, "light-discovery");
    if (err != ESP_OK) {
        cJSON_free(body);
        if (err != ESP_ERR_TIMEOUT) {
            ha_client_reset_http_client();
        }
        goto fail;
    }

    int written = esp_http_client_write(s_client.http_client, body, body_len);
    cJSON_free(body);
    if (written != body_len) {
        esp_http_client_close(s_client.http_client);
        err = ESP_FAIL;
        goto fail;
    }

    int64_t content_length = esp_http_client_fetch_headers(s_client.http_client);
    int status = esp_http_client_get_status_code(s_client.http_client);
    if (status != 200) {
        esp_http_client_close(s_client.http_client);
        err = ESP_ERR_INVALID_RESPONSE;
        goto fail;
    }

    size_t payload_cap = 8192;
    if (content_length > 0 && content_length < 16384) {
        payload_cap = (size_t)content_length + 1U;
    }
    char *payload = (char *)heap_caps_calloc(payload_cap, sizeof(char), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (payload == NULL) {
        payload = (char *)calloc(payload_cap, sizeof(char));
    }
    if (payload == NULL) {
        esp_http_client_close(s_client.http_client);
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    int total_read = 0;
    while (total_read < (int)payload_cap - 1) {
        int read =
            esp_http_client_read(s_client.http_client, payload + total_read, (int)payload_cap - 1 - total_read);
        if (read < 0) {
            err = ESP_FAIL;
            break;
        }
        if (read == 0) {
            break;
        }
        total_read += read;
    }
    esp_http_client_close(s_client.http_client);
    if (err != ESP_OK) {
        free(payload);
        goto fail;
    }
    payload[total_read] = '\0';

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        err = ESP_ERR_INVALID_RESPONSE;
        goto fail;
    }

    cJSON *total_item = cJSON_GetObjectItemCaseSensitive(root, "total");
    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    uint16_t total = cJSON_IsNumber(total_item) && total_item->valuedouble > 0
        ? (uint16_t)total_item->valuedouble
        : 0;
    uint16_t page_count = cJSON_IsArray(items) ? (uint16_t)cJSON_GetArraySize(items) : 0;

    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    if (offset == 0) {
        ha_client_light_discovery_reset_pending_locked();
    }
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, items)
    {
        if (!cJSON_IsObject(entry)) {
            continue;
        }
        ha_light_discovery_pending_item_t item = {0};
        if (!ha_client_json_string_value(entry, "id", item.id, sizeof(item.id)) ||
            !ha_client_entity_matches_domain(item.id, domain)) {
            continue;
        }
        if (!ha_client_json_string_value(entry, "name", item.name, sizeof(item.name))) {
            safe_copy_cstr(item.name, sizeof(item.name), item.id);
        }
        (void)ha_client_json_string_value(entry, "room", item.room, sizeof(item.room));
        (void)ha_client_json_string_value(entry, "icon", item.icon, sizeof(item.icon));
        ha_client_light_discovery_add_pending_locked(&item);
    }

    if (total == 0) {
        total = (uint16_t)(offset + page_count);
    }
    uint16_t next_offset = (uint16_t)(offset + page_count);
    int64_t complete_ms = ha_client_now_ms();
    s_client.light_discovery_template_total = total;
    if (page_count == 0 || page_count < page_size || next_offset >= total ||
        next_offset >= APP_HA_LIGHT_DISCOVERY_MAX_ITEMS) {
        if (total > APP_HA_LIGHT_DISCOVERY_MAX_ITEMS || next_offset >= APP_HA_LIGHT_DISCOVERY_MAX_ITEMS) {
            s_client.light_discovery_pending_truncated = true;
        }
        ha_client_light_discovery_finish_locked(complete_ms);
    } else {
        s_client.light_discovery_template_offset = next_offset;
        s_client.light_discovery_next_step_unix_ms = complete_ms + HA_LIGHT_DISCOVERY_PAGE_STEP_DELAY_MS;
        s_client.light_discovery_requested = true;
        s_client.light_discovery_inflight = false;
        s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_TEMPLATE;
    }
    xSemaphoreGive(s_client.mutex);
    cJSON_Delete(root);

    ESP_LOGI(TAG_HA_CLIENT, "Entity discovery page: domain=%s offset=%u count=%u total=%u",
        domain, (unsigned)offset, (unsigned)page_count, (unsigned)total);
    return ESP_OK;

fail:
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    s_client.light_discovery_inflight = false;
    s_client.light_discovery_requested = true;
    s_client.light_discovery_next_step_unix_ms = ha_client_now_ms() + HA_LIGHT_DISCOVERY_RETRY_DELAY_MS;
    xSemaphoreGive(s_client.mutex);
    ESP_LOGW(TAG_HA_CLIENT, "Entity discovery page failed: domain=%s err=%s", domain, esp_err_to_name(err));
    return err;
}

static bool ha_client_entity_id_in_list(const char *entity_ids, size_t count, const char *entity_id)
{
    if (entity_ids == NULL || entity_id == NULL) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        const char *entry = entity_ids + (i * APP_MAX_ENTITY_ID_LEN);
        if (strncmp(entry, entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            return true;
        }
    }
    return false;
}

static bool ha_client_energy_has_manual_entities(cJSON *energy)
{
    if (!cJSON_IsObject(energy)) {
        return false;
    }
    static const char *energy_keys[] = {
        "home_power_entity_id",
        "solar_power_entity_id",
        "grid_power_entity_id",
        "grid_import_power_entity_id",
        "grid_export_power_entity_id",
        "battery_power_entity_id",
        "battery_charge_power_entity_id",
        "battery_discharge_power_entity_id",
        "battery_soc_entity_id",
    };
    for (size_t i = 0; i < (sizeof(energy_keys) / sizeof(energy_keys[0])); i++) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(energy, energy_keys[i]);
        if (cJSON_IsString(item) && item->valuestring != NULL && item->valuestring[0] != '\0') {
            return true;
        }
    }
    return false;
}

static bool ha_client_energy_page_uses_manual_live(cJSON *energy)
{
    if (!cJSON_IsObject(energy)) {
        return false;
    }
    cJSON *source = cJSON_GetObjectItemCaseSensitive(energy, "source");
    if (cJSON_IsString(source) && source->valuestring != NULL && source->valuestring[0] != '\0') {
        return strcmp(source->valuestring, "manual_live") == 0;
    }
    return ha_client_energy_has_manual_entities(energy);
}

static void ha_client_collect_entity_id(
    cJSON *widget, const char *key, char *entity_ids, size_t *count, size_t max_count)
{
    if (widget == NULL || key == NULL || entity_ids == NULL || count == NULL || *count >= max_count) {
        return;
    }

    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(widget, key);
    if (!cJSON_IsString(id_item) || id_item->valuestring == NULL || id_item->valuestring[0] == '\0') {
        return;
    }
    if (ha_client_entity_id_in_list(entity_ids, *count, id_item->valuestring)) {
        return;
    }

    char *dst = entity_ids + (*count * APP_MAX_ENTITY_ID_LEN);
    safe_copy_cstr(dst, APP_MAX_ENTITY_ID_LEN, id_item->valuestring);
    (*count)++;
}

static size_t ha_client_collect_layout_entity_ids(char *entity_ids, size_t max_count, bool *out_need_weather_forecast)
{
    if (entity_ids == NULL || max_count == 0) {
        if (out_need_weather_forecast != NULL) {
            *out_need_weather_forecast = false;
        }
        return 0;
    }

    char *layout_json = NULL;
    esp_err_t load_err = layout_store_load(&layout_json);
    if (load_err != ESP_OK || layout_json == NULL) {
        layout_json = strdup(layout_store_default_json());
        if (layout_json == NULL) {
            return 0;
        }
    }

    cJSON *root = cJSON_Parse(layout_json);
    free(layout_json);
    if (root == NULL) {
        if (out_need_weather_forecast != NULL) {
            *out_need_weather_forecast = false;
        }
        return 0;
    }

    size_t count = 0;
    bool need_weather_forecast = false;
    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (cJSON_IsArray(pages)) {
        int page_count = cJSON_GetArraySize(pages);
        for (int p = 0; p < page_count && count < max_count; p++) {
            cJSON *page = cJSON_GetArrayItem(pages, p);
            cJSON *page_type = cJSON_GetObjectItemCaseSensitive(page, "type");
            if (cJSON_IsString(page_type) && page_type->valuestring != NULL &&
                strcmp(page_type->valuestring, "energy_dashboard") == 0) {
                cJSON *energy = cJSON_GetObjectItemCaseSensitive(page, "energy");
                static const char *energy_keys[] = {
                    "home_power_entity_id",
                    "solar_power_entity_id",
                    "grid_power_entity_id",
                    "grid_import_power_entity_id",
                    "grid_export_power_entity_id",
                    "battery_power_entity_id",
                    "battery_charge_power_entity_id",
                    "battery_discharge_power_entity_id",
                    "battery_soc_entity_id",
                };
                if (cJSON_IsObject(energy)) {
                    if (ha_client_energy_page_uses_manual_live(energy)) {
                        for (size_t i = 0; i < (sizeof(energy_keys) / sizeof(energy_keys[0])) && count < max_count; i++) {
                            ha_client_collect_entity_id(energy, energy_keys[i], entity_ids, &count, max_count);
                        }
                    }
                }
                continue;
            }

            cJSON *widgets = cJSON_GetObjectItemCaseSensitive(page, "widgets");
            if (!cJSON_IsArray(widgets)) {
                continue;
            }

            int widget_count = cJSON_GetArraySize(widgets);
            for (int w = 0; w < widget_count && count < max_count; w++) {
                cJSON *widget = cJSON_GetArrayItem(widgets, w);
                if (!cJSON_IsObject(widget)) {
                    continue;
                }
                cJSON *type = cJSON_GetObjectItemCaseSensitive(widget, "type");
                if (cJSON_IsString(type) && type->valuestring != NULL &&
                    strcmp(type->valuestring, "weather_3day") == 0) {
                    need_weather_forecast = true;
                }
                ha_client_collect_entity_id(widget, "entity_id", entity_ids, &count, max_count);
                ha_client_collect_entity_id(widget, "secondary_entity_id", entity_ids, &count, max_count);
            }
        }
    }

    cJSON_Delete(root);
    if (out_need_weather_forecast != NULL) {
        *out_need_weather_forecast = need_weather_forecast;
    }
    return count;
}

static bool ha_client_layout_needs_ha_energy(void)
{
    char *layout_json = NULL;
    esp_err_t load_err = layout_store_load(&layout_json);
    if (load_err != ESP_OK || layout_json == NULL) {
        layout_json = strdup(layout_store_default_json());
        if (layout_json == NULL) {
            return false;
        }
    }

    cJSON *root = cJSON_Parse(layout_json);
    free(layout_json);
    if (root == NULL) {
        return false;
    }

    bool needs_ha_energy = false;
    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (cJSON_IsArray(pages)) {
        int page_count = cJSON_GetArraySize(pages);
        for (int p = 0; p < page_count; p++) {
            cJSON *page = cJSON_GetArrayItem(pages, p);
            cJSON *page_type = cJSON_GetObjectItemCaseSensitive(page, "type");
            if (!cJSON_IsString(page_type) || page_type->valuestring == NULL ||
                strcmp(page_type->valuestring, "energy_dashboard") != 0) {
                continue;
            }

            cJSON *energy = cJSON_GetObjectItemCaseSensitive(page, "energy");
            if (!ha_client_energy_page_uses_manual_live(energy)) {
                needs_ha_energy = true;
                break;
            }
        }
    }

    cJSON_Delete(root);
    return needs_ha_energy;
}

static int ha_client_entity_id_sort_cmp(const void *lhs, const void *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return 0;
    }
    return strncmp((const char *)lhs, (const char *)rhs, APP_MAX_ENTITY_ID_LEN);
}

static uint32_t ha_client_layout_entity_signature(char *entity_ids, size_t entity_count)
{
    if (entity_ids == NULL || entity_count == 0) {
        return 0;
    }

    qsort(entity_ids, entity_count, APP_MAX_ENTITY_ID_LEN, ha_client_entity_id_sort_cmp);

    uint32_t hash = 2166136261u; /* FNV-1a 32-bit */
    for (size_t i = 0; i < entity_count; i++) {
        const char *entry = entity_ids + (i * APP_MAX_ENTITY_ID_LEN);
        for (size_t j = 0; j < APP_MAX_ENTITY_ID_LEN; j++) {
            uint8_t ch = (uint8_t)entry[j];
            if (ch == '\0') {
                break;
            }
            hash ^= ch;
            hash *= 16777619u;
        }
        hash ^= 0xFFu; /* delimiter to avoid concatenation ambiguity */
        hash *= 16777619u;
    }
    return hash;
}

static bool ha_client_capture_layout_snapshot(
    uint32_t *out_signature, uint16_t *out_count, bool *out_need_weather_forecast)
{
    if (out_signature == NULL || out_count == NULL || out_need_weather_forecast == NULL) {
        return false;
    }

    *out_signature = 0;
    *out_count = 0;
    *out_need_weather_forecast = false;

    size_t max_entities = (size_t)HA_LAYOUT_ENTITY_MAX;
    char *entity_ids = calloc(max_entities, APP_MAX_ENTITY_ID_LEN);
    if (entity_ids == NULL) {
        return false;
    }

    bool need_weather_forecast = false;
    size_t entity_count = ha_client_collect_layout_entity_ids(entity_ids, max_entities, &need_weather_forecast);
    uint32_t signature = ha_client_layout_entity_signature(entity_ids, entity_count);

    free(entity_ids);

    *out_signature = signature;
    *out_count = (entity_count > UINT16_MAX) ? UINT16_MAX : (uint16_t)entity_count;
    *out_need_weather_forecast = need_weather_forecast;
    return true;
}

static void ha_client_refresh_layout_capabilities(void)
{
    uint32_t signature = 0;
    uint16_t entity_count = 0;
    bool need_weather_forecast = false;
    if (!ha_client_capture_layout_snapshot(&signature, &entity_count, &need_weather_forecast)) {
        return;
    }

    if (s_client.mutex != NULL) {
        bool changed = false;
        bool rest_enabled = false;
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        changed = (s_client.layout_needs_weather_forecast != need_weather_forecast);
        s_client.layout_needs_weather_forecast = need_weather_forecast;
        rest_enabled = s_client.rest_enabled;
        s_client.layout_entity_signature = signature;
        s_client.layout_entity_count = entity_count;
        xSemaphoreGive(s_client.mutex);
        if (changed) {
            ESP_LOGI(TAG_HA_CLIENT, "Layout capability: weather forecast %s (%s)",
                need_weather_forecast ? "needed" : "not needed",
                rest_enabled ? "REST fallback enabled" : "WS-only mode");
        }
    }
}

static esp_err_t ha_client_fetch_state_http(
    const char *entity_id, bool allow_weather_forecast_rest, bool allow_when_rest_disabled)
{
    if (entity_id == NULL || entity_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!allow_when_rest_disabled && !ha_client_rest_enabled()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    int64_t t_start_ms = ha_client_now_ms();

    char base_url[256] = {0};
    char host_header[192] = {0};
    char cert_common_name[128] = {0};
    if (!ha_client_build_http_request_context(s_client.ws_url, base_url, sizeof(base_url), host_header,
            sizeof(host_header), cert_common_name, sizeof(cert_common_name))) {
        ESP_LOGW(TAG_HA_CLIENT, "Failed to build HA HTTP request context");
        return ESP_ERR_HTTP_CONNECT;
    }

    char url[384] = {0};
    int url_len = snprintf(url, sizeof(url), "%s/api/states/%s", base_url, entity_id);
    if (url_len <= 0 || (size_t)url_len >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = ha_client_ensure_http_client(base_url, cert_common_name);
    if (err != ESP_OK) {
        return err;
    }

    char auth_header[640] = {0};
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_client.access_token);
    esp_http_client_set_url(s_client.http_client, url);
    esp_http_client_set_method(s_client.http_client, HTTP_METHOD_GET);
    esp_http_client_set_header(s_client.http_client, "Authorization", auth_header);
    esp_http_client_set_header(s_client.http_client, "Accept", "application/json");
    if (host_header[0] != '\0') {
        esp_http_client_set_header(s_client.http_client, "Host", host_header);
    }

    err = ha_client_http_open_budgeted(s_client.http_client, 0, "sync-state");
    if (err != ESP_OK) {
        /* Force fresh DNS/TLS context only after real transport errors. */
        if (err != ESP_ERR_TIMEOUT) {
            ha_client_reset_http_client();
        }
        return err;
    }

    int64_t content_length = esp_http_client_fetch_headers(s_client.http_client);
    size_t payload_cap = 8192;
    if (content_length > 0 && content_length < 32768) {
        payload_cap = (size_t)content_length + 1U;
    }
    char *payload = calloc(payload_cap, sizeof(char));
    if (payload == NULL) {
        esp_http_client_close(s_client.http_client);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    while (total_read < (int)payload_cap - 1) {
        int read =
            esp_http_client_read(s_client.http_client, payload + total_read, (int)payload_cap - 1 - total_read);
        if (read < 0) {
            err = ESP_FAIL;
            break;
        }
        if (read == 0) {
            break;
        }
        total_read += read;
    }
    payload[total_read] = '\0';

    int status = esp_http_client_get_status_code(s_client.http_client);
    esp_http_client_close(s_client.http_client);

    if (err != ESP_OK) {
        free(payload);
        ha_client_reset_http_client();
        return err;
    }
    if (status == 404) {
        free(payload);
        return ESP_ERR_NOT_FOUND;
    }
    if (status != 200) {
        free(payload);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *state_obj = cJSON_Parse(payload);
    free(payload);
    if (!cJSON_IsObject(state_obj)) {
        cJSON_Delete(state_obj);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (allow_weather_forecast_rest && ha_client_entity_is_weather(entity_id)) {
        cJSON *attrs = cJSON_GetObjectItemCaseSensitive(state_obj, "attributes");
        if (!cJSON_IsObject(attrs)) {
            attrs = cJSON_CreateObject();
            if (attrs != NULL) {
                cJSON_AddItemToObject(state_obj, "attributes", attrs);
            }
        }

        if (cJSON_IsObject(attrs)) {
            cJSON *forecast = cJSON_GetObjectItemCaseSensitive(attrs, "forecast");
            cJSON *forecast_daily = cJSON_GetObjectItemCaseSensitive(attrs, "forecast_daily");
            if (!cJSON_IsArray(forecast) && !cJSON_IsArray(forecast_daily)) {
                cJSON *service_forecast = NULL;
                esp_err_t forecast_err =
                    ha_client_fetch_weather_daily_forecast_http(base_url, host_header, entity_id, &service_forecast);
                if (forecast_err == ESP_OK && cJSON_IsArray(service_forecast)) {
                    cJSON_AddItemToObject(attrs, "forecast", service_forecast);
                } else if (service_forecast != NULL) {
                    cJSON_Delete(service_forecast);
                }
            }
        }
    }

    (void)ha_client_import_state_object(state_obj);
    cJSON_Delete(state_obj);
    int64_t t_total_ms = ha_client_now_ms() - t_start_ms;
    if (t_total_ms >= HA_SVC_LATENCY_WARN_MS) {
        ESP_LOGW(TAG_HA_CLIENT, "Slow REST sync-state for %s: %" PRId64 " ms", entity_id, t_total_ms);
    } else if (t_total_ms >= HA_SVC_LATENCY_INFO_MS) {
        ESP_LOGI(TAG_HA_CLIENT, "REST sync-state for %s: %" PRId64 " ms", entity_id, t_total_ms);
    }
    return ESP_OK;
}

static esp_err_t ha_client_sync_layout_entity_step(bool is_initial, uint32_t *io_index, uint32_t *out_count,
    uint32_t *io_imported, bool *out_done, bool allow_http_when_rest_disabled)
{
    if (io_index == NULL || out_count == NULL || out_done == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t max_entities = (size_t)HA_LAYOUT_ENTITY_MAX;
    char *entity_ids = calloc(max_entities, APP_MAX_ENTITY_ID_LEN);
    if (entity_ids == NULL) {
        return ESP_ERR_NO_MEM;
    }

    bool need_weather_forecast = false;
    size_t entity_count = ha_client_collect_layout_entity_ids(entity_ids, max_entities, &need_weather_forecast);
    if (s_client.mutex != NULL) {
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        s_client.layout_needs_weather_forecast = need_weather_forecast;
        xSemaphoreGive(s_client.mutex);
    }
    *out_count = (uint32_t)entity_count;
    if (entity_count == 0) {
        *out_done = true;
        free(entity_ids);
        return ESP_OK;
    }
    if (!is_initial && *io_index >= entity_count) {
        *io_index = (uint32_t)(*io_index % entity_count);
    }
    if (*io_index >= entity_count) {
        *out_done = true;
        free(entity_ids);
        return ESP_OK;
    }

    const char *entity_id = entity_ids + ((size_t)(*io_index) * APP_MAX_ENTITY_ID_LEN);
    esp_err_t err = ha_client_fetch_state_http(entity_id, need_weather_forecast, allow_http_when_rest_disabled);
    if (err == ESP_OK) {
        if (io_imported != NULL) {
            (*io_imported)++;
        }
        ha_client_publish_event(EV_HA_STATE_CHANGED, entity_id);
    } else {
        ESP_LOGW(TAG_HA_CLIENT, "%s layout state sync failed for '%s': %s", is_initial ? "Initial" : "Periodic",
            entity_id, esp_err_to_name(err));
    }

    (*io_index)++;
    *out_done = is_initial ? (*io_index >= entity_count) : false;
    free(entity_ids);
    return err;
}

static uint32_t ha_client_next_message_id(void)
{
    uint32_t id;
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    id = ++s_client.next_message_id;
    if (s_client.next_message_id == 0U) {
        s_client.next_message_id = 1U;
    }
    xSemaphoreGive(s_client.mutex);
    return id;
}

static void ha_client_mark_ws_priority_boost(int64_t now_ms)
{
    if (s_client.mutex == NULL) {
        return;
    }

    if (now_ms <= 0) {
        now_ms = ha_client_now_ms();
    }
    int64_t boost_until = now_ms + HA_WS_PRIORITY_BOOST_MS;

    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    if (s_client.ws_priority_boost_until_unix_ms < boost_until) {
        s_client.ws_priority_boost_until_unix_ms = boost_until;
    }
    xSemaphoreGive(s_client.mutex);
}

static esp_err_t ha_client_send_json(cJSON *obj)
{
    char *payload = cJSON_PrintUnformatted(obj);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = ha_ws_send_text(payload);
    if (err != ESP_OK && ha_ws_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(15));
        err = ha_ws_send_text(payload);
    }
    cJSON_free(payload);
    return err;
}

static void ha_client_publish_event(app_event_type_t type, const char *entity_id)
{
    static uint32_t dropped_count = 0;
    static int64_t last_drop_log_ms = 0;

    app_event_t event = {.type = type};
    if (type == EV_HA_STATE_CHANGED && entity_id != NULL) {
        snprintf(event.data.ha_state_changed.entity_id, sizeof(event.data.ha_state_changed.entity_id), "%s", entity_id);
    }

    bool queued = app_events_publish(&event, pdMS_TO_TICKS(10));
    if (queued) {
        return;
    }

    dropped_count++;
    int64_t now_ms = ha_client_now_ms();
    if ((now_ms - last_drop_log_ms) < 5000) {
        return;
    }

    QueueHandle_t q = app_events_get_queue();
    UBaseType_t depth = (q != NULL) ? uxQueueMessagesWaiting(q) : 0;
    ESP_LOGW(TAG_HA_CLIENT,
        "App event queue saturated: dropped=%u type=%d entity=%s depth=%u/%u",
        (unsigned)dropped_count,
        (int)type,
        (entity_id != NULL && entity_id[0] != '\0') ? entity_id : "-",
        (unsigned)depth,
        (unsigned)APP_EVENT_QUEUE_LENGTH);
    dropped_count = 0;
    last_drop_log_ms = now_ms;
}

static esp_err_t ha_client_send_auth(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "auth");
    cJSON_AddStringToObject(root, "access_token", s_client.access_token);
    esp_err_t err = ha_client_send_json(root);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG_HA_CLIENT, "Failed to send auth");
    }
    cJSON_Delete(root);
    return err;
}

static esp_err_t ha_client_send_get_states(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint32_t req_id = ha_client_next_message_id();
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    s_client.get_states_req_id = req_id;
    xSemaphoreGive(s_client.mutex);

    cJSON_AddNumberToObject(root, "id", (double)req_id);
    cJSON_AddStringToObject(root, "type", "get_states");
    esp_err_t err = ha_client_send_json(root);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_HA_CLIENT, "Failed to request states");
    }
    cJSON_Delete(root);
    return err;
}

static void ha_client_energy_clear_refs_locked(void)
{
    memset(s_client.energy_stat_refs, 0, sizeof(s_client.energy_stat_refs));
    s_client.energy_stat_ref_count = 0;
}

static void ha_client_energy_add_ref_locked(ha_energy_stat_kind_t kind, const char *stat_id, const char *unit)
{
    if (stat_id == NULL || stat_id[0] == '\0') {
        return;
    }
    for (uint8_t i = 0; i < s_client.energy_stat_ref_count; i++) {
        if (s_client.energy_stat_refs[i].kind == kind &&
            strncmp(s_client.energy_stat_refs[i].id, stat_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            return;
        }
    }
    if (s_client.energy_stat_ref_count >= HA_ENERGY_STAT_REF_MAX) {
        return;
    }
    ha_energy_stat_ref_t *ref = &s_client.energy_stat_refs[s_client.energy_stat_ref_count++];
    ref->kind = kind;
    safe_copy_cstr(ref->id, sizeof(ref->id), stat_id);
    safe_copy_cstr(ref->unit, sizeof(ref->unit), unit);
}

static const char *ha_client_json_string(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_IsObject(obj) ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    return (cJSON_IsString(item) && item->valuestring != NULL) ? item->valuestring : "";
}

static void ha_client_energy_parse_prefs_locked(cJSON *result)
{
    ha_client_energy_clear_refs_locked();
    cJSON *sources = cJSON_IsObject(result) ? cJSON_GetObjectItemCaseSensitive(result, "energy_sources") : NULL;
    if (!cJSON_IsArray(sources)) {
        return;
    }

    cJSON *source = NULL;
    cJSON_ArrayForEach(source, sources)
    {
        const char *type = ha_client_json_string(source, "type");
        if (strcmp(type, "grid") == 0) {
            ha_client_energy_add_ref_locked(
                HA_ENERGY_STAT_GRID_FROM, ha_client_json_string(source, "stat_energy_from"), NULL);
            ha_client_energy_add_ref_locked(
                HA_ENERGY_STAT_GRID_TO, ha_client_json_string(source, "stat_energy_to"), NULL);
        } else if (strcmp(type, "solar") == 0) {
            ha_client_energy_add_ref_locked(
                HA_ENERGY_STAT_SOLAR, ha_client_json_string(source, "stat_energy_from"), NULL);
        } else if (strcmp(type, "battery") == 0) {
            ha_client_energy_add_ref_locked(
                HA_ENERGY_STAT_BATTERY_FROM, ha_client_json_string(source, "stat_energy_from"), NULL);
            ha_client_energy_add_ref_locked(
                HA_ENERGY_STAT_BATTERY_TO, ha_client_json_string(source, "stat_energy_to"), NULL);
        } else if (strcmp(type, "gas") == 0) {
            ha_client_energy_add_ref_locked(HA_ENERGY_STAT_GAS,
                ha_client_json_string(source, "stat_energy_from"),
                ha_client_json_string(source, "unit_of_measurement"));
        } else if (strcmp(type, "water") == 0) {
            ha_client_energy_add_ref_locked(HA_ENERGY_STAT_WATER,
                ha_client_json_string(source, "stat_energy_from"),
                ha_client_json_string(source, "unit_of_measurement"));
        }
    }
}

static bool ha_client_format_energy_period_iso(char *start_buf, size_t start_size, char *end_buf, size_t end_size)
{
    if (start_buf == NULL || start_size == 0 || end_buf == NULL || end_size == 0) {
        return false;
    }

    time_t now = time(NULL);
    if (now <= 0) {
        return false;
    }

    struct tm local_start = {0};
    localtime_r(&now, &local_start);
    local_start.tm_hour = 0;
    local_start.tm_min = 0;
    local_start.tm_sec = 0;
    time_t start = mktime(&local_start);

    struct tm local_end = local_start;
    local_end.tm_hour = 23;
    local_end.tm_min = 59;
    local_end.tm_sec = 59;
    time_t end = mktime(&local_end);
    if (start <= 0 || end <= start) {
        return false;
    }

    struct tm utc_start = {0};
    struct tm utc_end = {0};
    gmtime_r(&start, &utc_start);
    gmtime_r(&end, &utc_end);
    return strftime(start_buf, start_size, "%Y-%m-%dT%H:%M:%SZ", &utc_start) > 0 &&
           strftime(end_buf, end_size, "%Y-%m-%dT%H:%M:%SZ", &utc_end) > 0;
}

static esp_err_t ha_client_send_energy_prefs_ws(uint32_t *out_req_id)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uint32_t req_id = ha_client_next_message_id();
    cJSON_AddNumberToObject(root, "id", (double)req_id);
    cJSON_AddStringToObject(root, "type", "energy/get_prefs");
    esp_err_t err = ha_client_send_json(root);
    cJSON_Delete(root);
    if (err == ESP_OK && out_req_id != NULL) {
        *out_req_id = req_id;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG_HA_CLIENT, "Failed to request HA energy preferences");
    }
    return err;
}

#define HA_ENERGY_STATS_BATCH_SIZE 2U
/* Inter-batch delay.  Purpose is to relieve pressure on the mbedTLS stack
 * on the ESP: each batch is a WSS request/response with a large JSON
 * payload, which spikes heap + the TLS record decoder.  Pausing between
 * batches lets the receive buffer drain and cJSON allocations be freed
 * before the next roundtrip starts, avoiding -0x7100-class TLS errors.
 * 400 ms is a solid compromise vs the original 1000 ms: still gives mbedTLS
 * room to breathe while cutting ~2.4 s off a typical 4-batch sync. */
#define HA_ENERGY_STATS_BATCH_DELAY_MS 400U

static esp_err_t ha_client_send_energy_stats_ws(uint32_t *out_req_id)
{
    ha_energy_stat_ref_t refs[HA_ENERGY_STAT_REF_MAX] = {0};
    uint8_t ref_count = 0;
    uint8_t batch_offset = 0;
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    ref_count = s_client.energy_stat_ref_count;
    batch_offset = s_client.energy_stats_batch_offset;
    if (ref_count > 0) {
        memcpy(refs, s_client.energy_stat_refs, sizeof(ha_energy_stat_ref_t) * ref_count);
    }
    xSemaphoreGive(s_client.mutex);

    if (ref_count == 0 || batch_offset >= ref_count) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t batch_end = batch_offset + HA_ENERGY_STATS_BATCH_SIZE;
    if (batch_end > ref_count) {
        batch_end = ref_count;
    }

    char start_time[32] = {0};
    char end_time[32] = {0};
    if (!ha_client_format_energy_period_iso(start_time, sizeof(start_time), end_time, sizeof(end_time))) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *stat_ids = cJSON_CreateArray();
    cJSON *types = cJSON_CreateArray();
    cJSON *units = cJSON_CreateObject();
    if (root == NULL || stat_ids == NULL || types == NULL || units == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(stat_ids);
        cJSON_Delete(types);
        cJSON_Delete(units);
        return ESP_ERR_NO_MEM;
    }

    uint32_t req_id = ha_client_next_message_id();
    cJSON_AddNumberToObject(root, "id", (double)req_id);
    cJSON_AddStringToObject(root, "type", "recorder/statistics_during_period");
    cJSON_AddStringToObject(root, "start_time", start_time);
    cJSON_AddStringToObject(root, "end_time", end_time);
    cJSON_AddStringToObject(root, "period", "hour");
    cJSON_AddStringToObject(units, "energy", "kWh");
    cJSON_AddItemToObject(root, "units", units);
    units = NULL;
    cJSON *change_type = cJSON_CreateString("change");
    if (change_type == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(stat_ids);
        cJSON_Delete(types);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToArray(types, change_type);
    cJSON_AddItemToObject(root, "types", types);
    types = NULL;
    for (uint8_t i = batch_offset; i < batch_end; i++) {
        cJSON *stat_id = cJSON_CreateString(refs[i].id);
        if (stat_id == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(stat_ids);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddItemToArray(stat_ids, stat_id);
    }
    cJSON_AddItemToObject(root, "statistic_ids", stat_ids);
    stat_ids = NULL;

    esp_err_t err = ha_client_send_json(root);
    cJSON_Delete(root);
    if (err == ESP_OK && out_req_id != NULL) {
        *out_req_id = req_id;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG_HA_CLIENT, "Failed to request HA energy statistics");
    }
    return err;
}

static float ha_client_energy_sum_change(cJSON *series)
{
    if (!cJSON_IsArray(series)) {
        return 0.0f;
    }
    double total = 0.0;
    cJSON *point = NULL;
    cJSON_ArrayForEach(point, series)
    {
        cJSON *change = cJSON_IsObject(point) ? cJSON_GetObjectItemCaseSensitive(point, "change") : NULL;
        if (cJSON_IsNumber(change)) {
            total += change->valuedouble;
        }
    }
    return (float)total;
}

static void ha_client_energy_handle_stats_result(cJSON *result, bool is_final_batch)
{
    ha_energy_stat_ref_t refs[HA_ENERGY_STAT_REF_MAX] = {0};
    uint8_t ref_count = 0;
    uint8_t batch_offset = 0;
    uint8_t batch_end = 0;
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    ref_count = s_client.energy_stat_ref_count;
    batch_offset = s_client.energy_stats_batch_offset;
    batch_end = batch_offset + HA_ENERGY_STATS_BATCH_SIZE;
    if (batch_end > ref_count) {
        batch_end = ref_count;
    }
    if (ref_count > 0) {
        memcpy(refs, s_client.energy_stat_refs, sizeof(ha_energy_stat_ref_t) * ref_count);
    }
    xSemaphoreGive(s_client.mutex);

    /* Accumulate only the refs that were in this batch */
    for (uint8_t i = batch_offset; i < batch_end; i++) {
        cJSON *series = cJSON_IsObject(result) ? cJSON_GetObjectItemCaseSensitive(result, refs[i].id) : NULL;
        float value = ha_client_energy_sum_change(series);
        switch (refs[i].kind) {
        case HA_ENERGY_STAT_GRID_FROM:
            s_client.energy_staging.has_grid = true;
            s_client.energy_staging.from_grid_kwh += value;
            break;
        case HA_ENERGY_STAT_GRID_TO:
            s_client.energy_staging.has_grid = true;
            s_client.energy_staging.to_grid_kwh += value;
            break;
        case HA_ENERGY_STAT_SOLAR:
            s_client.energy_staging.has_solar = true;
            s_client.energy_staging.solar_kwh += value;
            break;
        case HA_ENERGY_STAT_BATTERY_FROM:
            s_client.energy_staging.has_battery = true;
            s_client.energy_staging.from_battery_kwh += value;
            break;
        case HA_ENERGY_STAT_BATTERY_TO:
            s_client.energy_staging.has_battery = true;
            s_client.energy_staging.to_battery_kwh += value;
            break;
        case HA_ENERGY_STAT_GAS:
            s_client.energy_staging.has_gas = true;
            s_client.energy_staging.gas_value += value;
            if (s_client.energy_staging.gas_unit[0] == '\0') {
                safe_copy_cstr(s_client.energy_staging.gas_unit, sizeof(s_client.energy_staging.gas_unit), refs[i].unit);
            }
            break;
        case HA_ENERGY_STAT_WATER:
            s_client.energy_staging.has_water = true;
            s_client.energy_staging.water_value += value;
            if (s_client.energy_staging.water_unit[0] == '\0') {
                safe_copy_cstr(s_client.energy_staging.water_unit, sizeof(s_client.energy_staging.water_unit), refs[i].unit);
            }
            break;
        default:
            break;
        }
    }

    /* Only publish to model after the final batch */
    if (is_final_batch) {
        s_client.energy_staging.available = ref_count > 0;
        s_client.energy_staging.updated_unix_ms = esp_timer_get_time() / 1000;
        ha_energy_model_update(&s_client.energy_staging);
        ha_client_publish_event(EV_HA_ENERGY_CHANGED, NULL);
    }
}

static const char *ha_client_light_discovery_ws_type(ha_light_discovery_phase_t phase)
{
    switch (phase) {
    case HA_LIGHT_DISCOVERY_PHASE_ENTITY_DISPLAY:
        return "config/entity_registry/list_for_display";
    case HA_LIGHT_DISCOVERY_PHASE_ENTITY_FULL:
        return "config/entity_registry/list";
    case HA_LIGHT_DISCOVERY_PHASE_AREAS:
        return "config/area_registry/list";
    case HA_LIGHT_DISCOVERY_PHASE_DEVICES:
        return "config/device_registry/list";
    default:
        return NULL;
    }
}

static esp_err_t ha_client_send_light_discovery_request(ha_light_discovery_phase_t phase)
{
    const char *type = ha_client_light_discovery_ws_type(phase);
    if (type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uint32_t req_id = ha_client_next_message_id();
    cJSON_AddNumberToObject(root, "id", (double)req_id);
    cJSON_AddStringToObject(root, "type", type);

    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    s_client.light_discovery_req_id = req_id;
    s_client.light_discovery_phase = phase;
    s_client.light_discovery_inflight = true;
    s_client.light_discovery_requested = false;
    xSemaphoreGive(s_client.mutex);

    ESP_LOGI(TAG_HA_CLIENT, "Requesting HA light discovery phase %s", ha_client_light_discovery_phase_name(phase));
    esp_err_t err = ha_client_send_json(root);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        if (s_client.light_discovery_req_id == req_id) {
            s_client.light_discovery_inflight = false;
            s_client.light_discovery_requested = true;
            s_client.light_discovery_req_id = 0;
        }
        xSemaphoreGive(s_client.mutex);
        ESP_LOGW(TAG_HA_CLIENT, "Failed to request light discovery phase %s: %s",
            ha_client_light_discovery_phase_name(phase), esp_err_to_name(err));
    }
    return err;
}

static esp_err_t ha_client_send_weather_daily_forecast_ws(const char *entity_id, uint32_t *out_req_id)
{
    if (entity_id == NULL || entity_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON *service_data = cJSON_CreateObject();
    cJSON *target = cJSON_CreateObject();
    if (service_data == NULL || target == NULL) {
        cJSON_Delete(service_data);
        cJSON_Delete(target);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    uint32_t req_id = ha_client_next_message_id();
    cJSON_AddNumberToObject(root, "id", (double)req_id);
    cJSON_AddStringToObject(root, "type", "call_service");
    cJSON_AddStringToObject(root, "domain", "weather");
    cJSON_AddStringToObject(root, "service", "get_forecasts");
    cJSON_AddBoolToObject(root, "return_response", true);
    cJSON_AddStringToObject(service_data, "type", "daily");
    cJSON_AddStringToObject(target, "entity_id", entity_id);
    cJSON_AddItemToObject(root, "service_data", service_data);
    cJSON_AddItemToObject(root, "target", target);

    ESP_LOGI(TAG_HA_CLIENT, "Requesting WS weather forecast for %s", entity_id);
    esp_err_t err = ha_client_send_json(root);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_HA_CLIENT, "Failed to request weather forecast via WS for '%s': %s",
            entity_id, esp_err_to_name(err));
    } else if (out_req_id != NULL) {
        *out_req_id = req_id;
    }
    cJSON_Delete(root);
    return err;
}

static esp_err_t ha_client_send_subscribe_single_entity(const char *entity_id, uint32_t *out_req_id)
{
    if (entity_id == NULL || entity_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *ids = cJSON_CreateArray();
    if (root == NULL || ids == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(ids);
        return ESP_ERR_NO_MEM;
    }

    uint32_t req_id = ha_client_next_message_id();
    cJSON_AddNumberToObject(root, "id", (double)req_id);
    cJSON_AddStringToObject(root, "type", "subscribe_entities");

    cJSON *id_item = cJSON_CreateString(entity_id);
    if (id_item == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToArray(ids, id_item);
    cJSON_AddItemToObject(root, "entity_ids", ids);

    esp_err_t err = ha_client_send_json(root);
    cJSON_Delete(root);
    if (err == ESP_OK && out_req_id != NULL) {
        *out_req_id = req_id;
    }
    return err;
}

static esp_err_t ha_client_send_subscribe_layout_state_trigger(void)
{
    size_t max_entities = (size_t)HA_LAYOUT_ENTITY_MAX;
    char *entity_ids = calloc(max_entities, APP_MAX_ENTITY_ID_LEN);
    if (entity_ids == NULL) {
        return ESP_ERR_NO_MEM;
    }

    bool need_weather_forecast = false;
    size_t entity_count = ha_client_collect_layout_entity_ids(entity_ids, max_entities, &need_weather_forecast);
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    s_client.layout_needs_weather_forecast = need_weather_forecast;
    xSemaphoreGive(s_client.mutex);

    if (entity_count == 0) {
        free(entity_ids);
        return ESP_ERR_NOT_FOUND;
    }
    size_t eligible_count = 0;
    for (size_t i = 0; i < entity_count; i++) {
        const char *entity_id = entity_ids + (i * APP_MAX_ENTITY_ID_LEN);
        if (ha_client_entity_should_use_trigger_subscription(entity_id)) {
            eligible_count++;
        }
    }
    size_t skipped_count = entity_count - eligible_count;
    if (eligible_count == 0) {
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        s_client.sub_state_via_trigger = false;
        s_client.trigger_sub_req_id = 0;
        s_client.sub_state_via_entities = false;
        s_client.entities_sub_req_id = 0;
        s_client.entities_sub_target_count = 0;
        s_client.entities_sub_sent_count = 0;
        s_client.entities_sub_seen_count = 0;
        s_client.next_entities_subscribe_unix_ms = 0;
        ha_client_clear_entities_sub_buffers();
        xSemaphoreGive(s_client.mutex);
        ESP_LOGW(TAG_HA_CLIENT, "No eligible entities for trigger subscription (skipped=%u)",
            (unsigned)skipped_count);
        free(entity_ids);
        return ESP_OK;
    }
    if (eligible_count > HA_TRIGGER_SUBSCRIBE_MAX_ENTITIES) {
        ESP_LOGW(TAG_HA_CLIENT,
            "Layout has %u eligible trigger entities; limit is %u. Falling back to global state_changed",
            (unsigned)eligible_count, (unsigned)HA_TRIGGER_SUBSCRIBE_MAX_ENTITIES);
        free(entity_ids);
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *triggers = cJSON_CreateArray();
    if (root == NULL || triggers == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(triggers);
        free(entity_ids);
        return ESP_ERR_NO_MEM;
    }

    uint32_t req_id = ha_client_next_message_id();
    cJSON_AddNumberToObject(root, "id", (double)req_id);
    cJSON_AddStringToObject(root, "type", "subscribe_trigger");

    for (size_t i = 0; i < entity_count; i++) {
        const char *entity_id = entity_ids + (i * APP_MAX_ENTITY_ID_LEN);
        if (!ha_client_entity_should_use_trigger_subscription(entity_id)) {
            continue;
        }
        cJSON *trigger = cJSON_CreateObject();
        if (trigger == NULL) {
            cJSON_Delete(root);
            free(entity_ids);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(trigger, "platform", "state");
        cJSON_AddStringToObject(trigger, "entity_id", entity_id);
        cJSON_AddItemToArray(triggers, trigger);
    }

    cJSON_AddItemToObject(root, "trigger", triggers);
    esp_err_t err = ha_client_send_json(root);
    cJSON_Delete(root);
    free(entity_ids);

    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    s_client.trigger_sub_req_id = req_id;
    s_client.sub_state_via_trigger = true;
    s_client.sub_state_via_entities = false;
    s_client.entities_sub_req_id = 0;
    xSemaphoreGive(s_client.mutex);
    ESP_LOGI(TAG_HA_CLIENT, "Subscribed to layout state changes via trigger (%u entities, skipped=%u)",
        (unsigned)eligible_count, (unsigned)skipped_count);
    return ESP_OK;
}

static esp_err_t ha_client_send_subscribe_state_changed(void)
{
    if (HA_USE_TRIGGER_SUBSCRIPTION) {
        esp_err_t trigger_err = ha_client_send_subscribe_layout_state_trigger();
        if (trigger_err == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG_HA_CLIENT, "Trigger subscribe failed (%s), falling back to global state_changed",
            esp_err_to_name(trigger_err));
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "id", (double)ha_client_next_message_id());
    cJSON_AddStringToObject(root, "type", "subscribe_events");
    cJSON_AddStringToObject(root, "event_type", "state_changed");
    esp_err_t err = ha_client_send_json(root);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_HA_CLIENT, "Failed to subscribe to events");
    } else {
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        s_client.sub_state_via_trigger = false;
        s_client.trigger_sub_req_id = 0;
        s_client.sub_state_via_entities = false;
        s_client.entities_sub_req_id = 0;
        s_client.entities_sub_target_count = 0;
        s_client.entities_sub_sent_count = 0;
        s_client.entities_sub_seen_count = 0;
        s_client.next_entities_subscribe_unix_ms = 0;
        ha_client_clear_entities_sub_buffers();
        xSemaphoreGive(s_client.mutex);
        ESP_LOGI(TAG_HA_CLIENT, "Subscribed to global state_changed events");
    }
    cJSON_Delete(root);
    return err;
}

static esp_err_t ha_client_send_ping(uint32_t *out_ping_id)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint32_t ping_id = ha_client_next_message_id();
    cJSON_AddNumberToObject(root, "id", (double)ping_id);
    cJSON_AddStringToObject(root, "type", "ping");
    esp_err_t err = ha_client_send_json(root);
    cJSON_Delete(root);
    if (err == ESP_OK && out_ping_id != NULL) {
        *out_ping_id = ping_id;
    }
    return err;
}

static esp_err_t ha_client_send_pong(uint32_t pong_id)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "id", (double)pong_id);
    cJSON_AddStringToObject(root, "type", "pong");
    esp_err_t err = ha_client_send_json(root);
    cJSON_Delete(root);
    return err;
}

static bool ha_client_import_state_object(cJSON *state_obj)
{
    if (!cJSON_IsObject(state_obj)) {
        return false;
    }

    cJSON *entity_id = cJSON_GetObjectItemCaseSensitive(state_obj, "entity_id");
    cJSON *state = cJSON_GetObjectItemCaseSensitive(state_obj, "state");
    cJSON *attributes = cJSON_GetObjectItemCaseSensitive(state_obj, "attributes");

    if (!cJSON_IsString(entity_id) || entity_id->valuestring == NULL || !cJSON_IsString(state) ||
        state->valuestring == NULL) {
        return false;
    }

    ha_state_t model_state = {0};
    snprintf(model_state.entity_id, sizeof(model_state.entity_id), "%s", entity_id->valuestring);
    snprintf(model_state.state, sizeof(model_state.state), "%s", state->valuestring);
    model_state.last_changed_unix_ms = esp_timer_get_time() / 1000;
    bool weather_missing_forecast = false;
    bool is_media_player = ha_client_entity_is_media_player(model_state.entity_id);
    ha_state_t previous_compact_state = {0};
    bool has_previous_compact_state = false;
    if (is_media_player) {
        has_previous_compact_state = ha_model_get_state(model_state.entity_id, &previous_compact_state);
    }

    if (cJSON_IsObject(attributes)) {
        bool serialized = false;
        if (ha_client_entity_is_weather(model_state.entity_id)) {
            serialized = ha_client_serialize_weather_attrs_compact(
                attributes, model_state.attributes_json, sizeof(model_state.attributes_json));
            bool weather_has_forecast = serialized && ha_client_weather_attrs_has_forecast_json(model_state.attributes_json);
            if (serialized && !weather_has_forecast) {
                ha_state_t previous_weather_state = {0};
                if (ha_model_get_state(model_state.entity_id, &previous_weather_state)) {
                    cJSON *previous_forecast =
                        ha_client_extract_compact_forecast_from_attrs_json(previous_weather_state.attributes_json);
                    if (previous_forecast != NULL) {
                        if (!ha_client_append_compact_forecast_to_attrs_json(
                                model_state.attributes_json, sizeof(model_state.attributes_json), previous_forecast)) {
                            ESP_LOGW(TAG_HA_CLIENT, "Failed to preserve previous forecast for %s", model_state.entity_id);
                        }
                    }
                }
                weather_has_forecast = ha_client_weather_attrs_has_forecast_json(model_state.attributes_json);
            }
            if (serialized && !weather_has_forecast) {
                weather_missing_forecast = true;
            }
        } else if (ha_client_entity_is_light(model_state.entity_id)) {
            serialized = ha_client_serialize_light_attrs_compact(
                attributes, model_state.attributes_json, sizeof(model_state.attributes_json));
            if (!serialized) {
                snprintf(model_state.attributes_json, sizeof(model_state.attributes_json), "{}");
                serialized = true;
            }
        } else if (ha_client_entity_is_climate(model_state.entity_id)) {
            serialized = ha_client_serialize_climate_attrs_compact(
                attributes, model_state.attributes_json, sizeof(model_state.attributes_json));
            if (!serialized) {
                snprintf(model_state.attributes_json, sizeof(model_state.attributes_json), "{}");
                serialized = true;
            }
        } else if (ha_client_entity_is_media_player(model_state.entity_id)) {
            serialized = ha_client_serialize_media_player_attrs_compact(
                attributes, model_state.attributes_json, sizeof(model_state.attributes_json));
            if (!serialized) {
                snprintf(model_state.attributes_json, sizeof(model_state.attributes_json), "{}");
                serialized = true;
            }
        }

        if (!serialized) {
            char *attr_json = cJSON_PrintUnformatted(attributes);
            if (attr_json != NULL) {
                int written = snprintf(model_state.attributes_json, sizeof(model_state.attributes_json), "%s", attr_json);
                if (written >= (int)sizeof(model_state.attributes_json)) {
                    ESP_LOGW(TAG_HA_CLIENT, "attributes_json truncated for %s (%d > %u bytes)",
                        model_state.entity_id, written, (unsigned)(sizeof(model_state.attributes_json) - 1U));
                }
                cJSON_free(attr_json);
            }
        }
    } else {
        snprintf(model_state.attributes_json, sizeof(model_state.attributes_json), "{}");
    }

    bool media_player_compact_changed = true;
    if (is_media_player && has_previous_compact_state) {
        media_player_compact_changed =
            (strncmp(previous_compact_state.state, model_state.state, sizeof(model_state.state)) != 0) ||
            (strncmp(previous_compact_state.attributes_json, model_state.attributes_json,
                sizeof(model_state.attributes_json)) != 0);
        if (!media_player_compact_changed) {
            model_state.last_changed_unix_ms = previous_compact_state.last_changed_unix_ms;
        }
    }

    ha_model_upsert_state(&model_state);
    if (weather_missing_forecast && s_client.mutex != NULL) {
        bool scheduled_retry = false;
        int64_t now_ms = ha_client_now_ms();
        int64_t ready_ms = now_ms;
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        bool allow_priority_sync = s_client.layout_needs_weather_forecast && !s_client.rest_enabled;
        if (allow_priority_sync && now_ms >= s_client.next_weather_forecast_retry_unix_ms) {
            if (s_client.ws_last_connected_unix_ms > 0) {
                int64_t grace_until = s_client.ws_last_connected_unix_ms + HA_WS_WEATHER_PRIORITY_GRACE_MS;
                if (ready_ms < grace_until) {
                    ready_ms = grace_until;
                }
            }
            ha_client_priority_sync_queue_push_locked(model_state.entity_id);
            if (s_client.next_priority_sync_unix_ms == 0 || s_client.next_priority_sync_unix_ms > ready_ms) {
                s_client.next_priority_sync_unix_ms = ready_ms;
            }
            s_client.next_weather_forecast_retry_unix_ms = now_ms + HA_WEATHER_FORECAST_RETRY_MIN_MS;
            scheduled_retry = true;
        }
        xSemaphoreGive(s_client.mutex);
        if (scheduled_retry) {
            int64_t delay_ms = ready_ms - now_ms;
            if (delay_ms < 0) {
                delay_ms = 0;
            }
            ESP_LOGI(TAG_HA_CLIENT, "Queued missing weather forecast retry for %s in %" PRId64 " ms",
                model_state.entity_id, delay_ms);
        } else {
            ESP_LOGD(TAG_HA_CLIENT, "Weather forecast retry deferred for %s", model_state.entity_id);
        }
    }

    ha_entity_info_t entity = {0};
    safe_copy_cstr(entity.id, sizeof(entity.id), model_state.entity_id);
    safe_copy_cstr(entity.name, sizeof(entity.name), model_state.entity_id);
    const char *dot = strchr(model_state.entity_id, '.');
    if (dot != NULL) {
        size_t domain_len = (size_t)(dot - model_state.entity_id);
        if (domain_len >= sizeof(entity.domain)) {
            domain_len = sizeof(entity.domain) - 1U;
        }
        memcpy(entity.domain, model_state.entity_id, domain_len);
        entity.domain[domain_len] = '\0';
    } else {
        snprintf(entity.domain, sizeof(entity.domain), "unknown");
    }

    if (cJSON_IsObject(attributes)) {
        cJSON *friendly_name = cJSON_GetObjectItemCaseSensitive(attributes, "friendly_name");
        cJSON *unit = cJSON_GetObjectItemCaseSensitive(attributes, "unit_of_measurement");
        cJSON *device_class = cJSON_GetObjectItemCaseSensitive(attributes, "device_class");
        cJSON *icon = cJSON_GetObjectItemCaseSensitive(attributes, "icon");
        cJSON *supported_features = cJSON_GetObjectItemCaseSensitive(attributes, "supported_features");
        if (cJSON_IsString(friendly_name) && friendly_name->valuestring != NULL) {
            snprintf(entity.name, sizeof(entity.name), "%s", friendly_name->valuestring);
        }
        if (cJSON_IsString(unit) && unit->valuestring != NULL) {
            snprintf(entity.unit, sizeof(entity.unit), "%s", unit->valuestring);
        }
        if (cJSON_IsString(device_class) && device_class->valuestring != NULL) {
            snprintf(entity.device_class, sizeof(entity.device_class), "%s", device_class->valuestring);
        }
        if (cJSON_IsString(icon) && icon->valuestring != NULL) {
            snprintf(entity.icon, sizeof(entity.icon), "%s", icon->valuestring);
        }
        if (cJSON_IsNumber(supported_features)) {
            entity.supported_features = (uint32_t)supported_features->valuedouble;
        }
    }
    ha_light_capabilities_t light_caps = {0};
    ha_light_capabilities_from_state(&model_state, &light_caps);
    entity.supports_dimming = light_caps.can_dim;
    entity.supports_color = light_caps.can_color;
    entity.supports_color_temp = light_caps.can_color_temp;
    ha_model_upsert_entity(&entity);
    return (!is_media_player) || media_player_compact_changed || !has_previous_compact_state;
}

static bool ha_client_import_ws_entity_state(const char *entity_id, const char *state_value, cJSON *attrs_obj)
{
    if (entity_id == NULL || entity_id[0] == '\0' || state_value == NULL) {
        return false;
    }

    cJSON *state_obj = cJSON_CreateObject();
    if (state_obj == NULL) {
        return false;
    }

    cJSON_AddStringToObject(state_obj, "entity_id", entity_id);
    cJSON_AddStringToObject(state_obj, "state", state_value);
    if (cJSON_IsObject(attrs_obj)) {
        cJSON *attrs_copy = cJSON_Duplicate(attrs_obj, true);
        if (attrs_copy != NULL) {
            cJSON_AddItemToObject(state_obj, "attributes", attrs_copy);
        } else {
            cJSON_AddItemToObject(state_obj, "attributes", cJSON_CreateObject());
        }
    } else {
        cJSON_AddItemToObject(state_obj, "attributes", cJSON_CreateObject());
    }

    bool changed = ha_client_import_state_object(state_obj);
    cJSON_Delete(state_obj);
    return changed;
}

static bool ha_client_entities_sub_req_known_locked(uint32_t req_id)
{
    if (req_id == 0U || s_client.entities_sub_req_ids == NULL) {
        return false;
    }
    for (uint16_t i = 0; i < s_client.entities_sub_sent_count && i < HA_WS_ENTITIES_SUB_MAX; i++) {
        if (s_client.entities_sub_req_ids[i] == req_id) {
            return true;
        }
    }
    return false;
}

static void ha_client_mark_entities_seen(const char *entity_id)
{
    if (entity_id == NULL || entity_id[0] == '\0' || s_client.mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    bool is_target = false;
    for (uint16_t i = 0; i < s_client.entities_sub_target_count && i < HA_WS_ENTITIES_SUB_MAX; i++) {
        const char *target = ha_client_entities_sub_target_at(i);
        if (target != NULL && strncmp(target, entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            is_target = true;
            break;
        }
    }
    if (!is_target) {
        xSemaphoreGive(s_client.mutex);
        return;
    }

    for (uint16_t i = 0; i < s_client.entities_sub_seen_count && i < HA_WS_ENTITIES_SUB_MAX; i++) {
        const char *seen = ha_client_entities_sub_seen_at(i);
        if (seen != NULL && strncmp(seen, entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            xSemaphoreGive(s_client.mutex);
            return;
        }
    }

    if (s_client.entities_sub_seen_count < HA_WS_ENTITIES_SUB_MAX) {
        char *seen_slot = ha_client_entities_sub_seen_at(s_client.entities_sub_seen_count);
        if (seen_slot != NULL) {
            safe_copy_cstr(seen_slot, APP_MAX_ENTITY_ID_LEN, entity_id);
            s_client.entities_sub_seen_count++;
        }
    }
    xSemaphoreGive(s_client.mutex);
}

static uint32_t ha_client_import_ws_entities_added(cJSON *added_map)
{
    if (!cJSON_IsObject(added_map)) {
        return 0;
    }

    uint32_t imported = 0;
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, added_map)
    {
        if (entry->string == NULL || !cJSON_IsObject(entry)) {
            continue;
        }
        cJSON *state_item = cJSON_GetObjectItemCaseSensitive(entry, "s");
        cJSON *attrs_item = cJSON_GetObjectItemCaseSensitive(entry, "a");
        const char *state_value = cJSON_IsString(state_item) && state_item->valuestring != NULL
            ? state_item->valuestring
            : "unknown";
        bool state_changed = ha_client_import_ws_entity_state(entry->string, state_value, attrs_item);
        ha_client_mark_entities_seen(entry->string);
        if (!state_changed) {
            continue;
        }
        ha_client_trace_service_state_changed(entry->string, state_value);
        ha_client_publish_event(EV_HA_STATE_CHANGED, entry->string);
        imported++;
    }
    return imported;
}

static void ha_client_apply_ws_attr_plus(cJSON *attrs_obj, cJSON *plus_attrs)
{
    if (!cJSON_IsObject(attrs_obj) || !cJSON_IsObject(plus_attrs)) {
        return;
    }
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, plus_attrs)
    {
        if (item->string == NULL) {
            continue;
        }
        cJSON_DeleteItemFromObjectCaseSensitive(attrs_obj, item->string);
        cJSON_AddItemToObject(attrs_obj, item->string, cJSON_Duplicate(item, true));
    }
}

static void ha_client_apply_ws_attr_minus(cJSON *attrs_obj, cJSON *minus_obj)
{
    if (!cJSON_IsObject(attrs_obj) || !cJSON_IsObject(minus_obj)) {
        return;
    }
    cJSON *minus_attrs = cJSON_GetObjectItemCaseSensitive(minus_obj, "a");
    if (!cJSON_IsArray(minus_attrs)) {
        return;
    }
    cJSON *key = NULL;
    cJSON_ArrayForEach(key, minus_attrs)
    {
        if (cJSON_IsString(key) && key->valuestring != NULL) {
            cJSON_DeleteItemFromObjectCaseSensitive(attrs_obj, key->valuestring);
        }
    }
}

static uint32_t ha_client_import_ws_entities_changed(cJSON *changed_map)
{
    if (!cJSON_IsObject(changed_map)) {
        return 0;
    }

    uint32_t updated = 0;
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, changed_map)
    {
        if (entry->string == NULL || !cJSON_IsObject(entry)) {
            continue;
        }
        if (ha_client_entity_is_media_player(entry->string) &&
            ha_client_ws_media_player_change_can_skip(entry)) {
            ha_client_mark_entities_seen(entry->string);
            continue;
        }

        ha_state_t prev = {0};
        bool has_prev = ha_model_get_state(entry->string, &prev);
        char next_state[32] = "unknown";
        if (has_prev && prev.state[0] != '\0') {
            safe_copy_cstr(next_state, sizeof(next_state), prev.state);
        }

        cJSON *attrs_obj = NULL;
        if (has_prev && prev.attributes_json[0] != '\0') {
            attrs_obj = cJSON_Parse(prev.attributes_json);
        }
        if (!cJSON_IsObject(attrs_obj)) {
            cJSON_Delete(attrs_obj);
            attrs_obj = cJSON_CreateObject();
        }
        if (attrs_obj == NULL) {
            continue;
        }

        cJSON *plus_obj = cJSON_GetObjectItemCaseSensitive(entry, "+");
        if (cJSON_IsObject(plus_obj)) {
            cJSON *plus_state = cJSON_GetObjectItemCaseSensitive(plus_obj, "s");
            if (cJSON_IsString(plus_state) && plus_state->valuestring != NULL) {
                safe_copy_cstr(next_state, sizeof(next_state), plus_state->valuestring);
            }
            cJSON *plus_attrs = cJSON_GetObjectItemCaseSensitive(plus_obj, "a");
            ha_client_apply_ws_attr_plus(attrs_obj, plus_attrs);
        }

        cJSON *minus_obj = cJSON_GetObjectItemCaseSensitive(entry, "-");
        ha_client_apply_ws_attr_minus(attrs_obj, minus_obj);

        bool state_changed = ha_client_import_ws_entity_state(entry->string, next_state, attrs_obj);
        cJSON_Delete(attrs_obj);
        ha_client_mark_entities_seen(entry->string);
        if (!state_changed) {
            continue;
        }
        ha_client_trace_service_state_changed(entry->string, next_state);
        ha_client_publish_event(EV_HA_STATE_CHANGED, entry->string);
        updated++;
    }
    return updated;
}

static uint32_t ha_client_import_ws_entities_removed(cJSON *removed_list)
{
    if (!cJSON_IsArray(removed_list)) {
        return 0;
    }

    uint32_t removed = 0;
    cJSON *entity_id = NULL;
    cJSON_ArrayForEach(entity_id, removed_list)
    {
        if (!cJSON_IsString(entity_id) || entity_id->valuestring == NULL) {
            continue;
        }
        cJSON *attrs = cJSON_CreateObject();
        if (attrs == NULL) {
            continue;
        }
        bool state_changed = ha_client_import_ws_entity_state(entity_id->valuestring, "unavailable", attrs);
        cJSON_Delete(attrs);
        ha_client_mark_entities_seen(entity_id->valuestring);
        if (!state_changed) {
            continue;
        }
        ha_client_trace_service_state_changed(entity_id->valuestring, "unavailable");
        ha_client_publish_event(EV_HA_STATE_CHANGED, entity_id->valuestring);
        removed++;
    }
    return removed;
}

static bool ha_client_light_discovery_handle_result(uint32_t msg_id, cJSON *root, cJSON *success_item)
{
    ha_light_discovery_phase_t phase = HA_LIGHT_DISCOVERY_PHASE_NONE;
    bool is_light_discovery = false;

    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    if (s_client.light_discovery_inflight && s_client.light_discovery_req_id == msg_id) {
        is_light_discovery = true;
        phase = s_client.light_discovery_phase;
        /* Heavy response fully received: arm gate cooldown before clearing flag. */
        ha_client_ws_send_gate_mark_heavy_done_locked(ha_client_now_ms());
        s_client.light_discovery_inflight = false;
        s_client.light_discovery_req_id = 0;
    }
    xSemaphoreGive(s_client.mutex);

    if (!is_light_discovery) {
        return false;
    }

    const bool success = cJSON_IsBool(success_item) && cJSON_IsTrue(success_item);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    int64_t now_ms = ha_client_now_ms();

    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    if (success) {
        switch (phase) {
        case HA_LIGHT_DISCOVERY_PHASE_ENTITY_DISPLAY:
            ha_client_light_discovery_parse_display_entities_locked(result);
            s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_AREAS;
            s_client.light_discovery_requested = true;
            break;
        case HA_LIGHT_DISCOVERY_PHASE_ENTITY_FULL:
            ha_client_light_discovery_parse_full_entities_locked(result);
            s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_AREAS;
            s_client.light_discovery_requested = true;
            break;
        case HA_LIGHT_DISCOVERY_PHASE_AREAS:
            ha_client_light_discovery_parse_areas_locked(result);
            s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_DEVICES;
            s_client.light_discovery_requested = true;
            break;
        case HA_LIGHT_DISCOVERY_PHASE_DEVICES:
            ha_client_light_discovery_parse_devices_locked(result);
            ha_client_light_discovery_finish_locked(now_ms);
            break;
        default:
            ha_client_light_discovery_abort_locked();
            break;
        }
    } else if (phase == HA_LIGHT_DISCOVERY_PHASE_ENTITY_DISPLAY) {
        ESP_LOGW(TAG_HA_CLIENT,
            "HA list_for_display light discovery failed; retrying with full entity registry");
        s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_ENTITY_FULL;
        s_client.light_discovery_requested = true;
    } else if (phase == HA_LIGHT_DISCOVERY_PHASE_AREAS) {
        ESP_LOGW(TAG_HA_CLIENT, "HA area registry discovery failed; continuing without room names");
        s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_DEVICES;
        s_client.light_discovery_requested = true;
    } else if (phase == HA_LIGHT_DISCOVERY_PHASE_DEVICES) {
        ESP_LOGW(TAG_HA_CLIENT, "HA device registry discovery failed; finishing with entity-assigned rooms only");
        ha_client_light_discovery_finish_locked(now_ms);
    } else {
        ESP_LOGW(TAG_HA_CLIENT, "HA light entity discovery failed");
        ha_client_light_discovery_abort_locked();
    }
    xSemaphoreGive(s_client.mutex);

    return true;
}

static void ha_client_handle_result_message(cJSON *root)
{
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (!cJSON_IsNumber(id)) {
        return;
    }
    uint32_t msg_id = (uint32_t)id->valuedouble;

    cJSON *success_item = cJSON_GetObjectItemCaseSensitive(root, "success");
    if (cJSON_IsBool(success_item)) {
        const char *error_text = NULL;
        cJSON *error_obj = cJSON_GetObjectItemCaseSensitive(root, "error");
        if (cJSON_IsObject(error_obj)) {
            cJSON *message = cJSON_GetObjectItemCaseSensitive(error_obj, "message");
            if (cJSON_IsString(message) && message->valuestring != NULL) {
                error_text = message->valuestring;
            }
        }
        ha_client_trace_service_result(msg_id, cJSON_IsTrue(success_item), error_text);
    }

    if (ha_client_light_discovery_handle_result(msg_id, root, success_item)) {
        return;
    }

    bool is_get_states = false;
    bool is_entities_sub = false;
    bool entities_sub_failed = false;
    bool is_weather_ws_req = false;
    bool is_energy_prefs_req = false;
    bool is_energy_stats_req = false;
    char weather_entity_id[APP_MAX_ENTITY_ID_LEN] = {0};
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    is_get_states = (msg_id == s_client.get_states_req_id);
    is_entities_sub = s_client.sub_state_via_entities && ha_client_entities_sub_req_known_locked(msg_id);
    if (s_client.weather_ws_req_inflight && msg_id == s_client.weather_ws_req_id) {
        is_weather_ws_req = true;
        safe_copy_cstr(weather_entity_id, sizeof(weather_entity_id), s_client.weather_ws_req_entity_id);
        /* Heavy response fully received: arm cooldown before clearing flag. */
        ha_client_ws_send_gate_mark_heavy_done_locked(ha_client_now_ms());
        s_client.weather_ws_req_inflight = false;
        s_client.weather_ws_req_id = 0;
        s_client.weather_ws_req_entity_id[0] = '\0';
    }
    if (s_client.energy_prefs_req_inflight && msg_id == s_client.energy_prefs_req_id) {
        is_energy_prefs_req = true;
        ha_client_ws_send_gate_mark_heavy_done_locked(ha_client_now_ms());
        s_client.energy_prefs_req_inflight = false;
        s_client.energy_prefs_req_id = 0;
    }
    if (s_client.energy_stats_req_inflight && msg_id == s_client.energy_stats_req_id) {
        is_energy_stats_req = true;
        ha_client_ws_send_gate_mark_heavy_done_locked(ha_client_now_ms());
        s_client.energy_stats_req_inflight = false;
        s_client.energy_stats_req_id = 0;
    }
    if (is_entities_sub && cJSON_IsBool(success_item) && !cJSON_IsTrue(success_item)) {
        s_client.sub_state_via_entities = false;
        s_client.entities_sub_req_id = 0;
        s_client.ws_entities_subscribe_supported = false;
        s_client.entities_sub_target_count = 0;
        s_client.entities_sub_sent_count = 0;
        s_client.entities_sub_seen_count = 0;
        ha_client_clear_entities_sub_buffers();
        s_client.pending_subscribe = APP_HA_SUBSCRIBE_STATE_CHANGED;
        entities_sub_failed = true;
    }
    if (is_get_states && cJSON_IsBool(success_item) && !cJSON_IsTrue(success_item)) {
        s_client.pending_get_states = true;
        s_client.get_states_req_id = 0;
        int64_t retry_at = ha_client_now_ms() + HA_INITIAL_LAYOUT_SYNC_RETRY_INTERVAL_MS;
        if (s_client.ws_get_states_block_until_unix_ms > retry_at) {
            retry_at = s_client.ws_get_states_block_until_unix_ms;
        }
        s_client.next_initial_layout_sync_unix_ms = retry_at;
    }
    xSemaphoreGive(s_client.mutex);

    if (is_entities_sub) {
        if (entities_sub_failed) {
            ESP_LOGW(TAG_HA_CLIENT,
                "WS subscribe_entities was rejected by HA, switching to trigger/global state_changed fallback");
        } else if (cJSON_IsBool(success_item) && cJSON_IsTrue(success_item)) {
            ESP_LOGI(TAG_HA_CLIENT, "WS subscribe_entities accepted");
        }
    }

    if (is_weather_ws_req) {
        bool updated = false;
        bool forecast_found = false;
        bool merge_failed = false;
        bool state_missing = false;
        if (cJSON_IsBool(success_item) && cJSON_IsTrue(success_item)) {
            cJSON *result_obj = cJSON_GetObjectItemCaseSensitive(root, "result");
            cJSON *compact_forecast = ha_client_find_compact_weather_forecast(result_obj, weather_entity_id);
            if (compact_forecast != NULL) {
                forecast_found = true;
                ha_state_t state = {0};
                if (ha_model_get_state(weather_entity_id, &state)) {
                    if (state.attributes_json[0] == '\0') {
                        snprintf(state.attributes_json, sizeof(state.attributes_json), "{}");
                    }
                    if (ha_client_append_compact_forecast_to_attrs_json(
                            state.attributes_json, sizeof(state.attributes_json), compact_forecast)) {
                        state.last_changed_unix_ms = esp_timer_get_time() / 1000;
                        ha_model_upsert_state(&state);
                        ha_client_publish_event(EV_HA_STATE_CHANGED, weather_entity_id);
                        updated = true;
                    } else {
                        merge_failed = true;
                    }
                } else {
                    state_missing = true;
                    cJSON_Delete(compact_forecast);
                }
            }
        }

        if (updated) {
            ESP_LOGI(TAG_HA_CLIENT, "WS weather forecast updated for %s", weather_entity_id);
        } else if (cJSON_IsBool(success_item) && !cJSON_IsTrue(success_item)) {
            ESP_LOGW(TAG_HA_CLIENT, "WS weather forecast request failed for %s", weather_entity_id);
        } else if (merge_failed) {
            ESP_LOGW(TAG_HA_CLIENT, "WS weather forecast merge failed for %s (attrs buffer too small or invalid)",
                weather_entity_id);
        } else if (!forecast_found) {
            ESP_LOGW(TAG_HA_CLIENT, "WS weather forecast response had no usable forecast for %s", weather_entity_id);
        } else if (state_missing) {
            ESP_LOGD(TAG_HA_CLIENT, "WS weather forecast arrived before state model existed for %s", weather_entity_id);
        } else {
            ESP_LOGD(TAG_HA_CLIENT, "WS weather forecast response was ignored for %s", weather_entity_id);
        }
    }

    if (is_energy_prefs_req) {
        if (cJSON_IsBool(success_item) && cJSON_IsTrue(success_item)) {
            cJSON *result_obj = cJSON_GetObjectItemCaseSensitive(root, "result");
            uint8_t ref_count = 0;
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            ha_client_energy_parse_prefs_locked(result_obj);
            ref_count = s_client.energy_stat_ref_count;
            s_client.pending_energy_stats = (ref_count > 0);
            s_client.energy_stats_batch_offset = 0;
            memset(&s_client.energy_staging, 0, sizeof(s_client.energy_staging));
            if (ref_count == 0) {
                s_client.next_energy_sync_unix_ms = ha_client_now_ms() + HA_ENERGY_SYNC_RETRY_MS;
            } else {
                /* Minimal spacing after prefs before we start the stats batch
                 * sequence.  The central WS/TLS gate (HA_WS_HEAVY_MIN_GAP_MS)
                 * already prevents overlap with weather forecast or any other
                 * heavy WS response, so the old 20 s safety window is no
                 * longer needed — 2 s is enough for the prefs payload to be
                 * fully parsed + freed before the first stats batch goes out. */
                s_client.next_energy_sync_unix_ms = ha_client_now_ms() + 2000;
            }
            xSemaphoreGive(s_client.mutex);
            if (ref_count > 0) {
                ESP_LOGI(TAG_HA_CLIENT, "HA energy preferences loaded (%u statistics)", (unsigned)ref_count);
            } else {
                ha_energy_model_reset();
                ha_client_publish_event(EV_HA_ENERGY_CHANGED, NULL);
                ESP_LOGW(TAG_HA_CLIENT, "HA energy preferences contain no supported statistics");
            }
        } else {
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            s_client.pending_energy_prefs = true;
            s_client.next_energy_sync_unix_ms = ha_client_now_ms() + HA_ENERGY_SYNC_RETRY_MS;
            xSemaphoreGive(s_client.mutex);
            ESP_LOGW(TAG_HA_CLIENT, "HA energy preferences request failed");
        }
        return;
    }

    if (is_energy_stats_req) {
        if (cJSON_IsBool(success_item) && cJSON_IsTrue(success_item)) {
            cJSON *result_obj = cJSON_GetObjectItemCaseSensitive(root, "result");
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            uint8_t next_offset = s_client.energy_stats_batch_offset + HA_ENERGY_STATS_BATCH_SIZE;
            bool is_final = next_offset >= s_client.energy_stat_ref_count;
            xSemaphoreGive(s_client.mutex);
            ha_client_energy_handle_stats_result(result_obj, is_final);
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            if (!is_final) {
                /* More batches to fetch */
                s_client.energy_stats_batch_offset = next_offset;
                s_client.pending_energy_stats = true;
                s_client.next_energy_sync_unix_ms = ha_client_now_ms() + HA_ENERGY_STATS_BATCH_DELAY_MS;
                ESP_LOGI(TAG_HA_CLIENT, "HA energy statistics batch done (offset %u/%u), next batch in %u ms",
                    (unsigned)next_offset, (unsigned)s_client.energy_stat_ref_count,
                    (unsigned)HA_ENERGY_STATS_BATCH_DELAY_MS);
            } else {
                /* All batches complete */
                s_client.energy_stats_batch_offset = 0;
                memset(&s_client.energy_staging, 0, sizeof(s_client.energy_staging));
                s_client.next_energy_sync_unix_ms = ha_client_now_ms() + HA_ENERGY_SYNC_INTERVAL_MS;
                ESP_LOGI(TAG_HA_CLIENT, "HA energy statistics updated (%u refs)", (unsigned)s_client.energy_stat_ref_count);
            }
            xSemaphoreGive(s_client.mutex);
        } else {
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            s_client.pending_energy_prefs = true;
            s_client.next_energy_sync_unix_ms = ha_client_now_ms() + HA_ENERGY_SYNC_RETRY_MS;
            xSemaphoreGive(s_client.mutex);
            ESP_LOGW(TAG_HA_CLIENT, "HA energy statistics request failed");
        }
        return;
    }

    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!cJSON_IsArray(result)) {
        return;
    }

    if (!is_get_states) {
        return;
    }
    if (!(cJSON_IsBool(success_item) && cJSON_IsTrue(success_item))) {
        ESP_LOGW(TAG_HA_CLIENT, "WS get_states returned non-success result");
        return;
    }

    int n = cJSON_GetArraySize(result);
    int imported = 0;
    size_t layout_entity_count = 0;
    bool filtered_to_layout = false;
    size_t max_entities = (size_t)HA_LAYOUT_ENTITY_MAX;
    char *layout_entity_ids = calloc(max_entities, APP_MAX_ENTITY_ID_LEN);
    if (layout_entity_ids != NULL) {
        bool need_weather_forecast = false;
        layout_entity_count =
            ha_client_collect_layout_entity_ids(layout_entity_ids, max_entities, &need_weather_forecast);
        filtered_to_layout = (layout_entity_count > 0);
    }

    for (int i = 0; i < n; i++) {
        cJSON *state_obj = cJSON_GetArrayItem(result, i);
        if (!cJSON_IsObject(state_obj)) {
            continue;
        }
        if (filtered_to_layout) {
            cJSON *entity_id_item = cJSON_GetObjectItemCaseSensitive(state_obj, "entity_id");
            if (!cJSON_IsString(entity_id_item) || entity_id_item->valuestring == NULL ||
                !ha_client_entity_id_in_list(layout_entity_ids, layout_entity_count, entity_id_item->valuestring)) {
                continue;
            }
        }
        (void)ha_client_import_state_object(state_obj);
        imported++;
    }
    free(layout_entity_ids);

    bool queue_weather_bootstrap = false;
    int64_t now_ms = ha_client_now_ms();
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    s_client.pending_get_states = false;
    s_client.get_states_req_id = 0;
    s_client.pending_initial_layout_sync = false;
    s_client.initial_layout_sync_done = true;
    if (!s_client.rest_enabled && s_client.layout_needs_weather_forecast) {
        queue_weather_bootstrap = true;
    }
    xSemaphoreGive(s_client.mutex);
    if (queue_weather_bootstrap) {
        ha_client_queue_weather_priority_sync_from_layout(now_ms);
    }
    if (filtered_to_layout) {
        ESP_LOGI(TAG_HA_CLIENT, "Imported initial states via WS: %d/%d (layout=%u)", imported, n,
            (unsigned)layout_entity_count);
    } else {
        ESP_LOGI(TAG_HA_CLIENT, "Imported initial states via WS: %d/%d", imported, n);
    }
    /* Refresh UI/runtime once the initial snapshot is in the model.
     * Otherwise widgets may stay "unavailable" until the next state_changed event. */
    ha_client_publish_event(EV_HA_CONNECTED, NULL);
}

static void ha_client_handle_event_message(cJSON *root)
{
    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    uint32_t msg_id = cJSON_IsNumber(id_item) ? (uint32_t)id_item->valuedouble : 0;
    bool is_trigger_event = false;
    bool is_entities_event = false;
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    is_trigger_event = s_client.sub_state_via_trigger && (msg_id == s_client.trigger_sub_req_id);
    is_entities_event = s_client.sub_state_via_entities && ha_client_entities_sub_req_known_locked(msg_id);
    xSemaphoreGive(s_client.mutex);

    cJSON *event = cJSON_GetObjectItemCaseSensitive(root, "event");
    if (!cJSON_IsObject(event)) {
        return;
    }

    if (is_entities_event) {
        cJSON *added_map = cJSON_GetObjectItemCaseSensitive(event, "a");
        cJSON *changed_map = cJSON_GetObjectItemCaseSensitive(event, "c");
        cJSON *removed_list = cJSON_GetObjectItemCaseSensitive(event, "r");

        uint32_t added_count = ha_client_import_ws_entities_added(added_map);
        uint32_t changed_count = ha_client_import_ws_entities_changed(changed_map);
        uint32_t removed_count = ha_client_import_ws_entities_removed(removed_list);

        bool mark_initial_done = false;
        bool queue_weather_bootstrap = false;
        uint16_t seen_count = 0;
        uint16_t target_count = 0;
        int64_t now_ms = ha_client_now_ms();
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        seen_count = s_client.entities_sub_seen_count;
        target_count = s_client.entities_sub_target_count;
        if (!s_client.initial_layout_sync_done && target_count > 0 &&
            seen_count >= target_count) {
            s_client.pending_initial_layout_sync = false;
            s_client.pending_get_states = false;
            s_client.get_states_req_id = 0;
            s_client.initial_layout_sync_done = true;
            s_client.initial_layout_sync_imported = seen_count;
            if (!s_client.rest_enabled && s_client.layout_needs_weather_forecast) {
                queue_weather_bootstrap = true;
            }
            mark_initial_done = true;
        }
        xSemaphoreGive(s_client.mutex);

        if (queue_weather_bootstrap) {
            ha_client_queue_weather_priority_sync_from_layout(now_ms);
        }
        if (mark_initial_done) {
            ESP_LOGI(TAG_HA_CLIENT, "Initial layout state sync via WS entities stream: imported %u/%u entities",
                (unsigned)seen_count, (unsigned)target_count);
            ha_client_publish_event(EV_HA_CONNECTED, NULL);
        } else if ((added_count + changed_count + removed_count) > 0U) {
            ESP_LOGD(TAG_HA_CLIENT, "WS entities stream update: +%u ~%u -%u",
                (unsigned)added_count, (unsigned)changed_count, (unsigned)removed_count);
        }
        return;
    }

    if (is_trigger_event) {
        cJSON *variables = cJSON_GetObjectItemCaseSensitive(event, "variables");
        cJSON *trigger = cJSON_IsObject(variables) ? cJSON_GetObjectItemCaseSensitive(variables, "trigger") : NULL;
        if (!cJSON_IsObject(trigger)) {
            return;
        }

        cJSON *to_state = cJSON_GetObjectItemCaseSensitive(trigger, "to_state");
        cJSON *entity_id = cJSON_GetObjectItemCaseSensitive(trigger, "entity_id");
        const char *state_value = NULL;
        bool state_changed = false;
        if (cJSON_IsObject(to_state)) {
            state_changed = ha_client_import_state_object(to_state);
            cJSON *state_item = cJSON_GetObjectItemCaseSensitive(to_state, "state");
            if (cJSON_IsString(state_item) && state_item->valuestring != NULL) {
                state_value = state_item->valuestring;
            }
        }
        if (state_changed && cJSON_IsString(entity_id) && entity_id->valuestring != NULL) {
            ha_client_trace_service_state_changed(entity_id->valuestring, state_value);
            ha_client_publish_event(EV_HA_STATE_CHANGED, entity_id->valuestring);
        }
        return;
    }

    cJSON *event_type = cJSON_GetObjectItemCaseSensitive(event, "event_type");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(event, "data");
    if (!cJSON_IsString(event_type) || event_type->valuestring == NULL || !cJSON_IsObject(data)) {
        return;
    }

    if (strcmp(event_type->valuestring, "state_changed") == 0) {
        cJSON *new_state = cJSON_GetObjectItemCaseSensitive(data, "new_state");
        cJSON *entity_id = cJSON_GetObjectItemCaseSensitive(data, "entity_id");
        const char *state_value = NULL;
        bool state_changed = false;
        if (cJSON_IsObject(new_state)) {
            state_changed = ha_client_import_state_object(new_state);
            cJSON *state_item = cJSON_GetObjectItemCaseSensitive(new_state, "state");
            if (cJSON_IsString(state_item) && state_item->valuestring != NULL) {
                state_value = state_item->valuestring;
            }
        }
        if (state_changed && cJSON_IsString(entity_id) && entity_id->valuestring != NULL) {
            ha_client_trace_service_state_changed(entity_id->valuestring, state_value);
            ha_client_publish_event(EV_HA_STATE_CHANGED, entity_id->valuestring);
        }
    }
}

static void ha_client_handle_text_message(const char *data, int len)
{
    if (data == NULL || len <= 0) {
        return;
    }

    cJSON *root = cJSON_ParseWithLength(data, (size_t)len);
    if (root == NULL) {
        return;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == NULL) {
        cJSON_Delete(root);
        return;
    }

    int64_t now_ms = ha_client_now_ms();
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    s_client.last_rx_unix_ms = now_ms;
    xSemaphoreGive(s_client.mutex);

    ESP_LOGD(TAG_HA_CLIENT, "HA message type=%s", type->valuestring);

    if (strcmp(type->valuestring, "auth_required") == 0) {
        ESP_LOGI(TAG_HA_CLIENT, "HA auth requested, sending token");
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        s_client.pending_send_auth = true;
        s_client.next_auth_retry_unix_ms = now_ms;
        xSemaphoreGive(s_client.mutex);
    } else if (strcmp(type->valuestring, "ping") == 0) {
        uint32_t ping_id = 0;
        cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
        if (cJSON_IsNumber(id)) {
            ping_id = (uint32_t)id->valuedouble;
        }
        ESP_LOGI(TAG_HA_CLIENT, "HA ping received, id=%" PRIu32, ping_id);
        if (ha_client_send_pong(ping_id) == ESP_OK) {
            ESP_LOGI(TAG_HA_CLIENT, "HA pong sent, id=%" PRIu32, ping_id);
        } else {
            ESP_LOGW(TAG_HA_CLIENT, "Immediate HA pong failed, queueing retry, id=%" PRIu32, ping_id);
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            s_client.pending_send_pong = true;
            s_client.pending_pong_id = ping_id;
            xSemaphoreGive(s_client.mutex);
        }
    } else if (strcmp(type->valuestring, "auth_ok") == 0) {
        ESP_LOGI(TAG_HA_CLIENT, "HA auth ok");
        bool rest_enabled = false;
        bool layout_needs_weather_forecast = false;
        bool schedule_initial_layout_sync = false;
        bool resume_initial_layout_sync = false;
        bool reconnect_ws_entities_resync = false;
        bool queue_weather_bootstrap = false;
        bool ws_entities_stream = false;
        bool layout_needs_ha_energy_now = ha_client_layout_needs_ha_energy();
        uint32_t initial_sync_progress = 0;
        uint32_t initial_sync_total = 0;
        int64_t ws_get_states_block_until = 0;
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        s_client.authenticated = true;
        s_client.published_disconnect = false;
        s_client.pending_subscribe = APP_HA_SUBSCRIBE_STATE_CHANGED;
        rest_enabled = s_client.rest_enabled;
        ws_entities_stream = (!rest_enabled && HA_USE_WS_ENTITIES_SUBSCRIPTION && s_client.ws_entities_subscribe_supported);
        layout_needs_weather_forecast = s_client.layout_needs_weather_forecast;
        s_client.pending_get_states = false;
        s_client.pending_send_auth = false;
        s_client.next_auth_retry_unix_ms = 0;
        s_client.ping_inflight = false;
        s_client.ping_inflight_id = 0;
        s_client.ping_sent_unix_ms = 0;
        s_client.last_rx_unix_ms = now_ms;
        s_client.ws_error_streak = 0;
        s_client.ping_timeout_strikes = 0;
        s_client.pending_force_wifi_recover = false;
        s_client.weather_ws_req_inflight = false;
        s_client.weather_ws_req_id = 0;
        s_client.weather_ws_req_entity_id[0] = '\0';
        s_client.layout_needs_ha_energy = layout_needs_ha_energy_now;
        s_client.pending_energy_prefs = layout_needs_ha_energy_now;
        s_client.pending_energy_stats = false;
        s_client.energy_prefs_req_inflight = false;
        s_client.energy_stats_req_inflight = false;
        s_client.energy_prefs_req_id = 0;
        s_client.energy_stats_req_id = 0;
        s_client.energy_stats_batch_offset = 0;
        memset(&s_client.energy_staging, 0, sizeof(s_client.energy_staging));
        /* Initial post-auth kickoff for energy prefs.  The central WS/TLS
         * gate (HA_WS_HEAVY_MIN_GAP_MS) now serializes against weather /
         * subscribes, so we only need a tiny cushion (~2 s) to let the WS
         * session fully settle before the first heavy request goes out. */
        s_client.next_energy_sync_unix_ms = layout_needs_ha_energy_now ? (now_ms + 2000) : 0;
        if (!layout_needs_ha_energy_now) {
            ha_client_energy_clear_refs_locked();
        }
        ws_get_states_block_until = s_client.ws_get_states_block_until_unix_ms;
        if (ws_entities_stream) {
            uint16_t target_count = ha_client_prepare_entities_resubscribe_locked(now_ms);
            if (APP_HA_FETCH_INITIAL_STATES && !s_client.initial_layout_sync_done) {
                s_client.pending_initial_layout_sync = false;
                s_client.pending_get_states = false;
                s_client.get_states_req_id = 0;
                s_client.next_initial_layout_sync_unix_ms = 0;
                if (target_count == 0) {
                    s_client.initial_layout_sync_done = true;
                    s_client.initial_layout_sync_imported = 0;
                }
                initial_sync_progress = s_client.entities_sub_seen_count;
                initial_sync_total = s_client.entities_sub_target_count;
                if (s_client.initial_layout_sync_index == 0 && s_client.initial_layout_sync_imported == 0) {
                    schedule_initial_layout_sync = true;
                } else {
                    resume_initial_layout_sync = true;
                }
            } else {
                s_client.pending_initial_layout_sync = false;
                s_client.pending_get_states = false;
                s_client.get_states_req_id = 0;
                s_client.next_initial_layout_sync_unix_ms = 0;
                reconnect_ws_entities_resync = APP_HA_SUBSCRIBE_STATE_CHANGED && (target_count > 0);
                initial_sync_total = s_client.entities_sub_target_count;
            }
        } else if (APP_HA_FETCH_INITIAL_STATES && !s_client.initial_layout_sync_done) {
            initial_sync_progress = s_client.initial_layout_sync_imported;
            initial_sync_total = s_client.initial_layout_sync_index;
            if (rest_enabled) {
                s_client.pending_initial_layout_sync = true;
                s_client.pending_get_states = false;
                s_client.next_initial_layout_sync_unix_ms = now_ms + ha_client_interval_initial_step_ms(s_client.bg_budget_level);
            } else {
                s_client.pending_initial_layout_sync = false;
                s_client.pending_get_states = false;
                s_client.get_states_req_id = 0;
                int64_t next_allowed = now_ms + HA_WS_GET_STATES_POST_SUBSCRIBE_DELAY_MS;
                if (s_client.ws_get_states_block_until_unix_ms > next_allowed) {
                    next_allowed = s_client.ws_get_states_block_until_unix_ms;
                }
                s_client.next_initial_layout_sync_unix_ms = next_allowed;
            }
            if (s_client.initial_layout_sync_index == 0 && s_client.initial_layout_sync_imported == 0) {
                schedule_initial_layout_sync = true;
            } else {
                resume_initial_layout_sync = true;
            }
        } else {
            s_client.pending_initial_layout_sync = false;
            s_client.pending_get_states = false;
        }
        layout_needs_weather_forecast = s_client.layout_needs_weather_forecast;
        s_client.next_periodic_layout_sync_unix_ms =
            rest_enabled ? (now_ms + ha_client_interval_periodic_step_ms(s_client.bg_budget_level)) : 0;
        s_client.next_priority_sync_unix_ms =
            rest_enabled ? now_ms : (now_ms + HA_WS_WEATHER_PRIORITY_GRACE_MS);
        if (!rest_enabled && layout_needs_weather_forecast &&
            (!APP_HA_FETCH_INITIAL_STATES || s_client.initial_layout_sync_done)) {
            queue_weather_bootstrap = true;
        }
        xSemaphoreGive(s_client.mutex);
        ha_client_publish_event(EV_HA_CONNECTED, NULL);
        if (schedule_initial_layout_sync) {
            if (rest_enabled) {
                ESP_LOGI(TAG_HA_CLIENT, "Initial targeted state sync scheduled (layout entities via REST)");
            } else if (ws_entities_stream) {
                ESP_LOGI(TAG_HA_CLIENT, "Initial targeted state sync scheduled via WS subscribe_entities (WS-only runtime)");
            } else {
                ESP_LOGI(TAG_HA_CLIENT, "Initial targeted state sync scheduled via WS get_states (WS-only runtime)");
            }
        } else if (resume_initial_layout_sync) {
            if (rest_enabled) {
                ESP_LOGI(TAG_HA_CLIENT, "Initial targeted state sync resumed (%u imported, cursor=%u)",
                    (unsigned)initial_sync_progress, (unsigned)initial_sync_total);
            } else if (ws_entities_stream) {
                ESP_LOGI(TAG_HA_CLIENT, "Initial targeted state sync resumed (%u imported, cursor=%u, WS-only runtime via subscribe_entities)",
                    (unsigned)initial_sync_progress, (unsigned)initial_sync_total);
            } else {
                ESP_LOGI(TAG_HA_CLIENT, "Initial targeted state sync resumed (%u imported, cursor=%u, WS-only runtime via get_states)",
                    (unsigned)initial_sync_progress, (unsigned)initial_sync_total);
            }
        } else if (reconnect_ws_entities_resync) {
            ESP_LOGI(TAG_HA_CLIENT, "Reconnect state refresh scheduled via WS subscribe_entities (%u entities)",
                (unsigned)initial_sync_total);
        } else if (APP_HA_FETCH_INITIAL_STATES) {
            if (rest_enabled) {
                ESP_LOGI(TAG_HA_CLIENT, "Initial targeted state sync already completed, skipping on reconnect");
            } else {
                ESP_LOGI(TAG_HA_CLIENT, "Initial targeted state sync already completed, skipping on reconnect (WS-only runtime)");
            }
        } else {
            ESP_LOGW(TAG_HA_CLIENT, "Skipping initial state sync (APP_HA_FETCH_INITIAL_STATES=0)");
        }
        if (!rest_enabled) {
            ESP_LOGI(TAG_HA_CLIENT, "Deferring WS weather forecast sync for %" PRId64 " ms after connect",
                HA_WS_WEATHER_PRIORITY_GRACE_MS);
            if (!ws_entities_stream && APP_HA_FETCH_INITIAL_STATES && ws_get_states_block_until > now_ms) {
                ESP_LOGW(TAG_HA_CLIENT, "WS initial get_states delayed for %" PRId64
                    " ms after recent TLS BAD_INPUT_DATA (-0x7100)",
                    (ws_get_states_block_until - now_ms));
            }
        }
        if (queue_weather_bootstrap) {
            ha_client_queue_weather_priority_sync_from_layout(now_ms);
        }
        ha_client_log_mem_snapshot("auth_ok", false);
        if (!APP_HA_SUBSCRIBE_STATE_CHANGED) {
            ESP_LOGW(TAG_HA_CLIENT, "Skipping state_changed subscription (APP_HA_SUBSCRIBE_STATE_CHANGED=0)");
        }
    } else if (strcmp(type->valuestring, "result") == 0) {
        ha_client_handle_result_message(root);
    } else if (strcmp(type->valuestring, "event") == 0) {
        ha_client_handle_event_message(root);
    } else if (strcmp(type->valuestring, "pong") == 0) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
        bool has_id = cJSON_IsNumber(id);
        uint32_t pong_id = has_id ? (uint32_t)id->valuedouble : 0;
        bool cleared_inflight = false;
        bool id_mismatch = false;
        uint32_t expected_id = 0;
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        if (s_client.ping_inflight) {
            expected_id = s_client.ping_inflight_id;
            if (!has_id || s_client.ping_inflight_id == pong_id) {
                s_client.ping_inflight = false;
                s_client.ping_inflight_id = 0;
                s_client.ping_sent_unix_ms = 0;
                s_client.ping_timeout_strikes = 0;
                cleared_inflight = true;
            } else {
                id_mismatch = true;
            }
        }
        s_client.last_rx_unix_ms = now_ms;
        xSemaphoreGive(s_client.mutex);
        if (has_id) {
            if (id_mismatch) {
                ESP_LOGW(TAG_HA_CLIENT, "HA pong id mismatch (expected=%" PRIu32 ", got=%" PRIu32 ")",
                    expected_id, pong_id);
            } else {
                ESP_LOGI(TAG_HA_CLIENT, "HA pong received, id=%" PRIu32, pong_id);
            }
        } else {
            ESP_LOGI(TAG_HA_CLIENT, "HA pong received without id");
        }
        if (!cleared_inflight && !id_mismatch) {
            ESP_LOGD(TAG_HA_CLIENT, "HA pong received while no ping was in-flight");
        }
    } else if (strcmp(type->valuestring, "auth_invalid") == 0) {
        ESP_LOGE(TAG_HA_CLIENT, "HA authentication failed");
    }

    cJSON_Delete(root);
}

static void ha_client_ws_event_cb(const ha_ws_event_t *event, void *user_ctx)
{
    (void)user_ctx;
    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case HA_WS_EVENT_CONNECTED:
        UBaseType_t ws_hwm_connected = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI(TAG_HA_CLIENT, "WebSocket connected (ws_task_hwm=%u words)", (unsigned)ws_hwm_connected);
        ha_client_log_mem_snapshot("ws_connected", false);
        ha_client_reset_ws_rx_assembly();
        ha_client_flush_ws_rx_queue();
        int64_t ws_connected_now_ms = ha_client_now_ms();
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        s_client.authenticated = false;
        s_client.pending_send_auth = false;
        s_client.next_auth_retry_unix_ms = 0;
        s_client.pending_send_pong = false;
        s_client.pending_pong_id = 0;
        s_client.sub_state_via_trigger = false;
        s_client.trigger_sub_req_id = 0;
        s_client.sub_state_via_entities = false;
        s_client.entities_sub_req_id = 0;
        s_client.entities_sub_target_count = 0;
        s_client.entities_sub_sent_count = 0;
        s_client.entities_sub_seen_count = 0;
        s_client.next_entities_subscribe_unix_ms = 0;
        ha_client_clear_entities_sub_buffers();
        s_client.ping_inflight = false;
        s_client.ping_inflight_id = 0;
        s_client.ping_sent_unix_ms = 0;
        s_client.ping_timeout_strikes = 0;
        s_client.weather_ws_req_inflight = false;
        s_client.weather_ws_req_id = 0;
        s_client.weather_ws_req_entity_id[0] = '\0';
        s_client.energy_prefs_req_inflight = false;
        s_client.energy_stats_req_inflight = false;
        s_client.energy_prefs_req_id = 0;
        s_client.energy_stats_req_id = 0;
        s_client.last_ws_tls_stack_err = 0;
        s_client.last_ws_tls_esp_err = ESP_OK;
        s_client.last_ws_sock_errno = 0;
        s_client.last_ws_error_unix_ms = 0;
        s_client.last_rx_unix_ms = ws_connected_now_ms;
        s_client.ws_last_connected_unix_ms = ws_connected_now_ms;
        s_client.ws_error_streak = 0;
        xSemaphoreGive(s_client.mutex);
        break;
    case HA_WS_EVENT_DISCONNECTED:
        UBaseType_t ws_hwm_disconnected = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGW(TAG_HA_CLIENT, "WebSocket disconnected (ws_task_hwm=%u words)", (unsigned)ws_hwm_disconnected);
        ha_client_log_mem_snapshot("ws_disconnected", false);
        ha_client_reset_ws_rx_assembly();
        ha_client_flush_ws_rx_queue();
        int64_t ws_disconnected_now_ms = ha_client_now_ms();
        int64_t ws_session_age_ms = 0;
        uint8_t ws_short_session_strikes = 0;
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        if (s_client.ws_last_connected_unix_ms > 0 && ws_disconnected_now_ms > s_client.ws_last_connected_unix_ms) {
            ws_session_age_ms = ws_disconnected_now_ms - s_client.ws_last_connected_unix_ms;
        }
        if (ws_session_age_ms > 0 && ws_session_age_ms < HA_WS_SHORT_SESSION_MS) {
            if (s_client.ws_short_session_strikes < UINT8_MAX) {
                s_client.ws_short_session_strikes++;
            }
            if (s_client.ws_short_session_strikes >= HA_WS_SHORT_SESSION_STRIKES_TO_WIFI_RECOVER) {
                s_client.pending_force_wifi_recover = true;
            }
        } else if (ws_session_age_ms >= HA_WS_SHORT_SESSION_MS) {
            s_client.ws_short_session_strikes = 0;
        }
        ws_short_session_strikes = s_client.ws_short_session_strikes;
        s_client.authenticated = false;
        s_client.pending_send_auth = false;
        s_client.next_auth_retry_unix_ms = 0;
        s_client.pending_send_pong = false;
        s_client.pending_pong_id = 0;
        s_client.sub_state_via_trigger = false;
        s_client.trigger_sub_req_id = 0;
        s_client.sub_state_via_entities = false;
        s_client.entities_sub_req_id = 0;
        s_client.entities_sub_target_count = 0;
        s_client.entities_sub_sent_count = 0;
        s_client.entities_sub_seen_count = 0;
        s_client.next_entities_subscribe_unix_ms = 0;
        ha_client_clear_entities_sub_buffers();
        s_client.ping_inflight = false;
        s_client.ping_inflight_id = 0;
        s_client.ping_sent_unix_ms = 0;
        s_client.ping_timeout_strikes = 0;
        s_client.weather_ws_req_inflight = false;
        s_client.weather_ws_req_id = 0;
        s_client.weather_ws_req_entity_id[0] = '\0';
        s_client.energy_prefs_req_inflight = false;
        s_client.energy_stats_req_inflight = false;
        s_client.energy_prefs_req_id = 0;
        s_client.energy_stats_req_id = 0;
        if (s_client.light_discovery_phase != HA_LIGHT_DISCOVERY_PHASE_NONE) {
            if (APP_HA_LIGHT_DISCOVERY_TEMPLATE_ENABLED &&
                s_client.light_discovery_phase == HA_LIGHT_DISCOVERY_PHASE_TEMPLATE) {
                s_client.light_discovery_requested = true;
                s_client.light_discovery_inflight = false;
                s_client.light_discovery_req_id = 0;
                s_client.light_discovery_next_step_unix_ms =
                    ws_disconnected_now_ms + HA_LIGHT_DISCOVERY_RETRY_DELAY_MS;
            } else if (APP_HA_LIGHT_DISCOVERY_REGISTRY_ENABLED) {
                s_client.light_discovery_requested = true;
                s_client.light_discovery_inflight = false;
                s_client.light_discovery_req_id = 0;
            } else {
                ha_client_light_discovery_abort_locked();
            }
        } else {
            s_client.light_discovery_inflight = false;
            s_client.light_discovery_req_id = 0;
        }
        xSemaphoreGive(s_client.mutex);
        if (ws_session_age_ms > 0 && ws_session_age_ms < HA_WS_SHORT_SESSION_MS) {
            ESP_LOGW(TAG_HA_CLIENT,
                "Short WS session detected (%" PRId64 " ms), strike=%u/%u",
                ws_session_age_ms, (unsigned)ws_short_session_strikes,
                (unsigned)HA_WS_SHORT_SESSION_STRIKES_TO_WIFI_RECOVER);
        }
        break;
    case HA_WS_EVENT_TEXT:
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        s_client.last_rx_unix_ms = ha_client_now_ms();
        xSemaphoreGive(s_client.mutex);
        ha_client_handle_text_chunk(event);
        break;
    case HA_WS_EVENT_ERROR:
        UBaseType_t ws_hwm_error = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGE(TAG_HA_CLIENT,
            "WebSocket error event (tls_esp=%s tls_stack=%d sock_errno=%d ws_task_hwm=%u words)",
            esp_err_to_name(event->tls_esp_err),
            event->tls_stack_err,
            event->sock_errno,
            (unsigned)ws_hwm_error);
        int64_t ws_error_now_ms = ha_client_now_ms();
        bool tls_bad_input = ha_client_is_tls_bad_input_data(event->tls_stack_err);
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        s_client.ws_error_streak++;
        s_client.last_ws_tls_stack_err = event->tls_stack_err;
        s_client.last_ws_tls_esp_err = event->tls_esp_err;
        s_client.last_ws_sock_errno = event->sock_errno;
        s_client.last_ws_error_unix_ms = ws_error_now_ms;
        if (tls_bad_input) {
            s_client.last_ws_bad_input_unix_ms = ws_error_now_ms;
            int64_t block_until = ws_error_now_ms + HA_WS_GET_STATES_BAD_INPUT_COOLDOWN_MS;
            if (s_client.ws_get_states_block_until_unix_ms < block_until) {
                s_client.ws_get_states_block_until_unix_ms = block_until;
            }
            s_client.pending_get_states = false;
            s_client.get_states_req_id = 0;
        }
        xSemaphoreGive(s_client.mutex);
        ha_client_log_mem_snapshot("ws_error", true);
        if (tls_bad_input) {
            ESP_LOGW(TAG_HA_CLIENT,
                "WS TLS BAD_INPUT_DATA (stack_err=%d) detected, pausing WS get_states for %" PRId64
                " ms and suppressing Wi-Fi force-recover path",
                event->tls_stack_err, HA_WS_GET_STATES_BAD_INPUT_COOLDOWN_MS);
        }
        break;
    default:
        break;
    }
}

static void ha_client_task(void *arg)
{
    (void)arg;
    /* Keep the initial websocket start attempt from being torn down immediately
       by the periodic restart logic while it is still handshaking. */
    int64_t last_ws_restart_ms = esp_timer_get_time() / 1000;
    int64_t wifi_down_since_ms = 0;
    int64_t last_wifi_force_recover_ms = 0;
    bool wifi_seen_connected_once = false;
    while (true) {
        if (s_client.ws_rx_queue != NULL) {
            ha_ws_rx_msg_t msg = {0};
            int drained = 0;
            while (drained < HA_WS_RX_DRAIN_BUDGET && xQueueReceive(s_client.ws_rx_queue, &msg, 0) == pdTRUE) {
                if (msg.payload != NULL && msg.len > 0) {
                    ha_client_handle_text_message(msg.payload, msg.len);
                }
                ha_client_free_ws_msg(&msg);
                drained++;
            }
            if (drained == HA_WS_RX_DRAIN_BUDGET) {
                taskYIELD();
            }
        }

        bool connected = ha_ws_is_connected();
        bool authenticated = false;
        bool published_disconnect = false;
        bool pending_send_auth = false;
        bool pending_initial_layout_sync = false;
        bool initial_layout_sync_done = false;
        bool pending_send_pong = false;
        bool pending_subscribe = false;
        bool pending_get_states = false;
        bool sub_state_via_entities = false;
        bool ws_entities_subscribe_supported = false;
        uint16_t entities_sub_target_count = 0;
        uint16_t entities_sub_sent_count = 0;
        int64_t next_entities_subscribe_unix_ms = 0;
        uint32_t pending_pong_id = 0;
        uint32_t initial_layout_sync_index = 0;
        uint32_t initial_layout_sync_imported = 0;
        uint32_t periodic_layout_sync_cursor = 0;
        bool ping_inflight = false;
        uint32_t ping_inflight_id = 0;
        uint8_t ping_timeout_strikes = 0;
        int64_t ping_sent_unix_ms = 0;
        int64_t last_rx_unix_ms = 0;
        int64_t ws_last_connected_unix_ms = 0;
        int64_t next_auth_retry_unix_ms = 0;
        int64_t next_initial_layout_sync_unix_ms = 0;
        int64_t next_periodic_layout_sync_unix_ms = 0;
        int64_t next_priority_sync_unix_ms = 0;
        uint8_t priority_sync_count = 0;
        uint8_t ws_short_session_strikes = 0;
        bool pending_force_wifi_recover = false;
        bool layout_needs_weather_forecast = false;
        bool layout_needs_ha_energy = false;
        bool pending_energy_prefs = false;
        bool pending_energy_stats = false;
        bool energy_prefs_req_inflight = false;
        bool energy_stats_req_inflight = false;
        int64_t next_energy_sync_unix_ms = 0;
        bool rest_enabled = false;
        uint32_t ws_error_streak = 0;
        int64_t ws_priority_boost_until_unix_ms = 0;
        int last_ws_tls_stack_err = 0;
        int64_t last_ws_bad_input_unix_ms = 0;
        int64_t ws_get_states_block_until_unix_ms = 0;
        bool light_discovery_requested = false;
        bool light_discovery_inflight = false;
        ha_light_discovery_phase_t light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_NONE;
        char light_discovery_domain[APP_HA_DISCOVERY_DOMAIN_MAX_LEN] = {0};
        int64_t light_discovery_next_step_unix_ms = 0;
        int64_t light_discovery_last_wait_log_unix_ms = 0;
        uint16_t light_discovery_template_offset = 0;
        size_t free_internal = 0;
        size_t ws_q_used = 0;
        uint8_t ws_q_fill_pct = 0;
        ha_bg_budget_level_t bg_budget_level = HA_BG_BUDGET_NORMAL;
        bool ws_priority_boost_active = false;
        bool should_send_ping = false;
        bool should_run_priority_sync_step = false;
        bool should_run_initial_layout_sync_step = false;
        bool should_run_periodic_layout_sync_step = false;
        bool ping_timed_out = false;
        int64_t now_ms = ha_client_now_ms();
        int64_t ping_interval_ms = ha_client_ping_interval_ms_effective();
        bool wifi_up = wifi_mgr_is_connected();
        bool ws_running = ha_ws_is_running();
        if (wifi_up) {
            wifi_seen_connected_once = true;
        }

        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        authenticated = s_client.authenticated;
        published_disconnect = s_client.published_disconnect;
        pending_send_auth = s_client.pending_send_auth;
        pending_initial_layout_sync = s_client.pending_initial_layout_sync;
        initial_layout_sync_done = s_client.initial_layout_sync_done;
        pending_send_pong = s_client.pending_send_pong;
        pending_pong_id = s_client.pending_pong_id;
        pending_subscribe = s_client.pending_subscribe;
        pending_get_states = s_client.pending_get_states;
        sub_state_via_entities = s_client.sub_state_via_entities;
        ws_entities_subscribe_supported = s_client.ws_entities_subscribe_supported;
        entities_sub_target_count = s_client.entities_sub_target_count;
        entities_sub_sent_count = s_client.entities_sub_sent_count;
        next_entities_subscribe_unix_ms = s_client.next_entities_subscribe_unix_ms;
        initial_layout_sync_index = s_client.initial_layout_sync_index;
        initial_layout_sync_imported = s_client.initial_layout_sync_imported;
        periodic_layout_sync_cursor = s_client.periodic_layout_sync_cursor;
        ping_inflight = s_client.ping_inflight;
        ping_inflight_id = s_client.ping_inflight_id;
        ping_timeout_strikes = s_client.ping_timeout_strikes;
        ping_sent_unix_ms = s_client.ping_sent_unix_ms;
        last_rx_unix_ms = s_client.last_rx_unix_ms;
        ws_last_connected_unix_ms = s_client.ws_last_connected_unix_ms;
        next_auth_retry_unix_ms = s_client.next_auth_retry_unix_ms;
        next_initial_layout_sync_unix_ms = s_client.next_initial_layout_sync_unix_ms;
        next_periodic_layout_sync_unix_ms = s_client.next_periodic_layout_sync_unix_ms;
        next_priority_sync_unix_ms = s_client.next_priority_sync_unix_ms;
        priority_sync_count = s_client.priority_sync_count;
        ws_short_session_strikes = s_client.ws_short_session_strikes;
        pending_force_wifi_recover = s_client.pending_force_wifi_recover;
        layout_needs_weather_forecast = s_client.layout_needs_weather_forecast;
        layout_needs_ha_energy = s_client.layout_needs_ha_energy;
        pending_energy_prefs = s_client.pending_energy_prefs;
        pending_energy_stats = s_client.pending_energy_stats;
        energy_prefs_req_inflight = s_client.energy_prefs_req_inflight;
        energy_stats_req_inflight = s_client.energy_stats_req_inflight;
        next_energy_sync_unix_ms = s_client.next_energy_sync_unix_ms;
        rest_enabled = s_client.rest_enabled;
        ws_error_streak = s_client.ws_error_streak;
        ws_priority_boost_until_unix_ms = s_client.ws_priority_boost_until_unix_ms;
        last_ws_tls_stack_err = s_client.last_ws_tls_stack_err;
        last_ws_bad_input_unix_ms = s_client.last_ws_bad_input_unix_ms;
        ws_get_states_block_until_unix_ms = s_client.ws_get_states_block_until_unix_ms;
        light_discovery_requested = s_client.light_discovery_requested;
        light_discovery_inflight = s_client.light_discovery_inflight;
        light_discovery_phase = s_client.light_discovery_phase;
        safe_copy_cstr(light_discovery_domain, sizeof(light_discovery_domain),
            ha_client_discovery_domain_or_default(s_client.light_discovery_domain));
        light_discovery_next_step_unix_ms = s_client.light_discovery_next_step_unix_ms;
        light_discovery_last_wait_log_unix_ms = s_client.light_discovery_last_wait_log_unix_ms;
        light_discovery_template_offset = s_client.light_discovery_template_offset;
        if (connected && authenticated && wifi_up) {
            if (ping_inflight && (now_ms - ping_sent_unix_ms) >= ha_client_ping_timeout_ms()) {
                ping_timed_out = true;
            } else if (!ping_inflight && (now_ms - last_rx_unix_ms) >= ping_interval_ms) {
                should_send_ping = true;
            }
            if (rest_enabled && pending_initial_layout_sync && now_ms >= next_initial_layout_sync_unix_ms) {
                should_run_initial_layout_sync_step = true;
            } else if (rest_enabled && !pending_initial_layout_sync && now_ms >= next_periodic_layout_sync_unix_ms) {
                should_run_periodic_layout_sync_step = true;
            }
            if (priority_sync_count > 0 && now_ms >= next_priority_sync_unix_ms) {
                should_run_priority_sync_step = true;
            }
        }
        xSemaphoreGive(s_client.mutex);

        ws_priority_boost_active = (ws_priority_boost_until_unix_ms > now_ms);

        free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (s_client.ws_rx_queue != NULL) {
            ws_q_used = (size_t)uxQueueMessagesWaiting(s_client.ws_rx_queue);
        }
        if (APP_HA_QUEUE_LENGTH > 0) {
            size_t pct = (ws_q_used * 100U) / (size_t)APP_HA_QUEUE_LENGTH;
            if (pct > 100U) {
                pct = 100U;
            }
            ws_q_fill_pct = (uint8_t)pct;
        }
        bg_budget_level = ha_client_eval_bg_budget_level(free_internal, ws_q_fill_pct, ws_error_streak);
        ha_client_update_bg_budget_state(bg_budget_level, free_internal, ws_q_fill_pct, ws_error_streak, now_ms);

        bool ws_bad_input_recent = false;
        if (last_ws_bad_input_unix_ms > 0 &&
            (now_ms - last_ws_bad_input_unix_ms) <= HA_WS_GET_STATES_BAD_INPUT_COOLDOWN_MS) {
            ws_bad_input_recent = true;
        } else if (ha_client_is_tls_bad_input_data(last_ws_tls_stack_err)) {
            ws_bad_input_recent = true;
        }

        if (!wifi_up) {
            if (wifi_down_since_ms == 0) {
                wifi_down_since_ms = now_ms;
            }
            if (wifi_seen_connected_once &&
                (now_ms - wifi_down_since_ms) >= HA_WIFI_DOWN_RECOVERY_MS &&
                (now_ms - last_wifi_force_recover_ms) >= HA_WIFI_FORCE_RECOVER_COOLDOWN_MS) {
                if (connected) {
                    ESP_LOGW(TAG_HA_CLIENT, "Wi-Fi link appears down while WS is still connected, stopping websocket");
                    ha_ws_stop();
                    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                    s_client.authenticated = false;
                    s_client.pending_send_auth = false;
                    s_client.pending_send_pong = false;
                    s_client.pending_pong_id = 0;
                    s_client.ping_inflight = false;
                    s_client.ping_inflight_id = 0;
                    s_client.ping_sent_unix_ms = 0;
                    s_client.ping_timeout_strikes = 0;
                    xSemaphoreGive(s_client.mutex);
                    connected = false;
                    authenticated = false;
                }

                bool used_transport_recover = false;
                esp_err_t recover_err =
                    ha_client_force_recover_with_escalation(false, "wifi-link-down", &used_transport_recover);
                if (recover_err == ESP_OK) {
                    ESP_LOGW(TAG_HA_CLIENT,
                        "Forced %s recover after %" PRId64 " ms of link-down state",
                        used_transport_recover ? "C6 transport" : "Wi-Fi",
                        (now_ms - wifi_down_since_ms));
                } else {
                    ESP_LOGW(TAG_HA_CLIENT, "Failed to force network recover: %s", esp_err_to_name(recover_err));
                }
                last_wifi_force_recover_ms = now_ms;
                wifi_down_since_ms = now_ms;
            }
        } else {
            wifi_down_since_ms = 0;
        }

        if (wifi_up && wifi_seen_connected_once && pending_force_wifi_recover &&
            (now_ms - last_wifi_force_recover_ms) >= HA_WIFI_FORCE_RECOVER_COOLDOWN_MS) {
            if (!HA_WS_ESCALATE_RECOVER_WHEN_WIFI_UP) {
                ESP_LOGW(TAG_HA_CLIENT,
                    "Suppressing Wi-Fi/C6 recover on short WS sessions while Wi-Fi link is up (likely HA downtime)");
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.pending_force_wifi_recover = false;
                s_client.ws_short_session_strikes = 0;
                xSemaphoreGive(s_client.mutex);
                last_wifi_force_recover_ms = now_ms;
                vTaskDelay(HA_CLIENT_TASK_DELAY_TICKS);
                continue;
            }
            if (ws_bad_input_recent) {
                ESP_LOGW(TAG_HA_CLIENT,
                    "Suppressing Wi-Fi recover on short WS sessions because last WS error is TLS BAD_INPUT_DATA (-0x7100)");
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.pending_force_wifi_recover = false;
                s_client.ws_short_session_strikes = 0;
                xSemaphoreGive(s_client.mutex);
                last_wifi_force_recover_ms = now_ms;
                vTaskDelay(HA_CLIENT_TASK_DELAY_TICKS);
                continue;
            }
            bool prefer_transport_recover =
                ws_short_session_strikes >= HA_WS_SHORT_SESSION_STRIKES_TO_TRANSPORT_RECOVER;
            bool used_transport_recover = false;
            esp_err_t recover_err = ha_client_force_recover_with_escalation(
                prefer_transport_recover, "ws-short-session-strikes", &used_transport_recover);
            if (recover_err == ESP_OK) {
                if (used_transport_recover) {
                    ESP_LOGW(TAG_HA_CLIENT,
                        "Forced C6 transport recover due to repeated short WS sessions (strike=%u/%u)",
                        (unsigned)ws_short_session_strikes,
                        (unsigned)HA_WS_SHORT_SESSION_STRIKES_TO_TRANSPORT_RECOVER);
                } else {
                    ESP_LOGW(TAG_HA_CLIENT,
                        "Forced Wi-Fi recover due to repeated short WS sessions (strike=%u/%u)",
                        (unsigned)ws_short_session_strikes,
                        (unsigned)HA_WS_SHORT_SESSION_STRIKES_TO_WIFI_RECOVER);
                }
            } else {
                ESP_LOGW(TAG_HA_CLIENT,
                    "Failed forced %s recover on WS short-session strikes: %s",
                    prefer_transport_recover ? "C6 transport" : "Wi-Fi",
                    esp_err_to_name(recover_err));
            }
            last_wifi_force_recover_ms = now_ms;
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            s_client.pending_force_wifi_recover = false;
            s_client.ws_short_session_strikes = 0;
            xSemaphoreGive(s_client.mutex);
        }

        if (wifi_up && wifi_seen_connected_once && !connected &&
            ws_error_streak >= HA_WS_ERROR_STREAK_WIFI_RECOVER_THRESHOLD &&
            (now_ms - last_wifi_force_recover_ms) >= HA_WIFI_FORCE_RECOVER_COOLDOWN_MS) {
            if (!HA_WS_ESCALATE_RECOVER_WHEN_WIFI_UP) {
                ESP_LOGW(TAG_HA_CLIENT,
                    "Suppressing Wi-Fi/C6 recover on WS connect error streak while Wi-Fi link is up (likely HA downtime)");
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.ws_error_streak = 0;
                xSemaphoreGive(s_client.mutex);
                last_wifi_force_recover_ms = now_ms;
                vTaskDelay(HA_CLIENT_TASK_DELAY_TICKS);
                continue;
            }
            if (ws_bad_input_recent) {
                ESP_LOGW(TAG_HA_CLIENT,
                    "Suppressing Wi-Fi recover on WS connect error streak because last WS error is TLS BAD_INPUT_DATA (-0x7100)");
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.ws_error_streak = 0;
                xSemaphoreGive(s_client.mutex);
                last_wifi_force_recover_ms = now_ms;
                vTaskDelay(HA_CLIENT_TASK_DELAY_TICKS);
                continue;
            }
            bool prefer_transport_recover =
                ws_error_streak >= HA_WS_ERROR_STREAK_TRANSPORT_RECOVER_THRESHOLD;
            bool used_transport_recover = false;
            esp_err_t recover_err = ha_client_force_recover_with_escalation(
                prefer_transport_recover, "ws-connect-error-streak", &used_transport_recover);
            if (recover_err == ESP_OK) {
                if (used_transport_recover) {
                    ESP_LOGW(TAG_HA_CLIENT,
                        "Forced C6 transport recover due to WS connect error streak=%u",
                        (unsigned)ws_error_streak);
                } else {
                    ESP_LOGW(TAG_HA_CLIENT,
                        "Forced Wi-Fi recover due to WS connect error streak=%u",
                        (unsigned)ws_error_streak);
                }
            } else {
                ESP_LOGW(TAG_HA_CLIENT,
                    "Failed forced %s recover on WS connect errors: %s",
                    prefer_transport_recover ? "C6 transport" : "Wi-Fi",
                    esp_err_to_name(recover_err));
            }
            last_wifi_force_recover_ms = now_ms;
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            s_client.ws_error_streak = 0;
            xSemaphoreGive(s_client.mutex);
        }

        if (ping_timed_out) {
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            s_client.ping_inflight = false;
            s_client.ping_inflight_id = 0;
            s_client.ping_sent_unix_ms = 0;
            if (s_client.ping_timeout_strikes < 255) {
                s_client.ping_timeout_strikes++;
            }
            ping_timeout_strikes = s_client.ping_timeout_strikes;
            xSemaphoreGive(s_client.mutex);
            if (ping_timeout_strikes < HA_PING_TIMEOUT_STRIKES_TO_RECONNECT) {
                ESP_LOGW(TAG_HA_CLIENT,
                    "HA pong timeout (id=%" PRIu32 ", age=%" PRId64 " ms), strike=%u/%u; keeping websocket alive",
                    ping_inflight_id, (now_ms - ping_sent_unix_ms), (unsigned)ping_timeout_strikes,
                    (unsigned)HA_PING_TIMEOUT_STRIKES_TO_RECONNECT);
                continue;
            }

            ESP_LOGW(TAG_HA_CLIENT,
                "HA pong timeout (id=%" PRIu32 ", age=%" PRId64 " ms), strike=%u/%u; forcing websocket reconnect",
                ping_inflight_id, (now_ms - ping_sent_unix_ms), (unsigned)ping_timeout_strikes,
                (unsigned)HA_PING_TIMEOUT_STRIKES_TO_RECONNECT);
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            s_client.ws_error_streak++;
            xSemaphoreGive(s_client.mutex);
            ha_ws_stop();
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            s_client.authenticated = false;
            s_client.pending_send_auth = false;
            s_client.pending_send_pong = false;
            s_client.pending_pong_id = 0;
            xSemaphoreGive(s_client.mutex);
            last_ws_restart_ms = now_ms - HA_WS_RESTART_INTERVAL_MS;
            continue;
        }

        if ((!connected || !authenticated) && !published_disconnect) {
            ha_client_publish_event(EV_HA_DISCONNECTED, NULL);
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            s_client.published_disconnect = true;
            xSemaphoreGive(s_client.mutex);
        }

        int64_t ws_restart_wait_ms = HA_WS_RESTART_INTERVAL_MS;
        if (ws_error_streak > 0) {
            uint32_t backoff_steps = ws_error_streak;
            if (backoff_steps > 4) {
                backoff_steps = 4;
            }
            for (uint32_t i = 0; i < backoff_steps; i++) {
                ws_restart_wait_ms *= 2;
                if (ws_restart_wait_ms >= HA_WS_RESTART_INTERVAL_MAX_MS) {
                    ws_restart_wait_ms = HA_WS_RESTART_INTERVAL_MAX_MS;
                    break;
                }
            }
        }
        ws_restart_wait_ms += (int64_t)(esp_random() % (uint32_t)(HA_WS_RESTART_JITTER_MS + 1));

        if (!connected && wifi_up && (now_ms - last_ws_restart_ms) >= ws_restart_wait_ms) {
            if (ws_running && (now_ms - last_ws_restart_ms) < HA_WS_CONNECT_GRACE_MS) {
                vTaskDelay(HA_CLIENT_TASK_DELAY_TICKS);
                continue;
            }
            ha_ws_stop();
            ha_ws_config_t ws_cfg = {
                .uri = s_client.ws_url,
                .event_cb = ha_client_ws_event_cb,
                .user_ctx = NULL,
            };
            ha_client_log_mem_snapshot("ws_restart_attempt", false);
            esp_err_t ws_err = ha_ws_start(&ws_cfg);
            if (ws_err != ESP_OK) {
                ESP_LOGW(TAG_HA_CLIENT, "WebSocket restart failed: %s (next retry in %" PRId64 " ms)",
                    esp_err_to_name(ws_err), ws_restart_wait_ms);
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.ws_error_streak++;
                xSemaphoreGive(s_client.mutex);
            } else {
                ESP_LOGI(TAG_HA_CLIENT, "WebSocket restart triggered");
            }
            last_ws_restart_ms = now_ms;
        }

        if (connected && authenticated) {
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            s_client.published_disconnect = false;
            xSemaphoreGive(s_client.mutex);
        }

        if (connected && pending_send_auth && now_ms >= next_auth_retry_unix_ms) {
            if (!ha_ws_is_connected()) {
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.next_auth_retry_unix_ms = now_ms + HA_AUTH_RETRY_INTERVAL_MS;
                xSemaphoreGive(s_client.mutex);
            } else if (ha_client_send_auth() == ESP_OK) {
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.pending_send_auth = false;
                s_client.next_auth_retry_unix_ms = 0;
                xSemaphoreGive(s_client.mutex);
            } else {
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.next_auth_retry_unix_ms = now_ms + HA_AUTH_RETRY_INTERVAL_MS;
                xSemaphoreGive(s_client.mutex);
            }
        }
        if (connected && pending_send_pong) {
            if (ha_client_send_pong(pending_pong_id) == ESP_OK) {
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.pending_send_pong = false;
                xSemaphoreGive(s_client.mutex);
            }
        }
        if (connected && authenticated && layout_needs_ha_energy && !energy_prefs_req_inflight &&
            !energy_stats_req_inflight && now_ms >= next_energy_sync_unix_ms &&
            (!sub_state_via_entities || entities_sub_sent_count >= entities_sub_target_count)) {
            /* Central TLS gate: no other heavy WS request in flight + cooldown elapsed. */
            bool gate_ok = false;
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            gate_ok = ha_client_ws_send_gate_ok_locked(HA_WS_SEND_HEAVY, now_ms);
            xSemaphoreGive(s_client.mutex);
            if (gate_ok && pending_energy_stats) {
                uint32_t req_id = 0;
                esp_err_t err = ha_client_send_energy_stats_ws(&req_id);
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                if (err == ESP_OK) {
                    s_client.pending_energy_stats = false;
                    s_client.energy_stats_req_inflight = true;
                    s_client.energy_stats_req_id = req_id;
                } else {
                    s_client.pending_energy_stats = false;
                    s_client.pending_energy_prefs = true;
                    s_client.next_energy_sync_unix_ms = now_ms + HA_ENERGY_SYNC_RETRY_MS;
                }
                xSemaphoreGive(s_client.mutex);
            } else if (gate_ok && (pending_energy_prefs || now_ms >= next_energy_sync_unix_ms)) {
                uint32_t req_id = 0;
                esp_err_t err = ha_client_send_energy_prefs_ws(&req_id);
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                if (err == ESP_OK) {
                    s_client.pending_energy_prefs = false;
                    s_client.energy_prefs_req_inflight = true;
                    s_client.energy_prefs_req_id = req_id;
                } else {
                    s_client.pending_energy_prefs = true;
                    s_client.next_energy_sync_unix_ms = now_ms + HA_ENERGY_SYNC_RETRY_MS;
                }
                xSemaphoreGive(s_client.mutex);
            }
        }
        if (connected && authenticated && light_discovery_requested && !light_discovery_inflight) {
            if (APP_HA_LIGHT_DISCOVERY_TEMPLATE_ENABLED &&
                light_discovery_phase == HA_LIGHT_DISCOVERY_PHASE_TEMPLATE) {
                size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
                int64_t ws_age_ms = (ws_last_connected_unix_ms > 0) ? (now_ms - ws_last_connected_unix_ms) : 0;
                bool discovery_template_stable = !pending_subscribe &&
                    !pending_get_states &&
                    !ping_inflight &&
                    !should_send_ping &&
                    !should_run_priority_sync_step &&
                    !should_run_initial_layout_sync_step &&
                    !should_run_periodic_layout_sync_step &&
                    ws_q_used == 0 &&
                    ws_last_connected_unix_ms > 0 &&
                    ws_age_ms >= HA_LIGHT_DISCOVERY_TEMPLATE_STABLE_DELAY_MS &&
                    free_internal >= HA_LIGHT_DISCOVERY_TEMPLATE_MIN_INTERNAL_FREE_BYTES &&
                    largest_internal >= HA_LIGHT_DISCOVERY_TEMPLATE_MIN_INTERNAL_LARGEST_BYTES &&
                    now_ms >= light_discovery_next_step_unix_ms;
                if (discovery_template_stable) {
                    uint16_t page_size = APP_HA_LIGHT_DISCOVERY_PAGE_SIZE;
                    if (page_size == 0 || page_size > APP_HA_LIGHT_DISCOVERY_MAX_ITEMS) {
                        page_size = APP_HA_LIGHT_DISCOVERY_MAX_ITEMS;
                    }
                    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                    s_client.light_discovery_last_wait_log_unix_ms = 0;
                    xSemaphoreGive(s_client.mutex);
                    (void)ha_client_light_discovery_template_step(light_discovery_template_offset, page_size);
                } else if ((now_ms - light_discovery_last_wait_log_unix_ms) >=
                           HA_LIGHT_DISCOVERY_WAIT_LOG_INTERVAL_MS) {
                    const char *reason = "unknown";
                    if (pending_subscribe) {
                        reason = "subscriptions";
                    } else if (pending_get_states) {
                        reason = "get_states";
                    } else if (ping_inflight || should_send_ping) {
                        reason = "ping";
                    } else if (should_run_priority_sync_step || should_run_initial_layout_sync_step ||
                               should_run_periodic_layout_sync_step) {
                        reason = "sync";
                    } else if (ws_q_used != 0) {
                        reason = "ws_queue";
                    } else if (ws_last_connected_unix_ms == 0 ||
                               ws_age_ms < HA_LIGHT_DISCOVERY_TEMPLATE_STABLE_DELAY_MS) {
                        reason = "ws_grace";
                    } else if (free_internal < HA_LIGHT_DISCOVERY_TEMPLATE_MIN_INTERNAL_FREE_BYTES) {
                        reason = "internal_heap";
                    } else if (largest_internal < HA_LIGHT_DISCOVERY_TEMPLATE_MIN_INTERNAL_LARGEST_BYTES) {
                        reason = "internal_block";
                    } else if (now_ms < light_discovery_next_step_unix_ms) {
                        reason = "page_delay";
                    }
                    ESP_LOGI(TAG_HA_CLIENT,
                        "Entity discovery waiting: domain=%s reason=%s ws_age=%" PRId64 " ms int_free=%u int_largest=%u ws_q=%u initial_done=%u",
                        light_discovery_domain,
                        reason,
                        ws_age_ms,
                        (unsigned)free_internal,
                        (unsigned)largest_internal,
                        (unsigned)ws_q_used,
                        initial_layout_sync_done ? 1U : 0U);
                    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                    s_client.light_discovery_last_wait_log_unix_ms = now_ms;
                    xSemaphoreGive(s_client.mutex);
                }
            } else if (APP_HA_LIGHT_DISCOVERY_REGISTRY_ENABLED) {
                size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
                bool discovery_ws_stable = initial_layout_sync_done &&
                    !pending_subscribe &&
                    !pending_get_states &&
                    !ping_inflight &&
                    !should_send_ping &&
                    !should_run_priority_sync_step &&
                    !should_run_initial_layout_sync_step &&
                    !should_run_periodic_layout_sync_step &&
                    ws_q_used == 0 &&
                    ws_last_connected_unix_ms > 0 &&
                    (now_ms - ws_last_connected_unix_ms) >= HA_LIGHT_DISCOVERY_WS_STABLE_DELAY_MS &&
                    free_internal >= HA_LIGHT_DISCOVERY_MIN_INTERNAL_FREE_BYTES &&
                    largest_internal >= HA_LIGHT_DISCOVERY_MIN_INTERNAL_LARGEST_BYTES;
                if (discovery_ws_stable) {
                    /* Central TLS gate: defer if another heavy WS request is in flight
                     * or the heavy-cooldown has not yet elapsed. */
                    bool gate_ok = false;
                    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                    gate_ok = ha_client_ws_send_gate_ok_locked(HA_WS_SEND_HEAVY, now_ms);
                    xSemaphoreGive(s_client.mutex);
                    if (gate_ok) {
                        if (ha_client_send_light_discovery_request(light_discovery_phase) != ESP_OK) {
                            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                            s_client.light_discovery_requested = true;
                            s_client.light_discovery_inflight = false;
                            xSemaphoreGive(s_client.mutex);
                        }
                    }
                }
            } else {
                (void)ha_client_light_discovery_refresh_from_model(light_discovery_domain, NULL, now_ms);
            }
        }
        if (connected && authenticated && pending_subscribe) {
            bool use_entities_subscribe_seq =
                (!rest_enabled && ws_entities_subscribe_supported && entities_sub_target_count > 0);
            if (use_entities_subscribe_seq) {
                if (now_ms >= next_entities_subscribe_unix_ms) {
                    if (entities_sub_sent_count >= entities_sub_target_count) {
                        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                        s_client.pending_subscribe = false;
                        xSemaphoreGive(s_client.mutex);
                    } else {
                        char entity_id[APP_MAX_ENTITY_ID_LEN] = {0};
                        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                        if (s_client.entities_sub_sent_count < s_client.entities_sub_target_count &&
                            s_client.entities_sub_sent_count < HA_WS_ENTITIES_SUB_MAX) {
                            const char *target = ha_client_entities_sub_target_at(s_client.entities_sub_sent_count);
                            if (target != NULL) {
                                safe_copy_cstr(entity_id, sizeof(entity_id), target);
                            }
                        }
                        xSemaphoreGive(s_client.mutex);

                        uint32_t req_id = 0;
                        if (entity_id[0] != '\0' &&
                            ha_client_send_subscribe_single_entity(entity_id, &req_id) == ESP_OK) {
                            uint16_t sent_after = 0;
                            uint16_t target_after = 0;
                            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                            uint16_t idx = s_client.entities_sub_sent_count;
                            if (idx < HA_WS_ENTITIES_SUB_MAX) {
                                s_client.entities_sub_req_ids[idx] = req_id;
                            }
                            if (s_client.entities_sub_sent_count < s_client.entities_sub_target_count) {
                                s_client.entities_sub_sent_count++;
                            }
                            sent_after = s_client.entities_sub_sent_count;
                            target_after = s_client.entities_sub_target_count;
                            s_client.entities_sub_req_id = req_id;
                            s_client.sub_state_via_entities = true;
                            s_client.sub_state_via_trigger = false;
                            s_client.trigger_sub_req_id = 0;
                            s_client.pending_subscribe =
                                (s_client.entities_sub_sent_count < s_client.entities_sub_target_count);
                            s_client.next_entities_subscribe_unix_ms = now_ms + HA_WS_ENTITIES_SUBSCRIBE_STEP_DELAY_MS;
                            xSemaphoreGive(s_client.mutex);
                            ESP_LOGI(TAG_HA_CLIENT, "WS subscribe_entities step %u/%u: %s",
                                (unsigned)sent_after, (unsigned)target_after, entity_id);
                        } else {
                            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                            s_client.next_entities_subscribe_unix_ms = now_ms + HA_AUTH_RETRY_INTERVAL_MS;
                            xSemaphoreGive(s_client.mutex);
                        }
                    }
                }
            } else if (ha_client_send_subscribe_state_changed() == ESP_OK) {
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.pending_subscribe = false;
                if (!s_client.rest_enabled && APP_HA_FETCH_INITIAL_STATES && !s_client.initial_layout_sync_done &&
                    !s_client.pending_get_states && !s_client.sub_state_via_entities) {
                    int64_t next_allowed = now_ms + HA_WS_GET_STATES_POST_SUBSCRIBE_DELAY_MS;
                    if (s_client.ws_get_states_block_until_unix_ms > next_allowed) {
                        next_allowed = s_client.ws_get_states_block_until_unix_ms;
                    }
                    s_client.pending_get_states = true;
                    s_client.next_initial_layout_sync_unix_ms = next_allowed;
                    s_client.get_states_req_id = 0;
                }
                xSemaphoreGive(s_client.mutex);
            }
        }
        if (connected && authenticated && !rest_enabled && !sub_state_via_entities && APP_HA_FETCH_INITIAL_STATES &&
            !initial_layout_sync_done && !pending_subscribe && !pending_get_states &&
            now_ms >= ws_get_states_block_until_unix_ms) {
            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
            if (!s_client.rest_enabled && !s_client.sub_state_via_entities && APP_HA_FETCH_INITIAL_STATES &&
                !s_client.initial_layout_sync_done &&
                !s_client.pending_subscribe && !s_client.pending_get_states &&
                now_ms >= s_client.ws_get_states_block_until_unix_ms) {
                s_client.pending_get_states = true;
                s_client.next_initial_layout_sync_unix_ms = now_ms + HA_WS_GET_STATES_POST_SUBSCRIBE_DELAY_MS;
                s_client.get_states_req_id = 0;
                pending_get_states = true;
                next_initial_layout_sync_unix_ms = s_client.next_initial_layout_sync_unix_ms;
            }
            xSemaphoreGive(s_client.mutex);
        }
        if (connected && authenticated && pending_get_states) {
            bool session_ready = (ws_last_connected_unix_ms == 0) ||
                ((now_ms - ws_last_connected_unix_ms) >= HA_WS_GET_STATES_MIN_SESSION_MS);
            if (session_ready && now_ms >= next_initial_layout_sync_unix_ms) {
                if (ha_client_send_get_states() == ESP_OK) {
                    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                    s_client.pending_get_states = false;
                    xSemaphoreGive(s_client.mutex);
                } else {
                    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                    s_client.next_initial_layout_sync_unix_ms = now_ms + HA_INITIAL_LAYOUT_SYNC_RETRY_INTERVAL_MS;
                    xSemaphoreGive(s_client.mutex);
                }
            }
        }
        if (connected && authenticated && should_send_ping) {
            uint32_t ping_id = 0;
            if (ha_client_send_ping(&ping_id) != ESP_OK) {
                ESP_LOGW(TAG_HA_CLIENT, "Failed to send HA ping, forcing websocket reconnect");
                ha_ws_stop();
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.authenticated = false;
                s_client.pending_send_auth = false;
                s_client.pending_send_pong = false;
                s_client.pending_pong_id = 0;
                s_client.ping_inflight = false;
                s_client.ping_inflight_id = 0;
                s_client.ping_sent_unix_ms = 0;
                s_client.ping_timeout_strikes = 0;
                xSemaphoreGive(s_client.mutex);
                last_ws_restart_ms = now_ms - HA_WS_RESTART_INTERVAL_MS;
                continue;
            } else {
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.ping_inflight = true;
                s_client.ping_inflight_id = ping_id;
                s_client.ping_sent_unix_ms = now_ms;
                xSemaphoreGive(s_client.mutex);
                ESP_LOGI(TAG_HA_CLIENT, "HA ping sent, id=%" PRIu32, ping_id);
            }
        }

        if (connected && authenticated && wifi_up && should_run_priority_sync_step) {
            if (ws_priority_boost_active) {
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.next_priority_sync_unix_ms = ws_priority_boost_until_unix_ms;
                xSemaphoreGive(s_client.mutex);
                vTaskDelay(HA_CLIENT_TASK_DELAY_TICKS);
                continue;
            }
            if (rest_enabled) {
                int64_t defer_ms = 0;
                if (ha_client_should_defer_bg_http(bg_budget_level, now_ms, &defer_ms)) {
                    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                    s_client.next_priority_sync_unix_ms = now_ms + defer_ms;
                    xSemaphoreGive(s_client.mutex);
                } else {
                    char entity_id[APP_MAX_ENTITY_ID_LEN] = {0};
                    bool has_work = false;
                    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                    has_work = ha_client_priority_sync_queue_pop_locked(entity_id, sizeof(entity_id));
                    xSemaphoreGive(s_client.mutex);

                    if (has_work) {
                        esp_err_t sync_err =
                            ha_client_fetch_state_http(entity_id, layout_needs_weather_forecast, false);
                        if (sync_err == ESP_OK) {
                            ha_client_publish_event(EV_HA_STATE_CHANGED, entity_id);
                        }

                        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                        if (!(sync_err == ESP_OK || sync_err == ESP_ERR_INVALID_RESPONSE ||
                                sync_err == ESP_ERR_NOT_FOUND || sync_err == ESP_ERR_TIMEOUT ||
                                sync_err == ESP_ERR_NOT_SUPPORTED)) {
                            ha_client_priority_sync_queue_push_locked(entity_id);
                            s_client.next_priority_sync_unix_ms = now_ms + HA_PRIORITY_SYNC_RETRY_INTERVAL_MS;
                        } else {
                            s_client.next_priority_sync_unix_ms = now_ms + ha_client_interval_priority_step_ms(bg_budget_level);
                        }
                        xSemaphoreGive(s_client.mutex);
                    } else {
                        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                        s_client.next_priority_sync_unix_ms = now_ms + ha_client_interval_priority_step_ms(bg_budget_level);
                        xSemaphoreGive(s_client.mutex);
                    }
                }
            } else {
                char entity_id[APP_MAX_ENTITY_ID_LEN] = {0};
                bool has_work = false;
                bool ws_req_inflight = false;
                bool ws_weather_stable = false;
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                has_work = ha_client_priority_sync_queue_pop_locked(entity_id, sizeof(entity_id));
                ws_req_inflight = s_client.weather_ws_req_inflight;
                xSemaphoreGive(s_client.mutex);
                ws_weather_stable = (ws_last_connected_unix_ms > 0) &&
                    ((now_ms - ws_last_connected_unix_ms) >= HA_WS_WEATHER_PRIORITY_GRACE_MS);

                if (has_work) {
                    if (!ha_client_entity_is_weather(entity_id)) {
                        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                        s_client.next_priority_sync_unix_ms = now_ms + ha_client_interval_priority_step_ms(bg_budget_level);
                        xSemaphoreGive(s_client.mutex);
                    } else if (!ws_weather_stable) {
                        int64_t elapsed_ms = (ws_last_connected_unix_ms > 0) ? (now_ms - ws_last_connected_unix_ms) : 0;
                        if (elapsed_ms < 0) {
                            elapsed_ms = 0;
                        }
                        int64_t wait_ms = HA_WS_WEATHER_PRIORITY_GRACE_MS - elapsed_ms;
                        if (wait_ms < HA_PRIORITY_SYNC_RETRY_INTERVAL_MS) {
                            wait_ms = HA_PRIORITY_SYNC_RETRY_INTERVAL_MS;
                        }
                        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                        ha_client_priority_sync_queue_push_locked(entity_id);
                        s_client.next_priority_sync_unix_ms = now_ms + wait_ms;
                        xSemaphoreGive(s_client.mutex);
                    } else if (ws_req_inflight) {
                        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                        ha_client_priority_sync_queue_push_locked(entity_id);
                        s_client.next_priority_sync_unix_ms = now_ms + HA_PRIORITY_SYNC_RETRY_INTERVAL_MS;
                        xSemaphoreGive(s_client.mutex);
                    } else {
                        /* Central TLS gate: defer if another heavy WS request is in flight
                         * or the heavy-cooldown has not yet elapsed. */
                        bool gate_ok = false;
                        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                        gate_ok = ha_client_ws_send_gate_ok_locked(HA_WS_SEND_HEAVY, now_ms);
                        xSemaphoreGive(s_client.mutex);
                        if (!gate_ok) {
                            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                            ha_client_priority_sync_queue_push_locked(entity_id);
                            s_client.next_priority_sync_unix_ms = now_ms + HA_PRIORITY_SYNC_RETRY_INTERVAL_MS;
                            xSemaphoreGive(s_client.mutex);
                        } else {
                            uint32_t ws_req_id = 0;
                            esp_err_t ws_req_err = ha_client_send_weather_daily_forecast_ws(entity_id, &ws_req_id);
                            xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                            if (ws_req_err == ESP_OK) {
                                s_client.weather_ws_req_inflight = true;
                                s_client.weather_ws_req_id = ws_req_id;
                                safe_copy_cstr(
                                    s_client.weather_ws_req_entity_id, sizeof(s_client.weather_ws_req_entity_id), entity_id);
                                s_client.next_priority_sync_unix_ms = now_ms + ha_client_interval_priority_step_ms(bg_budget_level);
                            } else {
                                ha_client_priority_sync_queue_push_locked(entity_id);
                                s_client.next_priority_sync_unix_ms = now_ms + HA_PRIORITY_SYNC_RETRY_INTERVAL_MS;
                            }
                            xSemaphoreGive(s_client.mutex);
                        }
                    }
                } else {
                    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                    s_client.next_priority_sync_unix_ms = now_ms + ha_client_interval_priority_step_ms(bg_budget_level);
                    xSemaphoreGive(s_client.mutex);
                }
            }
        }

        if (connected && authenticated && wifi_up && should_run_initial_layout_sync_step) {
            if (ws_priority_boost_active) {
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.next_initial_layout_sync_unix_ms = ws_priority_boost_until_unix_ms;
                xSemaphoreGive(s_client.mutex);
                vTaskDelay(HA_CLIENT_TASK_DELAY_TICKS);
                continue;
            }
            int64_t defer_ms = 0;
            if (ha_client_should_defer_bg_http(bg_budget_level, now_ms, &defer_ms)) {
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.next_initial_layout_sync_unix_ms = now_ms + defer_ms;
                xSemaphoreGive(s_client.mutex);
            } else {
                bool done = false;
                uint32_t entity_count = 0;
                uint32_t index = initial_layout_sync_index;
                uint32_t imported = initial_layout_sync_imported;
                esp_err_t sync_err =
                    ha_client_sync_layout_entity_step(true, &index, &entity_count, &imported, &done, !rest_enabled);

                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.initial_layout_sync_index = index;
                s_client.initial_layout_sync_imported = imported;
                if (done) {
                    s_client.pending_initial_layout_sync = false;
                    s_client.initial_layout_sync_done = true;
                    s_client.next_initial_layout_sync_unix_ms = 0;
                    s_client.next_periodic_layout_sync_unix_ms =
                        rest_enabled ? (now_ms + ha_client_interval_periodic_step_ms(bg_budget_level)) : 0;
                } else if (sync_err == ESP_OK || sync_err == ESP_ERR_INVALID_RESPONSE || sync_err == ESP_ERR_NOT_FOUND ||
                           sync_err == ESP_ERR_TIMEOUT) {
                    s_client.next_initial_layout_sync_unix_ms = now_ms + ha_client_interval_initial_step_ms(bg_budget_level);
                } else {
                    s_client.next_initial_layout_sync_unix_ms = now_ms + HA_INITIAL_LAYOUT_SYNC_RETRY_INTERVAL_MS;
                }
                xSemaphoreGive(s_client.mutex);

                if (done) {
                    ESP_LOGI(TAG_HA_CLIENT, "Initial layout state sync: imported %u/%u entities", (unsigned)imported,
                        (unsigned)entity_count);
                    ha_client_publish_event(EV_HA_CONNECTED, NULL);
                    if (!rest_enabled && layout_needs_weather_forecast) {
                        ha_client_queue_weather_priority_sync_from_layout(now_ms);
                    }
                }
            }
        }

        if (connected && authenticated && wifi_up && should_run_periodic_layout_sync_step) {
            if (ws_priority_boost_active) {
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.next_periodic_layout_sync_unix_ms = ws_priority_boost_until_unix_ms;
                xSemaphoreGive(s_client.mutex);
                vTaskDelay(HA_CLIENT_TASK_DELAY_TICKS);
                continue;
            }
            int64_t defer_ms = 0;
            if (ha_client_should_defer_bg_http(bg_budget_level, now_ms, &defer_ms)) {
                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.next_periodic_layout_sync_unix_ms = now_ms + defer_ms;
                xSemaphoreGive(s_client.mutex);
            } else {
                bool done = false;
                uint32_t entity_count = 0;
                uint32_t cursor = periodic_layout_sync_cursor;
                esp_err_t sync_err =
                    ha_client_sync_layout_entity_step(false, &cursor, &entity_count, NULL, &done, false);

                xSemaphoreTake(s_client.mutex, portMAX_DELAY);
                s_client.periodic_layout_sync_cursor = cursor;
                if (sync_err == ESP_OK || sync_err == ESP_ERR_INVALID_RESPONSE || sync_err == ESP_ERR_NOT_FOUND ||
                    sync_err == ESP_ERR_TIMEOUT) {
                    s_client.next_periodic_layout_sync_unix_ms = now_ms + ha_client_interval_periodic_step_ms(bg_budget_level);
                } else {
                    s_client.next_periodic_layout_sync_unix_ms = now_ms + HA_PERIODIC_LAYOUT_SYNC_RETRY_INTERVAL_MS;
                }
                xSemaphoreGive(s_client.mutex);
                (void)entity_count;
                (void)done;
            }
        }

        vTaskDelay(HA_CLIENT_TASK_DELAY_TICKS);
    }
}

static void *cjson_psram_malloc(size_t sz)
{
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) {
        p = malloc(sz);
    }
    return p;
}

static void cjson_psram_free(void *ptr)
{
    free(ptr);
}

esp_err_t ha_client_start(const ha_client_config_t *cfg)
{
    if (cfg == NULL || cfg->ws_url == NULL || cfg->access_token == NULL || cfg->ws_url[0] == '\0' ||
        cfg->access_token[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client.started) {
        return ESP_OK;
    }

    /* Route all cJSON allocations to PSRAM to keep internal RAM free for TLS. */
    static bool s_cjson_hooks_set = false;
    if (!s_cjson_hooks_set) {
        cJSON_Hooks hooks = { .malloc_fn = cjson_psram_malloc, .free_fn = cjson_psram_free };
        cJSON_InitHooks(&hooks);
        s_cjson_hooks_set = true;
    }

    if (s_client.mutex == NULL) {
        s_client.mutex = xSemaphoreCreateMutex();
        if (s_client.mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_client.ws_rx_queue == NULL) {
        s_client.ws_rx_queue = xQueueCreate(APP_HA_QUEUE_LENGTH, sizeof(ha_ws_rx_msg_t));
        if (s_client.ws_rx_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    } else {
        ha_client_flush_ws_rx_queue();
    }
    if (ha_client_ensure_ws_rx_buffer() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    if (ha_client_ensure_entities_sub_buffers() != ESP_OK) {
        if (s_ws_rx_buf != NULL) {
            heap_caps_free(s_ws_rx_buf);
            s_ws_rx_buf = NULL;
            s_ws_rx_buf_cap = 0;
        }
        return ESP_ERR_NO_MEM;
    }

    s_client.rest_enabled = cfg->rest_enabled;
    snprintf(s_client.ws_url, sizeof(s_client.ws_url), "%s", cfg->ws_url);
    snprintf(s_client.access_token, sizeof(s_client.access_token), "%s", cfg->access_token);
    s_client.next_message_id = 1;
    s_client.get_states_req_id = 0;
    s_client.authenticated = false;
    s_client.published_disconnect = false;
    s_client.pending_send_auth = false;
    s_client.pending_initial_layout_sync = false;
    s_client.pending_send_pong = false;
    s_client.pending_subscribe = false;
    s_client.pending_get_states = false;
    s_client.initial_layout_sync_done = false;
    s_client.sub_state_via_trigger = false;
    s_client.trigger_sub_req_id = 0;
    s_client.sub_state_via_entities = false;
    s_client.entities_sub_req_id = 0;
    s_client.ws_entities_subscribe_supported = true;
    s_client.entities_sub_target_count = 0;
    s_client.entities_sub_sent_count = 0;
    s_client.entities_sub_seen_count = 0;
    s_client.next_entities_subscribe_unix_ms = 0;
    ha_client_clear_entities_sub_buffers();
    s_client.pending_pong_id = 0;
    s_client.ping_inflight = false;
    s_client.ping_inflight_id = 0;
    s_client.ping_sent_unix_ms = 0;
    s_client.ping_timeout_strikes = 0;
    s_client.ws_short_session_strikes = 0;
    s_client.pending_force_wifi_recover = false;
    s_client.last_rx_unix_ms = ha_client_now_ms();
    s_client.ws_last_connected_unix_ms = 0;
    s_client.next_auth_retry_unix_ms = 0;
    s_client.next_initial_layout_sync_unix_ms = 0;
    s_client.next_periodic_layout_sync_unix_ms = ha_client_now_ms() + ha_client_interval_periodic_step_ms(s_client.bg_budget_level);
    s_client.initial_layout_sync_index = 0;
    s_client.initial_layout_sync_imported = 0;
    s_client.periodic_layout_sync_cursor = 0;
    memset(s_client.priority_sync_entities, 0, sizeof(s_client.priority_sync_entities));
    s_client.priority_sync_head = 0;
    s_client.priority_sync_tail = 0;
    s_client.priority_sync_count = 0;
    s_client.next_priority_sync_unix_ms = 0;
    s_client.ws_error_streak = 0;
    s_client.bg_budget_level = HA_BG_BUDGET_NORMAL;
    s_client.bg_budget_level_since_unix_ms = ha_client_now_ms();
    s_client.bg_budget_last_log_unix_ms = 0;
    s_client.bg_budget_level_change_count = 0;
    s_client.http_open_count_window = 0;
    s_client.http_open_fail_count_window = 0;
    s_client.http_open_fail_streak = 0;
    s_client.http_open_window_start_unix_ms = ha_client_now_ms();
    s_client.http_open_cooldown_until_unix_ms = 0;
    s_client.next_weather_forecast_retry_unix_ms = 0;
    s_client.layout_needs_weather_forecast = false;
    s_client.weather_ws_req_inflight = false;
    s_client.weather_ws_req_id = 0;
    s_client.weather_ws_req_entity_id[0] = '\0';
    s_client.layout_needs_ha_energy = ha_client_layout_needs_ha_energy();
    s_client.pending_energy_prefs = s_client.layout_needs_ha_energy;
    s_client.pending_energy_stats = false;
    s_client.energy_prefs_req_inflight = false;
    s_client.energy_stats_req_inflight = false;
    s_client.energy_prefs_req_id = 0;
    s_client.energy_stats_req_id = 0;
    s_client.next_energy_sync_unix_ms = s_client.layout_needs_ha_energy ? ha_client_now_ms() : 0;
    ha_client_energy_clear_refs_locked();
    s_client.layout_entity_signature = 0;
    s_client.layout_entity_count = 0;
    s_client.ws_priority_boost_until_unix_ms = 0;
    s_client.last_ws_bad_input_unix_ms = 0;
    s_client.ws_get_states_block_until_unix_ms = 0;
    s_client.light_discovery_requested = false;
    s_client.light_discovery_inflight = false;
    s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_NONE;
    s_client.light_discovery_domain[0] = '\0';
    s_client.light_discovery_search[0] = '\0';
    s_client.light_discovery_req_id = 0;
    s_client.light_discovery_started_unix_ms = 0;
    s_client.light_discovery_updated_unix_ms = 0;
    s_client.light_discovery_next_step_unix_ms = 0;
    s_client.light_discovery_last_wait_log_unix_ms = 0;
    s_client.light_discovery_template_offset = 0;
    s_client.light_discovery_template_total = 0;
    s_client.light_discovery_count = 0;
    s_client.light_discovery_truncated = false;
    s_client.light_discovery_pending_count = 0;
    s_client.light_discovery_pending_truncated = false;
    s_client.light_discovery_area_count = 0;
    s_client.light_discovery_area_truncated = false;
    s_client.light_discovery_device_count = 0;
    s_client.light_discovery_device_truncated = false;
    if (s_client.light_discovery_items != NULL) {
        heap_caps_free(s_client.light_discovery_items);
        s_client.light_discovery_items = NULL;
    }
    ha_client_light_discovery_free_work_buffers_locked();
    memset(s_client.service_traces, 0, sizeof(s_client.service_traces));
    s_client.http_resolved_host[0] = '\0';
    s_client.http_resolved_ip[0] = '\0';
    ha_client_reset_http_client();
    ha_client_refresh_layout_capabilities();
    ESP_LOGI(TAG_HA_CLIENT, "HA REST fallback: %s", s_client.rest_enabled ? "enabled" : "disabled (WS-only)");

    int64_t effective_ping_ms = ha_client_ping_interval_ms_effective();
    if (effective_ping_ms != (int64_t)APP_HA_PING_INTERVAL_MS) {
        ESP_LOGW(TAG_HA_CLIENT, "Configured HA ping interval %d ms too low, clamped to %" PRId64 " ms",
            APP_HA_PING_INTERVAL_MS, effective_ping_ms);
    }

    BaseType_t created = pdFAIL;
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
    created = xTaskCreatePinnedToCore(
        ha_client_task, "ha_client", APP_HA_TASK_STACK, NULL, APP_HA_TASK_PRIO, &s_client.task_handle, 0);
#else
    created = xTaskCreate(
        ha_client_task, "ha_client", APP_HA_TASK_STACK, NULL, APP_HA_TASK_PRIO, &s_client.task_handle);
#endif
    if (created != pdPASS) {
        if (s_ws_rx_buf != NULL) {
            heap_caps_free(s_ws_rx_buf);
            s_ws_rx_buf = NULL;
            s_ws_rx_buf_cap = 0;
        }
        ha_client_free_entities_sub_buffers();
        return ESP_FAIL;
    }

    ha_ws_config_t ws_cfg = {
        .uri = s_client.ws_url,
        .event_cb = ha_client_ws_event_cb,
        .user_ctx = NULL,
    };
    ha_client_log_mem_snapshot("ws_start_initial", false);
    esp_err_t err = ha_ws_start(&ws_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_HA_CLIENT, "Initial websocket start deferred: %s", esp_err_to_name(err));
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        if (s_client.ws_error_streak < UINT32_MAX) {
            s_client.ws_error_streak++;
        }
        xSemaphoreGive(s_client.mutex);
    }

    s_client.started = true;
    return ESP_OK;
}

void ha_client_stop(void)
{
    if (!s_client.started) {
        return;
    }
    if (s_client.task_handle != NULL) {
        vTaskDelete(s_client.task_handle);
        s_client.task_handle = NULL;
    }
    ha_ws_stop();
    ha_client_reset_http_client();
    ha_client_flush_ws_rx_queue();
    if (s_client.ws_rx_queue != NULL) {
        vQueueDelete(s_client.ws_rx_queue);
        s_client.ws_rx_queue = NULL;
    }
    if (s_ws_rx_buf != NULL) {
        heap_caps_free(s_ws_rx_buf);
        s_ws_rx_buf = NULL;
        s_ws_rx_buf_cap = 0;
    }
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    s_client.started = false;
    s_client.rest_enabled = false;
    s_client.authenticated = false;
    s_client.pending_send_auth = false;
    s_client.pending_initial_layout_sync = false;
    s_client.pending_send_pong = false;
    s_client.pending_subscribe = false;
    s_client.pending_get_states = false;
    s_client.get_states_req_id = 0;
    s_client.initial_layout_sync_done = false;
    s_client.sub_state_via_trigger = false;
    s_client.trigger_sub_req_id = 0;
    s_client.sub_state_via_entities = false;
    s_client.entities_sub_req_id = 0;
    s_client.ws_entities_subscribe_supported = true;
    s_client.entities_sub_target_count = 0;
    s_client.entities_sub_sent_count = 0;
    s_client.entities_sub_seen_count = 0;
    s_client.next_entities_subscribe_unix_ms = 0;
    ha_client_clear_entities_sub_buffers();
    s_client.pending_pong_id = 0;
    s_client.ping_inflight = false;
    s_client.ping_inflight_id = 0;
    s_client.ping_sent_unix_ms = 0;
    s_client.ping_timeout_strikes = 0;
    s_client.ws_short_session_strikes = 0;
    s_client.pending_force_wifi_recover = false;
    s_client.last_rx_unix_ms = 0;
    s_client.ws_last_connected_unix_ms = 0;
    s_client.next_auth_retry_unix_ms = 0;
    s_client.next_initial_layout_sync_unix_ms = 0;
    s_client.next_periodic_layout_sync_unix_ms = 0;
    s_client.initial_layout_sync_index = 0;
    s_client.initial_layout_sync_imported = 0;
    s_client.periodic_layout_sync_cursor = 0;
    memset(s_client.priority_sync_entities, 0, sizeof(s_client.priority_sync_entities));
    s_client.priority_sync_head = 0;
    s_client.priority_sync_tail = 0;
    s_client.priority_sync_count = 0;
    s_client.next_priority_sync_unix_ms = 0;
    s_client.ws_error_streak = 0;
    s_client.bg_budget_level = HA_BG_BUDGET_NORMAL;
    s_client.bg_budget_level_since_unix_ms = 0;
    s_client.bg_budget_last_log_unix_ms = 0;
    s_client.bg_budget_level_change_count = 0;
    s_client.http_open_count_window = 0;
    s_client.http_open_fail_count_window = 0;
    s_client.http_open_fail_streak = 0;
    s_client.http_open_window_start_unix_ms = 0;
    s_client.http_open_cooldown_until_unix_ms = 0;
    s_client.next_weather_forecast_retry_unix_ms = 0;
    s_client.layout_needs_weather_forecast = false;
    s_client.weather_ws_req_inflight = false;
    s_client.weather_ws_req_id = 0;
    s_client.weather_ws_req_entity_id[0] = '\0';
    s_client.layout_needs_ha_energy = false;
    s_client.pending_energy_prefs = false;
    s_client.pending_energy_stats = false;
    s_client.energy_prefs_req_inflight = false;
    s_client.energy_stats_req_inflight = false;
    s_client.energy_prefs_req_id = 0;
    s_client.energy_stats_req_id = 0;
    s_client.next_energy_sync_unix_ms = 0;
    ha_client_energy_clear_refs_locked();
    s_client.layout_entity_signature = 0;
    s_client.layout_entity_count = 0;
    s_client.ws_priority_boost_until_unix_ms = 0;
    s_client.last_ws_bad_input_unix_ms = 0;
    s_client.ws_get_states_block_until_unix_ms = 0;
    s_client.light_discovery_requested = false;
    s_client.light_discovery_inflight = false;
    s_client.light_discovery_phase = HA_LIGHT_DISCOVERY_PHASE_NONE;
    s_client.light_discovery_domain[0] = '\0';
    s_client.light_discovery_search[0] = '\0';
    s_client.light_discovery_req_id = 0;
    s_client.light_discovery_started_unix_ms = 0;
    s_client.light_discovery_updated_unix_ms = 0;
    s_client.light_discovery_next_step_unix_ms = 0;
    s_client.light_discovery_last_wait_log_unix_ms = 0;
    s_client.light_discovery_template_offset = 0;
    s_client.light_discovery_template_total = 0;
    s_client.light_discovery_count = 0;
    s_client.light_discovery_truncated = false;
    s_client.light_discovery_pending_count = 0;
    s_client.light_discovery_pending_truncated = false;
    s_client.light_discovery_area_count = 0;
    s_client.light_discovery_area_truncated = false;
    s_client.light_discovery_device_count = 0;
    s_client.light_discovery_device_truncated = false;
    if (s_client.light_discovery_items != NULL) {
        heap_caps_free(s_client.light_discovery_items);
        s_client.light_discovery_items = NULL;
    }
    ha_client_light_discovery_free_work_buffers_locked();
    memset(s_client.service_traces, 0, sizeof(s_client.service_traces));
    s_client.http_resolved_host[0] = '\0';
    s_client.http_resolved_ip[0] = '\0';
    xSemaphoreGive(s_client.mutex);
    ha_client_free_entities_sub_buffers();
}

esp_err_t ha_client_notify_layout_updated(void)
{
    if (s_client.mutex == NULL) {
        return ESP_OK;
    }

    int64_t now_ms = ha_client_now_ms();
    uint32_t new_signature = 0;
    uint16_t new_count = 0;
    bool new_need_weather_forecast = false;
    bool new_need_ha_energy = ha_client_layout_needs_ha_energy();
    bool has_snapshot = ha_client_capture_layout_snapshot(&new_signature, &new_count, &new_need_weather_forecast);
    bool started = false;
    bool scheduled_resync = false;
    bool scheduled_resubscribe = false;
    bool entity_set_changed = false;
    bool forecast_capability_changed = false;
    bool energy_capability_changed = false;
    bool reset_energy_model = false;
    bool rest_enabled = false;
    bool ws_entities_stream = false;
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    started = s_client.started;
    rest_enabled = s_client.rest_enabled;
    ws_entities_stream = (!rest_enabled && HA_USE_WS_ENTITIES_SUBSCRIPTION && s_client.ws_entities_subscribe_supported);
    if (has_snapshot) {
        entity_set_changed =
            (s_client.layout_entity_signature != new_signature) || (s_client.layout_entity_count != new_count);
        forecast_capability_changed = (s_client.layout_needs_weather_forecast != new_need_weather_forecast);
        energy_capability_changed = (s_client.layout_needs_ha_energy != new_need_ha_energy);
        s_client.layout_entity_signature = new_signature;
        s_client.layout_entity_count = new_count;
        s_client.layout_needs_weather_forecast = new_need_weather_forecast;
        s_client.layout_needs_ha_energy = new_need_ha_energy;
        if (energy_capability_changed || (new_need_ha_energy && s_client.next_energy_sync_unix_ms == 0)) {
            s_client.pending_energy_prefs = new_need_ha_energy;
            s_client.pending_energy_stats = false;
            s_client.energy_prefs_req_inflight = false;
            s_client.energy_stats_req_inflight = false;
            s_client.energy_prefs_req_id = 0;
            s_client.energy_stats_req_id = 0;
            s_client.next_energy_sync_unix_ms = new_need_ha_energy ? now_ms : 0;
            if (!new_need_ha_energy) {
                ha_client_energy_clear_refs_locked();
                reset_energy_model = true;
            }
        }
    }

    if (started && entity_set_changed) {
        s_client.initial_layout_sync_index = 0;
        s_client.initial_layout_sync_imported = 0;
        s_client.get_states_req_id = 0;
        if (rest_enabled) {
            s_client.initial_layout_sync_done = false;
            s_client.pending_get_states = false;
            s_client.pending_initial_layout_sync = APP_HA_FETCH_INITIAL_STATES;
            s_client.next_initial_layout_sync_unix_ms =
                APP_HA_FETCH_INITIAL_STATES ? (now_ms + ha_client_interval_initial_step_ms(s_client.bg_budget_level)) : 0;
            s_client.periodic_layout_sync_cursor = 0;
            s_client.next_periodic_layout_sync_unix_ms = now_ms + ha_client_interval_periodic_step_ms(s_client.bg_budget_level);
            scheduled_resync = APP_HA_FETCH_INITIAL_STATES;
        } else if (ws_entities_stream) {
            size_t max_entities = (size_t)HA_LAYOUT_ENTITY_MAX;
            char *entity_ids = calloc(max_entities, APP_MAX_ENTITY_ID_LEN);
            size_t entity_count = 0;
            if (entity_ids != NULL) {
                bool need_weather_forecast = false;
                entity_count = ha_client_collect_layout_entity_ids(entity_ids, max_entities, &need_weather_forecast);
                s_client.layout_needs_weather_forecast = need_weather_forecast;
            }
            uint16_t target_count =
                (entity_count > HA_WS_ENTITIES_SUB_MAX) ? HA_WS_ENTITIES_SUB_MAX : (uint16_t)entity_count;
            ha_client_clear_entities_sub_buffers();
            for (uint16_t i = 0; i < target_count; i++) {
                char *target = ha_client_entities_sub_target_at(i);
                if (target != NULL) {
                    safe_copy_cstr(target, APP_MAX_ENTITY_ID_LEN, entity_ids + ((size_t)i * APP_MAX_ENTITY_ID_LEN));
                }
            }
            s_client.entities_sub_target_count = target_count;
            s_client.entities_sub_sent_count = 0;
            s_client.entities_sub_seen_count = 0;
            s_client.next_entities_subscribe_unix_ms = now_ms;
            s_client.sub_state_via_entities = false;
            s_client.entities_sub_req_id = 0;
            if (entity_ids != NULL) {
                free(entity_ids);
            }
            s_client.pending_initial_layout_sync = false;
            s_client.next_initial_layout_sync_unix_ms = 0;
            s_client.next_periodic_layout_sync_unix_ms = 0;
            s_client.initial_layout_sync_done = false;
            s_client.pending_get_states = false;
            s_client.get_states_req_id = 0;
            s_client.pending_subscribe = APP_HA_SUBSCRIBE_STATE_CHANGED && (target_count > 0);
            if (target_count == 0) {
                s_client.initial_layout_sync_done = true;
                s_client.initial_layout_sync_imported = 0;
            }
            scheduled_resync = APP_HA_FETCH_INITIAL_STATES;
        } else {
            s_client.pending_initial_layout_sync = false;
            int64_t next_allowed = now_ms + HA_WS_GET_STATES_POST_SUBSCRIBE_DELAY_MS;
            if (s_client.ws_get_states_block_until_unix_ms > next_allowed) {
                next_allowed = s_client.ws_get_states_block_until_unix_ms;
            }
            s_client.next_initial_layout_sync_unix_ms = next_allowed;
            s_client.next_periodic_layout_sync_unix_ms = 0;
            s_client.initial_layout_sync_done = false;
            s_client.pending_get_states = false;
            s_client.get_states_req_id = 0;
            scheduled_resync = APP_HA_FETCH_INITIAL_STATES;
        }

        memset(s_client.priority_sync_entities, 0, sizeof(s_client.priority_sync_entities));
        s_client.priority_sync_head = 0;
        s_client.priority_sync_tail = 0;
        s_client.priority_sync_count = 0;
        s_client.next_priority_sync_unix_ms =
            rest_enabled ? now_ms : (now_ms + HA_WS_WEATHER_PRIORITY_GRACE_MS);
        memset(s_client.service_traces, 0, sizeof(s_client.service_traces));

        if (APP_HA_SUBSCRIBE_STATE_CHANGED) {
            if (!ws_entities_stream) {
                s_client.pending_subscribe = true;
            }
            s_client.sub_state_via_trigger = false;
            s_client.trigger_sub_req_id = 0;
            if (!ws_entities_stream) {
                s_client.sub_state_via_entities = false;
                s_client.entities_sub_req_id = 0;
            }
            scheduled_resubscribe = true;
        }
    }
    xSemaphoreGive(s_client.mutex);

    if (reset_energy_model) {
        ha_energy_model_reset();
        ha_client_publish_event(EV_HA_ENERGY_CHANGED, NULL);
    }

    if (started) {
        if (!has_snapshot) {
            ESP_LOGW(TAG_HA_CLIENT, "Layout updated: snapshot failed, keeping current HA subscriptions/sync state");
        } else if (scheduled_resubscribe || scheduled_resync) {
            if (rest_enabled) {
                ESP_LOGI(TAG_HA_CLIENT, "Layout updated: scheduled immediate HA resubscribe/resync");
            } else {
                ESP_LOGI(TAG_HA_CLIENT,
                    "Layout updated: scheduled immediate HA resubscribe/WS state sync (WS-only runtime)");
            }
        } else if (forecast_capability_changed) {
            ESP_LOGI(TAG_HA_CLIENT, "Layout updated: weather forecast capability changed, keeping current subscriptions");
        } else if (energy_capability_changed) {
            ESP_LOGI(TAG_HA_CLIENT, "Layout updated: HA energy capability changed, scheduled energy sync");
        } else {
            ESP_LOGI(TAG_HA_CLIENT, "Layout updated: entity set unchanged, skipping HA resubscribe/resync");
        }
    }
    return ESP_OK;
}

esp_err_t ha_client_request_energy_refresh(void)
{
    if (s_client.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int64_t now_ms = ha_client_now_ms();
    bool scheduled = false;
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    if (s_client.started && s_client.authenticated) {
        s_client.layout_needs_ha_energy = true;
        if (!s_client.pending_energy_prefs && !s_client.pending_energy_stats &&
            !s_client.energy_prefs_req_inflight && !s_client.energy_stats_req_inflight &&
            (s_client.next_energy_sync_unix_ms == 0 || s_client.next_energy_sync_unix_ms <= now_ms)) {
            s_client.pending_energy_prefs = true;
            s_client.next_energy_sync_unix_ms = now_ms;
            scheduled = true;
        } else {
            scheduled = true;
        }
    }
    xSemaphoreGive(s_client.mutex);
    return scheduled ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool ha_client_is_connected(void)
{
    bool authenticated = false;
    if (s_client.mutex != NULL) {
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        authenticated = s_client.authenticated;
        xSemaphoreGive(s_client.mutex);
    }
    return ha_ws_is_connected() && authenticated;
}

bool ha_client_is_initial_sync_done(void)
{
    if (!APP_HA_FETCH_INITIAL_STATES) {
        return true;
    }

    bool done = false;
    if (s_client.mutex != NULL) {
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        done = s_client.initial_layout_sync_done;
        xSemaphoreGive(s_client.mutex);
    }
    return done;
}

esp_err_t ha_client_get_domain_entities_json(const char *domain, const char *search, bool refresh, char **out_json)
{
    if (out_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    const char *checked_domain = ha_client_discovery_domain_or_default(domain);
    char checked_search[APP_HA_DISCOVERY_SEARCH_MAX_LEN] = {0};
    ha_client_discovery_normalize_search(search, checked_search, sizeof(checked_search));
    const bool connected = ha_client_is_connected();
    const int64_t now_ms = ha_client_now_ms();

    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();
    if (root == NULL || items == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(items);
        return ESP_ERR_NO_MEM;
    }

    if (s_client.mutex == NULL) {
        cJSON_AddStringToObject(root, "status", "disconnected");
        cJSON_AddBoolToObject(root, "connected", false);
        cJSON_AddBoolToObject(root, "pending", false);
        cJSON_AddStringToObject(root, "domain", checked_domain);
        cJSON_AddStringToObject(root, "search", checked_search);
        cJSON_AddStringToObject(root, "phase", "idle");
        cJSON_AddBoolToObject(root, "truncated", false);
        cJSON_AddNumberToObject(root, "count", 0);
        cJSON_AddNumberToObject(root, "updated_ms", 0);
        cJSON_AddNumberToObject(root, "age_ms", 0);
        cJSON_AddItemToObject(root, "items", items);
        char *payload = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (payload == NULL) {
            return ESP_ERR_NO_MEM;
        }
        *out_json = payload;
        return ESP_OK;
    }

    bool has_cache_before = false;
    bool pending_before = false;
    xSemaphoreTake(s_client.mutex, portMAX_DELAY);
    bool same_domain_before =
        strncmp(s_client.light_discovery_domain, checked_domain, sizeof(s_client.light_discovery_domain)) == 0 &&
        strncmp(s_client.light_discovery_search, checked_search, sizeof(s_client.light_discovery_search)) == 0;
    has_cache_before = same_domain_before && ha_client_light_discovery_has_ready_cache_locked();
    pending_before = same_domain_before && (s_client.light_discovery_requested || s_client.light_discovery_inflight);
    xSemaphoreGive(s_client.mutex);

    bool has_cache = false;
    bool pending = false;
    if (APP_HA_LIGHT_DISCOVERY_TEMPLATE_ENABLED || APP_HA_LIGHT_DISCOVERY_REGISTRY_ENABLED) {
        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        bool same_domain =
            strncmp(s_client.light_discovery_domain, checked_domain, sizeof(s_client.light_discovery_domain)) == 0 &&
            strncmp(s_client.light_discovery_search, checked_search, sizeof(s_client.light_discovery_search)) == 0;
        bool pending_any = s_client.light_discovery_requested || s_client.light_discovery_inflight;
        if (connected && pending_any && !same_domain) {
            ha_client_light_discovery_abort_locked();
            pending_any = false;
            same_domain = false;
        }
        has_cache = same_domain && ha_client_light_discovery_has_ready_cache_locked();
        pending = same_domain && pending_any;
        if (connected && !pending_any && (refresh || !has_cache)) {
            if (ha_client_light_discovery_start_locked(checked_domain, checked_search, now_ms)) {
                pending = true;
            }
        }
    } else {
        if (connected && (refresh || !has_cache_before || pending_before)) {
            (void)ha_client_light_discovery_refresh_from_model(checked_domain, checked_search, now_ms);
        }

        xSemaphoreTake(s_client.mutex, portMAX_DELAY);
        has_cache = strncmp(s_client.light_discovery_domain, checked_domain,
                        sizeof(s_client.light_discovery_domain)) == 0 &&
            strncmp(s_client.light_discovery_search, checked_search, sizeof(s_client.light_discovery_search)) == 0 &&
            ha_client_light_discovery_has_ready_cache_locked();
        pending = false;
    }

    const char *status = "empty";
    if (has_cache && pending) {
        status = "refreshing";
    } else if (has_cache) {
        status = "ready";
    } else if (pending) {
        status = "pending";
    } else if (!connected) {
        status = "disconnected";
    }

    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddBoolToObject(root, "connected", connected);
    cJSON_AddBoolToObject(root, "pending", pending);
    cJSON_AddStringToObject(root, "domain", checked_domain);
    cJSON_AddStringToObject(root, "search", checked_search);
    cJSON_AddStringToObject(root, "phase", ha_client_light_discovery_phase_name(s_client.light_discovery_phase));
    cJSON_AddBoolToObject(root, "truncated", has_cache ? s_client.light_discovery_truncated : false);
    cJSON_AddNumberToObject(root, "count", has_cache ? (double)s_client.light_discovery_count : 0.0);
    cJSON_AddNumberToObject(root, "loaded",
        has_cache ? (double)s_client.light_discovery_count : (pending ? (double)s_client.light_discovery_pending_count : 0.0));
    cJSON_AddNumberToObject(root, "total",
        pending ? (double)s_client.light_discovery_template_total :
            (has_cache ? (double)s_client.light_discovery_template_total : 0.0));
    cJSON_AddNumberToObject(root, "limit", (double)APP_HA_LIGHT_DISCOVERY_MAX_ITEMS);
    cJSON_AddNumberToObject(root, "page_size", (double)APP_HA_LIGHT_DISCOVERY_PAGE_SIZE);
    cJSON_AddNumberToObject(root, "updated_ms", has_cache ? (double)s_client.light_discovery_updated_unix_ms : 0.0);
    cJSON_AddNumberToObject(root, "age_ms",
        (has_cache && s_client.light_discovery_updated_unix_ms > 0 && now_ms >= s_client.light_discovery_updated_unix_ms)
            ? (double)(now_ms - s_client.light_discovery_updated_unix_ms)
            : 0.0);

    if (has_cache) {
        for (uint16_t i = 0; i < s_client.light_discovery_count; i++) {
            const ha_light_discovery_item_t *src = &s_client.light_discovery_items[i];
            cJSON *item = cJSON_CreateObject();
            if (item == NULL) {
                continue;
            }
            cJSON_AddStringToObject(item, "id", src->id);
            cJSON_AddStringToObject(item, "name", src->name);
            cJSON_AddStringToObject(item, "room", src->room);
            cJSON_AddStringToObject(item, "area_id", src->area_id);
            cJSON_AddStringToObject(item, "icon", src->icon);
            cJSON_AddItemToArray(items, item);
        }
    }
    xSemaphoreGive(s_client.mutex);

    cJSON_AddItemToObject(root, "items", items);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }

    *out_json = payload;
    return ESP_OK;
}

esp_err_t ha_client_call_service(const char *domain, const char *service, const char *json_service_data)
{
    if (domain == NULL || service == NULL || domain[0] == '\0' || service[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ha_client_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    ha_client_mark_ws_priority_boost(ha_client_now_ms());

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint32_t req_id = ha_client_next_message_id();
    cJSON_AddNumberToObject(root, "id", (double)req_id);
    cJSON_AddStringToObject(root, "type", "call_service");
    cJSON_AddStringToObject(root, "domain", domain);
    cJSON_AddStringToObject(root, "service", service);

    cJSON *service_data_obj = NULL;
    char trace_entity_id[APP_MAX_ENTITY_ID_LEN] = {0};
    char current_entity_state[APP_MAX_STATE_LEN] = {0};
    if (json_service_data != NULL && json_service_data[0] != '\0') {
        cJSON *service_data = cJSON_Parse(json_service_data);
        if (service_data != NULL && cJSON_IsObject(service_data)) {
            service_data_obj = service_data;
            cJSON_AddItemToObject(root, "service_data", service_data);
        } else {
            if (service_data != NULL) {
                cJSON_Delete(service_data);
            }
            service_data_obj = cJSON_CreateObject();
            if (service_data_obj == NULL) {
                cJSON_Delete(root);
                return ESP_ERR_NO_MEM;
            }
            cJSON_AddItemToObject(root, "service_data", service_data_obj);
        }
    } else {
        service_data_obj = cJSON_CreateObject();
        if (service_data_obj == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddItemToObject(root, "service_data", service_data_obj);
    }

    if (cJSON_IsObject(service_data_obj)) {
        cJSON *entity_id = cJSON_GetObjectItemCaseSensitive(service_data_obj, "entity_id");
        if (cJSON_IsString(entity_id) && entity_id->valuestring != NULL) {
            safe_copy_cstr(trace_entity_id, sizeof(trace_entity_id), entity_id->valuestring);
            ha_state_t current_state = {0};
            if (ha_model_get_state(trace_entity_id, &current_state)) {
                safe_copy_cstr(current_entity_state, sizeof(current_entity_state), current_state.state);
            }
        }
    }

#if APP_HA_ROUTE_TRACE_LOG
    if (strcmp(domain, "light") == 0) {
        char transition_text[24] = "none";
        char brightness_text[24] = "none";
        char brightness_pct_text[24] = "none";
        if (cJSON_IsObject(service_data_obj)) {
            cJSON *transition = cJSON_GetObjectItemCaseSensitive(service_data_obj, "transition");
            cJSON *brightness = cJSON_GetObjectItemCaseSensitive(service_data_obj, "brightness");
            cJSON *brightness_pct = cJSON_GetObjectItemCaseSensitive(service_data_obj, "brightness_pct");
            if (cJSON_IsNumber(transition)) {
                snprintf(transition_text, sizeof(transition_text), "%.2f", transition->valuedouble);
            }
            if (cJSON_IsNumber(brightness)) {
                snprintf(brightness_text, sizeof(brightness_text), "%.0f", brightness->valuedouble);
            }
            if (cJSON_IsNumber(brightness_pct)) {
                snprintf(brightness_pct_text, sizeof(brightness_pct_text), "%.0f", brightness_pct->valuedouble);
            }
        }
        ESP_LOGI(TAG_HA_CLIENT,
            "svc payload %s.%s entity=%s transition=%s brightness=%s brightness_pct=%s",
            domain,
            service,
            (trace_entity_id[0] != '\0') ? trace_entity_id : "?",
            transition_text,
            brightness_text,
            brightness_pct_text);
    }
#endif

    const char *expected_state =
        ha_client_expected_state_from_service(service, trace_entity_id, current_entity_state);
    ha_client_trace_service_queued(req_id, domain, service, trace_entity_id, expected_state);
    esp_err_t err = ha_client_send_json(root);
    ha_client_trace_service_sent(req_id, err);
    cJSON_Delete(root);
    return err;
}
