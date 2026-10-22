/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/net_watchdog_esp.h"

#include <stdint.h>

#include "esp_attr.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

#include "app_config.h"
#include "ha/ha_client.h"
#include "net/c6_memory.h"
#include "net/wifi_mgr.h"
#include "util/log_tags.h"
#include "util/net_watchdog.h"
#include "util/reboot_log_esp.h"

#define NET_WD_POLL_MS 30000
#define NET_WD_PING_EVERY_S 60
#define NET_WD_TASK_STACK 6144
#define NET_WD_RTC_MAGIC 0x4E455744u

typedef struct {
    uint32_t magic;
    uint32_t restarts_in_a_row;
} net_wd_rtc_t;

static RTC_NOINIT_ATTR net_wd_rtc_t s_rtc;
static net_wd_state_t s_state;
static bool s_wifi_expected;
static bool s_ha_expected;
static volatile int s_ping_result = -1; /* 1 answered, 0 timed out, -1 none yet */
static volatile bool s_ping_done;
static bool s_ping_ever_ok;
static int64_t s_last_ping_s = -1000;
static net_wd_decision_t s_last_decision = NET_WD_OK;
static int s_http_result = -1;

static void ping_success(esp_ping_handle_t hdl, void *args)
{
    (void)hdl;
    (void)args;
    s_ping_result = 1;
    s_ping_ever_ok = true;
}

static void ping_timeout(esp_ping_handle_t hdl, void *args)
{
    (void)hdl;
    (void)args;
    s_ping_result = 0;
}

static void ping_end(esp_ping_handle_t hdl, void *args)
{
    (void)hdl;
    (void)args;
    s_ping_done = true;
}

/* One echo request to the default gateway. Some routers never answer; the watchdog only reacts once one did. */
static void ping_gateway(void)
{
    uint32_t gateway = 0;
    if (!wifi_mgr_get_sta_gateway(&gateway)) {
        s_ping_result = -1;
        return;
    }
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = 2;
    config.interval_ms = 500;
    config.timeout_ms = 2000;
    config.data_size = 16;
    config.task_stack_size = 3072;
    ip_addr_set_ip4_u32(&config.target_addr, gateway);
    const esp_ping_callbacks_t callbacks = {.on_ping_success = ping_success, .on_ping_timeout = ping_timeout,
                                            .on_ping_end = ping_end};
    esp_ping_handle_t session = NULL;
    s_ping_done = false;
    s_ping_result = 0; /* stays 0 unless a reply arrives */
    if (esp_ping_new_session(&config, &callbacks, &session) != ESP_OK) {
        s_ping_result = -1;
        return;
    }
    esp_ping_start(session);
    for (int i = 0; i < 100 && !s_ping_done; i++) vTaskDelay(pdMS_TO_TICKS(100)); /* up to 10 s */
    esp_ping_stop(session);
    esp_ping_delete_session(session);
}

/* A request to the panel's own HTTP server over loopback: lwIP only, no SDIO, so it tells a hung web server from a dead link. */
static int http_self_probe(void)
{
    char url[48];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/api/version", APP_HTTP_PORT);
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = 4000};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) return -1;
    const int ok = (esp_http_client_perform(client) == ESP_OK && esp_http_client_get_status_code(client) == 200) ? 1 : 0;
    esp_http_client_cleanup(client);
    return ok;
}

static void net_watchdog_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(NET_WD_POLL_MS));
        const int64_t now_s = esp_timer_get_time() / 1000000LL;
        const bool link_up = wifi_mgr_is_connected();
        if (link_up && now_s - s_last_ping_s >= NET_WD_PING_EVERY_S) {
            ping_gateway();
            s_last_ping_s = now_s;
        }
        s_http_result = http_self_probe();
        const net_wd_input_t input = {
            .wifi_expected = s_wifi_expected && !wifi_mgr_is_setup_ap_active(),
            .link_up = link_up,
            .ha_connected = s_ha_expected && ha_client_is_connected(),
            .ping = link_up ? s_ping_result : -1,
            .ping_ever_ok = s_ping_ever_ok,
            .http = s_http_result,
            .restarts_in_a_row = s_rtc.restarts_in_a_row,
        };
        s_last_decision = net_wd_step(&s_state, &input, now_s);
        if (net_wd_may_clear_restarts(&s_state, now_s)) s_rtc.restarts_in_a_row = 0;
        if (s_last_decision == NET_WD_SUPPRESSED) {
            ESP_LOGW(TAG_WIFI, "network watchdog: a restart is due but %u already happened in a row; waiting for the network",
                     (unsigned)s_rtc.restarts_in_a_row);
        } else if (s_last_decision != NET_WD_OK) {
            const bool http = s_last_decision == NET_WD_RESTART_HTTP;
            const int64_t since = http ? s_state.http_fail_since
                                  : (s_last_decision == NET_WD_RESTART_LINK ? s_state.link_down_since : s_state.ping_fail_since);
            ESP_LOGE(TAG_WIFI, "network watchdog: %s for %d s, restarting (restart %u in a row)",
                     http ? "the HTTP server does not answer"
                          : (s_last_decision == NET_WD_RESTART_LINK ? "no Wi-Fi link" : "gateway silent, no Home Assistant traffic"),
                     (int)(now_s - since), (unsigned)s_rtc.restarts_in_a_row + 1U);
            s_rtc.restarts_in_a_row++;
            const uint8_t cause = http ? REBOOT_CAUSE_HTTP_WATCHDOG : REBOOT_CAUSE_NET_WATCHDOG;
            /* the C6 memory at the restart: a silent network with the link up records no disconnect incident */
            if (!c6_memory_retain_for_restart(cause, 1000)) ESP_LOGW(TAG_WIFI, "network watchdog: no C6 memory sample retained");
            reboot_log_mark(cause);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
    }
}

void net_watchdog_start(bool wifi_configured, bool ha_expected)
{
    const esp_reset_reason_t reason = esp_reset_reason();
    if (s_rtc.magic != NET_WD_RTC_MAGIC || reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT) {
        s_rtc.magic = NET_WD_RTC_MAGIC;
        s_rtc.restarts_in_a_row = 0;
    }
    s_wifi_expected = wifi_configured;
    s_ha_expected = ha_expected;
    net_wd_init(&s_state);
    if (xTaskCreateWithCaps(net_watchdog_task, "net_watchdog", NET_WD_TASK_STACK, NULL, 2, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGW(TAG_WIFI, "network watchdog not started");
    }
}

void net_watchdog_add_json(cJSON *root)
{
    const int64_t now_s = esp_timer_get_time() / 1000000LL;
    cJSON *wd = cJSON_AddObjectToObject(root, "net_watchdog");
    if (wd == NULL) return;
    cJSON_AddBoolToObject(wd, "active", s_wifi_expected);
    cJSON_AddNumberToObject(wd, "link_down_s", s_state.link_down_since >= 0 ? (double)(now_s - s_state.link_down_since) : 0.0);
    cJSON_AddNumberToObject(wd, "gateway_silent_s", s_state.ping_fail_since >= 0 ? (double)(now_s - s_state.ping_fail_since) : 0.0);
    cJSON_AddNumberToObject(wd, "last_ping", (double)s_ping_result);
    cJSON_AddNumberToObject(wd, "http_self_probe", (double)s_http_result);
    cJSON_AddNumberToObject(wd, "http_fail_s", s_state.http_fail_since >= 0 ? (double)(now_s - s_state.http_fail_since) : 0.0);
    cJSON_AddBoolToObject(wd, "gateway_answers_pings", s_ping_ever_ok);
    cJSON_AddNumberToObject(wd, "restarts_in_a_row", (double)s_rtc.restarts_in_a_row);
    cJSON_AddNumberToObject(wd, "last_decision", (double)s_last_decision);
}
