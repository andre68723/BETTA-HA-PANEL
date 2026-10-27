/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The top bar's connectivity popups (Wi-Fi card, Home Assistant / MQTT / firmware card), moved out of
 * ui_pages.c. See ui_conn_popup.h.
 */
#include "ui/ui_conn_popup.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "app_config.h"
#include "build_stamp.h"
#include "component_versions.h"
#include "ha/ha_client.h"
#include "ha/ha_auth_state.h"
#include "net/mqtt_mgr.h"
#include "net/update_check.h"
#include "net/wifi_mgr.h"
#include "settings/runtime_settings.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_i18n.h"
#include "ui/ui_popup.h"

static const char *wifi_info_signal_quality(int8_t rssi)
{
    if (rssi >= -50) {
        return ui_i18n_get("wifi.info.signal_excellent", "Excellent");
    }
    if (rssi >= -60) {
        return ui_i18n_get("wifi.info.signal_good", "Good");
    }
    if (rssi >= -70) {
        return ui_i18n_get("wifi.info.signal_fair", "Fair");
    }
    return ui_i18n_get("wifi.info.signal_weak", "Weak");
}

static void wifi_info_popup_close_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(event);
    if (overlay != NULL) {
        lv_obj_del(overlay);
    }
}

static void wifi_info_popup_overlay_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    if (lv_event_get_target(event) == lv_event_get_current_target(event)) {
        lv_obj_del((lv_obj_t *)lv_event_get_current_target(event));
    }
}

/* Last HA status pushed through ui_pages_set_topbar_status(), so the
 * connectivity popup can show it without reaching into ha_client. */
static bool s_topbar_api_connected = false;
static bool s_topbar_api_sync_done = false;

typedef struct {
    const char *label;   /* section header when value is NULL */
    char value[160];
    bool header;
} conn_popup_row_t;

#define CONN_POPUP_MAX_ROWS 14

static void conn_popup_row_text(const conn_popup_row_t *row, char *out, size_t size)
{
    if (row->header) snprintf(out, size, "%s", row->label);
    else snprintf(out, size, "%s: %s", row->label, row->value[0] != '\0' ? row->value : "-");
}

static lv_obj_t *conn_popup_add_row(lv_obj_t *body, int width, const conn_popup_row_t *row)
{
    lv_obj_t *lbl = lv_label_create(body);
    char buf[200];
    conn_popup_row_text(row, buf, sizeof(buf));
    lv_label_set_text(lbl, buf);
    ui_text_set(lbl, UI_TEXT_LABEL, true);
    lv_obj_set_style_text_color(lbl, lv_color_hex(row->header ? APP_UI_COLOR_TEXT_MUTED : APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_width(lbl, width - (row->header ? 0 : 12));
    lv_obj_set_height(lbl, LV_SIZE_CONTENT);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    return lbl;
}

/* Re-settable text for a live-refreshed (non-header) row: same "label: value" format
 * conn_popup_add_row used at creation, called again on the label it already made. */
static void conn_popup_set_row_text(lv_obj_t *label, const conn_popup_row_t *row)
{
    if (label == NULL || row->header) {
        return;
    }
    char buf[200] = {0};
    conn_popup_row_text(row, buf, sizeof(buf));
    lv_label_set_text(label, buf);
}

static conn_popup_row_t *conn_popup_push(conn_popup_row_t *rows, int *count, const char *label, bool header)
{
    if (*count >= CONN_POPUP_MAX_ROWS) {
        return NULL;
    }
    conn_popup_row_t *row = &rows[*count];
    memset(row, 0, sizeof(*row));
    row->label = label;
    row->header = header;
    (*count)++;
    return row;
}

/* "ws://host:8123/api/websocket" -> "host:8123". Keeps "[v6]:port" intact. */
static void conn_popup_host_from_url(const char *url, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (url == NULL || url[0] == '\0') {
        return;
    }
    const char *p = strstr(url, "://");
    p = (p != NULL) ? p + 3 : url;
    size_t n = strcspn(p, "/?#");
    if (n >= out_len) {
        n = out_len - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
}

typedef enum {
    CONN_POPUP_WIFI = 0,
    CONN_POPUP_HA,         /* Home Assistant and this panel's firmware */
    CONN_POPUP_MQTT,       /* the MQTT client */
} conn_popup_kind_t;

/* The popup used to be built once and never touched again -- SSID/IP/signal or HA/MQTT connection
 * status stayed frozen at whatever they were the moment it opened, however long it stayed open. A
 * timer (same pattern as ui_pages_mqtt_status_timer_cb's topbar glyph) now re-derives the same rows
 * every 2 s and updates each row's label text in place, mirroring the connectivity chip that opened
 * the popup rather than a menu built once from a database snapshot. */
static lv_timer_t *s_conn_popup_timer = NULL;
static conn_popup_kind_t s_conn_popup_kind;
static lv_obj_t *s_conn_popup_row_labels[CONN_POPUP_MAX_ROWS];
static int s_conn_popup_row_count = 0;
static lv_obj_t *s_conn_retry_button, *s_conn_retry_label, *s_conn_retry_hint;

static void conn_popup_retry_update(void)
{
    if (!s_conn_retry_button) return;
    ui_style_action_button(s_conn_retry_button, s_conn_retry_label, UI_CONTROL_ACTION,
                           ha_client_reconnect_available());
    if (ha_client_is_connected() || ha_auth_state_blocked() || ha_client_reconnect_available())
        lv_label_set_text(s_conn_retry_label, ui_i18n_get("conn.reconnect", "Reconnect"));
}

static void conn_popup_retry_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !ha_client_request_reconnect()) return;
    lv_label_set_text(s_conn_retry_label, ui_i18n_get("conn.reconnecting", "Reconnecting..."));
    ui_style_action_button(s_conn_retry_button, s_conn_retry_label, UI_CONTROL_ACTION, false);
}

static int conn_popup_place_retry(int y)
{
    if (!s_conn_retry_button) return y;
    lv_obj_align(s_conn_retry_hint, LV_ALIGN_TOP_LEFT, 0, y + 10);
    lv_obj_update_layout(s_conn_retry_hint);
    int button_y = y + 10 + lv_obj_get_height(s_conn_retry_hint) + 12;
    lv_obj_align(s_conn_retry_button, LV_ALIGN_TOP_LEFT, 0, button_y);
    return button_y + lv_obj_get_height(s_conn_retry_button);
}

/* Wrapping can change after a live refresh; place each next row below the actual label height. */
static int conn_popup_place_rows(const conn_popup_row_t *rows, int count)
{
    int y = 0;
    for (int i = 0; i < count; i++) {
        if (rows[i].header && i > 0) y += 8;
        lv_obj_t *label = s_conn_popup_row_labels[i];
        lv_obj_update_layout(label);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, rows[i].header ? 0 : 12, y);
        y += lv_obj_get_height(label) + 6;
    }
    return count > 0 ? y - 6 : 0;
}

/* Each top bar chip opens its own card: Wi-Fi, Home Assistant (with the firmware version) or MQTT. */
/* Builds the current row values for `kind` fresh every time -- called once when the popup opens and
 * again on every refresh tick, so this is the only place that knows how to read Wi-Fi/HA/MQTT status
 * into a row list. */
static void conn_popup_build_rows(conn_popup_kind_t kind, conn_popup_row_t *rows, int *row_count)
{
    *row_count = 0;
    conn_popup_row_t *row = NULL;

    const char *txt_status = ui_i18n_get("conn.status", "Status");
    const char *txt_connected = ui_i18n_get("conn.connected", "Connected");
    const char *txt_disconnected = ui_i18n_get("conn.disconnected", "Not connected");

    if (kind == CONN_POPUP_WIFI) {
        if (wifi_mgr_is_setup_ap_active()) {
            row = conn_popup_push(rows, row_count, txt_status, false);
            snprintf(row->value, sizeof(row->value), "%s", ui_i18n_get("wifi.info.setup_ap", "Setup AP active"));
            row = conn_popup_push(rows, row_count, ui_i18n_get("settings.wifi.ssid", "SSID"), false);
            snprintf(row->value, sizeof(row->value), "%s", wifi_mgr_get_setup_ap_ssid());
            row = conn_popup_push(rows, row_count, ui_i18n_get("boot.setup_ap_password", "Password"), false);
            snprintf(row->value, sizeof(row->value), "%s", wifi_mgr_get_setup_ap_password());
            row = conn_popup_push(rows, row_count, ui_i18n_get("wifi.info.ip", "IP address"), false);
            if (wifi_mgr_get_ap_ip(row->value, sizeof(row->value)) != ESP_OK) {
                row->value[0] = '\0';
            }
        } else if (wifi_mgr_is_connected()) {
            wifi_mgr_sta_ap_info_t ap_info = {0};
            bool have_info = wifi_mgr_get_sta_ap_info(&ap_info) == ESP_OK;
            row = conn_popup_push(rows, row_count, ui_i18n_get("settings.wifi.ssid", "SSID"), false);
            snprintf(row->value, sizeof(row->value), "%s", (have_info && ap_info.ssid[0] != '\0') ? ap_info.ssid : "");
            row = conn_popup_push(rows, row_count, ui_i18n_get("wifi.info.ip", "IP address"), false);
            if (wifi_mgr_get_sta_ip(row->value, sizeof(row->value)) != ESP_OK) {
                row->value[0] = '\0';
            }
            row = conn_popup_push(rows, row_count, ui_i18n_get("wifi.info.signal", "Signal strength"), false);
            if (have_info) {
                snprintf(row->value, sizeof(row->value), "%d dBm (%s)", (int)ap_info.rssi,
                    wifi_info_signal_quality(ap_info.rssi));
            }
        } else {
            row = conn_popup_push(rows, row_count, txt_status, false);
            snprintf(row->value, sizeof(row->value), "%s", txt_disconnected);

            /* Diagnostics for exactly the situation this popup cannot otherwise explain: the
             * display keeps working while Wi-Fi/the C6 co-processor does not come back on its
             * own, and there is no way to reach GET /api/log to find out why (see wifi_mgr.h's
             * wifi_mgr_diag_t / wifi_diag_store.h). Only shown once something has actually gone
             * wrong this boot, not on every normal "still connecting after power-on" moment. */
            wifi_mgr_diag_t diag = {0};
            wifi_mgr_get_diag(&diag);
            if (diag.disconnect_count > 0) {
                int64_t since_s = (esp_timer_get_time() / 1000 - diag.last_disconnect_uptime_ms) / 1000;
                row = conn_popup_push(rows, row_count, ui_i18n_get("conn.diag_reason", "Last disconnect reason"), false);
                snprintf(row->value, sizeof(row->value), "%s (%" PRId64 "s)",
                    wifi_mgr_reason_to_str(diag.last_disconnect_reason), since_s);

                row = conn_popup_push(rows, row_count, ui_i18n_get("conn.diag_reconnects", "Reconnect attempts"), false);
                snprintf(row->value, sizeof(row->value), "%" PRIu32, diag.reconnect_attempts);

                if (diag.hard_recover_count > 0) {
                    row = conn_popup_push(
                        rows, row_count, ui_i18n_get("conn.diag_hard_recover", "C6 hard recover"), false);
                    snprintf(row->value, sizeof(row->value), "%" PRIu32 " (%s)", diag.hard_recover_count,
                        esp_err_to_name((esp_err_t)diag.last_hard_recover_err));
                }
            }
        }
    }

    /* Settings are only needed for the configured HA / MQTT hosts; the
     * struct is ~1.6 KB, so keep it off the LVGL task stack. */
    runtime_settings_t *settings =
        (kind == CONN_POPUP_HA || kind == CONN_POPUP_MQTT) ? calloc(1, sizeof(runtime_settings_t)) : NULL;
    bool have_settings = settings != NULL && runtime_settings_load(settings) == ESP_OK;

    if (kind == CONN_POPUP_HA) {
        /* ---- Home Assistant (the card's title names it) ---- */
        row = conn_popup_push(rows, row_count, txt_status, false);
        if (ha_auth_state_blocked()) {
            snprintf(row->value, sizeof(row->value), "%s", txt_disconnected);
        } else if (s_topbar_api_connected && ha_client_is_connected() && s_topbar_api_sync_done) {
            snprintf(row->value, sizeof(row->value), "%s", txt_connected);
        } else if (s_topbar_api_connected && ha_client_is_connected()) {
            snprintf(row->value, sizeof(row->value), "%s", ui_i18n_get("conn.syncing", "Connected, syncing"));
        } else {
            snprintf(row->value, sizeof(row->value), "%s", txt_disconnected);
        }
        row = conn_popup_push(rows, row_count, ui_i18n_get("conn.host", "Host"), false);
        if (have_settings) {
            conn_popup_host_from_url(settings->ha_ws_url, row->value, sizeof(row->value));
        }
        row = conn_popup_push(rows, row_count, ui_i18n_get("conn.access", "Access"), false);
        const char *access = ha_auth_state_reason() == HA_AUTH_FORBIDDEN
            ? ui_i18n_get("conn.forbidden", "Denied (HTTP 403); check HA IP bans")
            : ha_auth_state_reason() == HA_AUTH_TOKEN_REJECTED
            ? ui_i18n_get("conn.token_rejected", "Access token rejected")
            : ui_i18n_get("conn.no_rejection", "No authentication rejection");
        snprintf(row->value, sizeof(row->value), "%s", access);

        /* ---- This panel's firmware ---- */
        conn_popup_push(rows, row_count, ui_i18n_get("conn.firmware", "Firmware"), true);
        const esp_app_desc_t *app = esp_app_get_description();
        row = conn_popup_push(rows, row_count, ui_i18n_get("conn.version", "Version"), false);
        snprintf(row->value, sizeof(row->value), "%s (%s)", app != NULL ? app->version : "?", APP_GIT_COMMIT_SHA);
        row = conn_popup_push(rows, row_count, ui_i18n_get("conn.built", "Built"), false);
        app_build_stamp_format(row->value, sizeof(row->value));
        char update_version[32];
        if (update_check_available_version(update_version, sizeof(update_version))) {
            row = conn_popup_push(rows, row_count, ui_i18n_get("conn.update_available", "Update available"), false);
            snprintf(row->value, sizeof(row->value), "%s", update_version);
        }
    }

    if (kind == CONN_POPUP_MQTT) {
        const bool mqtt_configured = have_settings && settings->mqtt_enabled && settings->mqtt_broker_host[0] != '\0';
        row = conn_popup_push(rows, row_count, txt_status, false);
        if (!mqtt_configured && !mqtt_mgr_is_started()) {
            snprintf(row->value, sizeof(row->value), "%s", ui_i18n_get("conn.disabled", "Disabled"));
        } else if (mqtt_mgr_is_connected()) {
            snprintf(row->value, sizeof(row->value), "%s", txt_connected);
        } else {
            snprintf(row->value, sizeof(row->value), "%s", txt_disconnected);
        }
        if (mqtt_configured) {
            row = conn_popup_push(rows, row_count, ui_i18n_get("conn.broker", "Broker"), false);
            snprintf(row->value, sizeof(row->value), "%s:%d", settings->mqtt_broker_host,
                settings->mqtt_broker_port > 0 ? settings->mqtt_broker_port : 1883);
        }
        if (mqtt_mgr_is_started() && mqtt_mgr_device_id()[0] != '\0') {
            row = conn_popup_push(rows, row_count, ui_i18n_get("conn.device_id", "Device ID"), false);
            snprintf(row->value, sizeof(row->value), "%s", mqtt_mgr_device_id());
        }
    }
    free(settings);
}

static void ui_pages_conn_popup_stop_timer(void)
{
    if (s_conn_popup_timer != NULL) {
        lv_timer_delete(s_conn_popup_timer);
        s_conn_popup_timer = NULL;
    }
    s_conn_popup_row_count = 0;
}

static void ui_pages_conn_popup_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    conn_popup_row_t rows[CONN_POPUP_MAX_ROWS];
    int row_count = 0;
    conn_popup_build_rows(s_conn_popup_kind, rows, &row_count);
    /* The row count for a given kind can change while the popup is open (e.g. Wi-Fi disconnects
     * mid-view, dropping the SSID/IP/signal rows down to a single "Not connected" one) -- rebuilding
     * the whole popup for that is more churn than a modal info popup needs; skip this tick's update
     * rather than write a fresh value into a label that no longer corresponds to the same row. The
     * next open will show the current state either way. */
    if (row_count != s_conn_popup_row_count) {
        return;
    }
    for (int i = 0; i < row_count; i++) {
        conn_popup_set_row_text(s_conn_popup_row_labels[i], &rows[i]);
    }
    conn_popup_place_retry(conn_popup_place_rows(rows, row_count));
    conn_popup_retry_update();
}

static void ui_pages_conn_popup_deleted_cb(lv_event_t *event)
{
    (void)event;
    ui_pages_conn_popup_stop_timer();
    s_conn_retry_button = s_conn_retry_label = s_conn_retry_hint = NULL;
}

static void ui_pages_show_connectivity_popup(conn_popup_kind_t kind)
{
    s_conn_retry_button = s_conn_retry_label = s_conn_retry_hint = NULL;
    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    int screen_w = lv_obj_get_width(screen);
    int screen_h = lv_obj_get_height(screen);
    if (screen_w <= 0) {
        screen_w = 720;
    }
    if (screen_h <= 0) {
        screen_h = 720;
    }

    conn_popup_row_t rows[CONN_POPUP_MAX_ROWS];
    int row_count = 0;
    conn_popup_build_rows(kind, rows, &row_count);

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, screen_w, screen_h);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(overlay, wifi_info_popup_overlay_event_cb, LV_EVENT_CLICKED, NULL);

    /* Keep the familiar size at Standard, but grow for the actual font and values before wrapping. */
    int card_w = 440;
    for (int i = 0; i < row_count; i++) {
        char text[200];
        lv_point_t size;
        conn_popup_row_text(&rows[i], text, sizeof(text));
        lv_text_get_size(&size, text, ui_font_role(UI_TEXT_LABEL), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int needed = size.x + 36 + 8 + 8 + (rows[i].header ? 0 : 12);
        if (needed > card_w) card_w = needed;
    }
    if (card_w > screen_w - 48) card_w = screen_w - 48;
    if (card_w < 260) {
        card_w = 260;
    }
    lv_obj_t *card = ui_popup_card_create(overlay);
    lv_obj_set_size(card, card_w, screen_h - 24);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);

    lv_obj_set_style_pad_all(card, 18, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title,
        kind == CONN_POPUP_WIFI ? ui_i18n_get("conn.wifi", "Wi-Fi")
        : kind == CONN_POPUP_HA ? ui_i18n_get("conn.ha", "Home Assistant")
                                : ui_i18n_get("conn.mqtt", "MQTT"));
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *close = ui_popup_close_button(
        card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), wifi_info_popup_close_event_cb, overlay);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -8);
    lv_obj_update_layout(card);
    const int content_w = lv_obj_get_content_width(card);
    lv_obj_set_width(title, content_w - lv_obj_get_width(close) - 12);
    lv_obj_set_height(title, lv_font_get_line_height(ui_font_role(UI_TEXT_META)));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    int row_y_start = lv_obj_get_height(close) + 12;
    int title_bottom = lv_obj_get_height(title) + 12;
    if (title_bottom > row_y_start) row_y_start = title_bottom;

    lv_obj_t *body = lv_obj_create(card);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, content_w);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, row_y_start);
    lv_obj_set_scrollable(body, true);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    for (int i = 0; i < row_count; i++) {
        s_conn_popup_row_labels[i] = conn_popup_add_row(body, content_w - 8, &rows[i]);
    }
    int content_h = conn_popup_place_rows(rows, row_count);
    if (kind == CONN_POPUP_HA) {
        s_conn_retry_hint = ui_popup_label_create(body, "", UI_POPUP_LABEL_HINT);
        lv_obj_set_width(s_conn_retry_hint, content_w - 8);
        lv_label_set_long_mode(s_conn_retry_hint, LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_conn_retry_hint, ui_i18n_get("conn.retry_hint",
            "Fix access or remove the IP ban in Home Assistant first, then reconnect."));
        s_conn_retry_button = lv_button_create(body);
        lv_obj_set_size(s_conn_retry_button, content_w - 8,
                        lv_font_get_line_height(ui_font_role(UI_TEXT_META)) + 24);
        s_conn_retry_label = ui_popup_label_create(s_conn_retry_button, "", UI_POPUP_LABEL_BODY);
        lv_label_set_text(s_conn_retry_label, ui_i18n_get("conn.reconnect", "Reconnect"));
        lv_obj_center(s_conn_retry_label);
        lv_obj_add_event_cb(s_conn_retry_button, conn_popup_retry_cb, LV_EVENT_CLICKED, NULL);
        conn_popup_retry_update();
        content_h = conn_popup_place_retry(content_h);
    }
    int frame_h = 2 * (18 + lv_obj_get_style_border_width(card, LV_PART_MAIN));
    int card_h = row_y_start + content_h + frame_h + 6;
    if (card_h > screen_h - 24) card_h = screen_h - 24;
    lv_obj_set_height(card, card_h);
    lv_obj_set_height(body, card_h - frame_h - row_y_start);
    lv_obj_center(card);

    ui_pages_conn_popup_stop_timer(); /* in case a previous popup's timer is somehow still running */
    s_conn_popup_kind = kind;
    s_conn_popup_row_count = row_count;
    s_conn_popup_timer = lv_timer_create(ui_pages_conn_popup_refresh_timer_cb, 2000, NULL);
    lv_obj_add_event_cb(overlay, ui_pages_conn_popup_deleted_cb, LV_EVENT_DELETE, NULL);
}

void ui_conn_popup_show_wifi(void)
{
    ui_pages_show_connectivity_popup(CONN_POPUP_WIFI);
}

void ui_conn_popup_show_ha(void)
{
    ui_pages_show_connectivity_popup(CONN_POPUP_HA);
}

void ui_conn_popup_show_mqtt(void)
{
    ui_pages_show_connectivity_popup(CONN_POPUP_MQTT);
}

void ui_conn_popup_set_api_status(bool connected, bool initial_sync_done)
{
    s_topbar_api_connected = connected;
    s_topbar_api_sync_done = initial_sync_done;
}
