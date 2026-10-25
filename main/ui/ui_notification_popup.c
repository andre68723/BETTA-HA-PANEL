/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Full-screen notification overlay driven by POST /api/notifications.
 * One notification at a time: a new one replaces the current overlay
 * content in place. Lives on lv_layer_top() so it renders above all
 * pages and widgets; a scrim blocks touches behind it.
 */
#include "ui/display_geometry.h"
#include "ui/ui_notification_popup.h"
#include "ui/notification_emoji.h"
#include "ui/notification_text.h"
#include "ui/notification_message.h"
#include "drivers/chime.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "app_config.h"
#include "util/entity_id.h"
#include "drivers/display_init.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "ha/ha_client.h"
#include "ha/ha_cover_fetcher.h"
#include "ha/ha_lock_state.h"
#include "ha/ha_model.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_popup.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "util/log_tags.h"

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *message_label;
    lv_obj_t *message_spans;
    lv_obj_t *dismiss_btn;
    lv_obj_t *action_btn;
    lv_obj_t *action_label;
    lv_timer_t *timeout_timer;
    char message_id[APP_NOTIFICATION_MAX_ID_LEN];
    char action_entity_id[APP_MAX_ENTITY_ID_LEN];
    bool action_open;
    /* After a lock action the card stays a moment and the button shows the lock's state. */
    lv_timer_t *lock_status_timer;
    /* Optional camera snapshot, refreshed while the card is open. */
    lv_obj_t *image;
    lv_image_dsc_t image_dsc;
    lv_timer_t *camera_timer;
    char camera_url[256];
    bool camera_inflight;
    int64_t camera_request_ms; /* when the pending snapshot request was sent (see notif_camera_request) */
    int camera_w;
    int camera_h;
} ui_notification_state_t;

/* The popup's text font: the META role with the color emoji of ui/notification_emoji.c in front of the monochrome fallback. */
static const lv_font_t *notif_font(void)
{
    const lv_font_t *base = ui_font_role(UI_TEXT_META);
    return notification_emoji_font(base, ui_font_text_px_of(base));
}

static ui_notification_state_t s_notif = {0};

/* The text of a title, message or action label for the panel's labels: supported emoji stay, what the font cannot shape becomes a
 * short marker, and the result always fits dst (ui/notification_text.h). */
static void notif_sanitize_label_text(const char *src, char *dst, size_t dst_size)
{
    if (dst_size == 0) {
        return;
    }
    (void)notif_text_sanitize(src, src != NULL ? strlen(src) : 0, dst, dst_size);
}

static void notif_stop_timer(void)
{
    if (s_notif.timeout_timer != NULL) {
        lv_timer_del(s_notif.timeout_timer);
        s_notif.timeout_timer = NULL;
    }
}

static void notif_camera_release(void)
{
    if (s_notif.image_dsc.data != NULL) {
        heap_caps_free((void *)s_notif.image_dsc.data);
        memset(&s_notif.image_dsc, 0, sizeof(s_notif.image_dsc));
    }
}

static void notif_camera_result_cb(void *user, const ha_cover_result_t *result)
{
    (void)user;
    s_notif.camera_inflight = false;
    if (result == NULL || !result->valid) {
        return;
    }
    if (s_notif.overlay == NULL || s_notif.image == NULL) {
        if (result->image.data != NULL) {
            heap_caps_free((void *)result->image.data); /* the card is gone: nobody owns the picture */
        }
        return;
    }
    lv_image_dsc_t fresh = result->image;
    lv_image_set_src(s_notif.image, &fresh); /* switch before the old buffer goes away */
    notif_camera_release();
    s_notif.image_dsc = fresh;
    lv_image_set_src(s_notif.image, &s_notif.image_dsc);
    lv_obj_set_hidden(s_notif.image, false);
}

static void notif_camera_request(void)
{
    if (s_notif.camera_url[0] == '\0') {
        return;
    }
    /* The shared fetcher may drop a queued request without calling back: forget one that stays unanswered. */
    if (s_notif.camera_inflight && esp_timer_get_time() / 1000 - s_notif.camera_request_ms < 30000) {
        return;
    }
    s_notif.camera_inflight = true;
    s_notif.camera_request_ms = esp_timer_get_time() / 1000;
    if (ha_cover_fetcher_request(s_notif.camera_url, s_notif.camera_w, s_notif.camera_h, notif_camera_result_cb, &s_notif) != ESP_OK) {
        s_notif.camera_inflight = false;
    }
}

static void notif_camera_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    notif_camera_request();
}

/* Look up the camera's snapshot address (entity_picture) and start showing / refreshing it. */
static void notif_camera_setup(const app_notification_t *notification)
{
    s_notif.camera_url[0] = '\0';
    if (s_notif.camera_timer != NULL) {
        lv_timer_delete(s_notif.camera_timer);
        s_notif.camera_timer = NULL;
    }
    if (s_notif.image == NULL) {
        return;
    }
    if (notification->camera_entity_id[0] != '\0') {
        ha_state_t state = {0};
        if (ha_model_get_state(notification->camera_entity_id, &state) && state.attributes_json[0] != '\0') {
            cJSON *attrs = cJSON_Parse(state.attributes_json);
            cJSON *pic = attrs != NULL ? cJSON_GetObjectItemCaseSensitive(attrs, "entity_picture") : NULL;
            if (cJSON_IsString(pic) && pic->valuestring != NULL) {
                strlcpy(s_notif.camera_url, pic->valuestring, sizeof(s_notif.camera_url));
            }
            cJSON_Delete(attrs);
        }
    }
    if (s_notif.camera_url[0] == '\0') {
        lv_obj_set_hidden(s_notif.image, true);
        return;
    }
    notif_camera_request();
    s_notif.camera_timer = lv_timer_create(notif_camera_timer_cb, 3000, NULL);
}

static void notif_dismiss_internal(void)
{
    notif_stop_timer();
    if (s_notif.overlay != NULL) {
        lv_obj_del(s_notif.overlay);
        /* overlay delete event resets the rest of the state */
    }
}

static void notif_timeout_cb(lv_timer_t *timer)
{
    (void)timer;
    /* repeat_count 1: LVGL deletes the timer after this callback returns */
    s_notif.timeout_timer = NULL;
    notif_dismiss_internal();
}

static void notif_dismiss_event_cb(lv_event_t *event)
{
    (void)event;
    notif_dismiss_internal();
}

static void notif_start_timer(uint32_t timeout_sec);

/* The lock's state on the action button ("Unlocked", "Jammed"), read from the entity model while it
 * is subscribed (a lock tile anywhere in the layout); otherwise just "Sent". */
static void notif_lock_status_cb(lv_timer_t *timer)
{
    (void)timer;
    ha_state_t *state = calloc(1, sizeof(*state));
    const char *text = ui_i18n_get("notification.sent", "Sent");
    if (state != NULL && ha_model_get_state(s_notif.action_entity_id, state)) {
        const ha_lock_state_t lock = ha_lock_state_parse(state->state);
        if (lock != HA_LOCK_UNKNOWN) {
            text = ui_i18n_get(ha_lock_state_key(lock), state->state);
        }
    }
    free(state);
    lv_label_set_text(s_notif.action_label, text);
}

/* The action button: press a button entity, turn on a script or scene, or unlock / open a lock. A lock
 * keeps the card open for a few seconds so the person at the panel sees whether the door opened. */
static void notif_action_event_cb(lv_event_t *event)
{
    (void)event;
    const char *entity = s_notif.action_entity_id;
    const char *dot = strchr(entity, '.');
    bool lock = false;
    if (entity[0] != '\0' && dot != NULL && entity_id_action_allowed(entity)) {
        char domain[16];
        snprintf(domain, sizeof(domain), "%.*s", (int)(dot - entity) < 15 ? (int)(dot - entity) : 15, entity);
        const char *service = "turn_on";
        if (strcmp(domain, "button") == 0 || strcmp(domain, "input_button") == 0) {
            service = "press";
        } else if (strcmp(domain, "lock") == 0) {
            lock = true;
            service = s_notif.action_open ? "open" : "unlock";
        }
        char payload[APP_MAX_ENTITY_ID_LEN + 24];
        snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"}", entity);
        (void)ha_client_call_service(domain, service, payload);
        ESP_LOGI(TAG_UI, "notification action: %s.%s %s", domain, service, entity);
    }
    if (!lock || s_notif.lock_status_timer != NULL) {
        notif_dismiss_internal();
        return;
    }
    lv_obj_set_clickable(s_notif.action_btn, false);
    lv_label_set_text(s_notif.action_label, ui_i18n_get("notification.sent", "Sent"));
    s_notif.lock_status_timer = lv_timer_create(notif_lock_status_cb, 500, NULL);
    notif_start_timer(6);
}

static void notif_overlay_delete_event_cb(lv_event_t *event)
{
    (void)event;
    notif_stop_timer();
    if (s_notif.lock_status_timer != NULL) {
        lv_timer_delete(s_notif.lock_status_timer);
    }
    ha_cover_fetcher_cancel(&s_notif);
    if (s_notif.camera_timer != NULL) {
        lv_timer_delete(s_notif.camera_timer);
        s_notif.camera_timer = NULL;
    }
    notif_camera_release();
    memset(&s_notif, 0, sizeof(s_notif));
}

static void notif_start_timer(uint32_t timeout_sec)
{
    notif_stop_timer();
    if (timeout_sec == 0) {
        return;
    }
    if (timeout_sec > APP_NOTIFICATION_MAX_TIMEOUT_SEC) {
        timeout_sec = APP_NOTIFICATION_MAX_TIMEOUT_SEC;
    }
    s_notif.timeout_timer = lv_timer_create(notif_timeout_cb, timeout_sec * 1000U, NULL);
    if (s_notif.timeout_timer != NULL) {
        lv_timer_set_repeat_count(s_notif.timeout_timer, 1);
    }
}

static void notif_apply_texts(const app_notification_t *notification)
{
    char sanitized_title[APP_NOTIFICATION_MAX_TITLE_LEN];
    char sanitized_message[APP_NOTIFICATION_MAX_MESSAGE_LEN];
    notif_sanitize_label_text(notification->title, sanitized_title, sizeof(sanitized_title));
    notif_sanitize_label_text(notification->message, sanitized_message, sizeof(sanitized_message));

    if (s_notif.title_label != NULL) {
        if (sanitized_title[0] != '\0') {
            lv_label_set_text(s_notif.title_label, sanitized_title);
        } else {
            lv_label_set_text(s_notif.title_label, "");
        }
    }
    if (s_notif.message_label != NULL) {
        notification_message_set(s_notif.message_label, &s_notif.message_spans, sanitized_message);
    }
    strlcpy(s_notif.message_id, notification->message_id, sizeof(s_notif.message_id));

    strlcpy(s_notif.action_entity_id, notification->action_entity_id, sizeof(s_notif.action_entity_id));
    s_notif.action_open = notification->action_open;
    if (s_notif.action_btn != NULL) {
        if (notification->action_entity_id[0] != '\0') {
            char label[APP_NOTIFICATION_MAX_ACTION_LABEL_LEN];
            notif_sanitize_label_text(notification->action_label, label, sizeof(label));
            lv_label_set_text(s_notif.action_label, label[0] != '\0' ? label : ui_i18n_get("notification.action", "Open"));
            lv_obj_set_hidden(s_notif.action_btn, false);
        } else {
            lv_obj_set_hidden(s_notif.action_btn, true);
        }
    }
    notif_camera_setup(notification);
}

static void notif_create_overlay(const app_notification_t *notification)
{
    lv_obj_t *screen = lv_scr_act();
    int screen_w = 0;
    int screen_h = 0;
    if (screen != NULL) {
        lv_obj_update_layout(screen);
        screen_w = lv_obj_get_width(screen);
        screen_h = lv_obj_get_height(screen);
    }
    if (screen_w <= 0) {
        screen_w = APP_VIEW_WIDTH;
    }
    if (screen_h <= 0) {
        screen_h = APP_VIEW_HEIGHT;
    }

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    s_notif.overlay = overlay;
    lv_obj_set_size(overlay, screen_w, screen_h);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_set_clickable(overlay, true);
    lv_obj_add_event_cb(overlay, notif_dismiss_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(overlay, notif_overlay_delete_event_cb, LV_EVENT_DELETE, NULL);

    int larger_dim = screen_w > screen_h ? screen_w : screen_h;
    bool large_display = larger_dim >= 1000;

    int card_w = screen_w - (large_display ? 160 : 48);
    int max_card_w = large_display ? 760 : 520;
    if (card_w > max_card_w) {
        card_w = max_card_w;
    }
    if (card_w < 260) {
        card_w = 260;
    }

    int max_card_h = screen_h - (large_display ? 96 : 32);
    if (max_card_h < 260) {
        max_card_h = screen_h - 24;
    }

    lv_obj_t *card = ui_popup_card_create(overlay);
    s_notif.card = card;
    lv_obj_set_width(card, card_w);
    if (large_display && strlen(notification->message) > 320U) {
        lv_obj_set_height(card, max_card_h);
    } else {
        lv_obj_set_height(card, LV_SIZE_CONTENT);
    }
    lv_obj_set_style_max_height(card, max_card_h, LV_PART_MAIN);
    lv_obj_center(card);
    lv_obj_set_scrollable(card, true);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(card, LV_DIR_VER); /* never sideways: it shifted the content off centre */
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);

    lv_obj_set_style_pad_all(card, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    /* Cross-axis center only affects the dismiss button; the labels span 100%. */
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 12, LV_PART_MAIN);
    lv_obj_set_clickable(card, true);
    lv_obj_add_event_cb(card, notif_dismiss_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(card);
    s_notif.title_label = title;
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_pad_right(title, 60, LV_PART_MAIN); /* leaves room for the X on the right */
    lv_obj_set_style_min_height(title, 50, LV_PART_MAIN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, notif_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, theme_default_color_text_primary(), LV_PART_MAIN);

    lv_obj_t *image = lv_image_create(card);
    s_notif.image = image;
    /* Card width minus padding (2 x 20) and border (2 x 1): one pixel more and the card scrolls sideways. */
    s_notif.camera_w = card_w - 42;
    s_notif.camera_h = (card_w - 42) * 9 / 16;
    lv_obj_set_hidden(image, true);

    lv_obj_t *message = lv_label_create(card);
    s_notif.message_label = message;
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(message, notif_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(message, theme_default_color_text_primary(), LV_PART_MAIN);

    lv_obj_t *abtn = lv_btn_create(card);
    s_notif.action_btn = abtn;
    theme_default_style_button(abtn, true);
    /* Large: whoever is at the door has to be let in quickly. */
    lv_obj_set_height(abtn, 84);
    lv_obj_set_width(abtn, LV_PCT(100));
    lv_obj_add_event_cb(abtn, notif_action_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_hidden(abtn, true);
    s_notif.action_label = lv_label_create(abtn);
    /* Poppins (Latin-1): the label may carry umlauts such as "Öffnen". */
    lv_obj_set_style_text_font(s_notif.action_label, notif_font(), LV_PART_MAIN);
    lv_obj_center(s_notif.action_label);

    /* The same round close button as the other popups (top right). */
    lv_obj_t *btn = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), notif_dismiss_event_cb, NULL);
    s_notif.dismiss_btn = btn;
    lv_obj_set_ignore_layout(btn, true);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, 0, 0);

    notif_apply_texts(notification);
}

/* The last REST/MQTT message, in PSRAM and RAM only (lost on reboot). */
static app_notification_t *s_last = NULL;

static void notif_remember(const app_notification_t *notification)
{
    if (s_last == NULL) {
        s_last = heap_caps_calloc(1, sizeof(*s_last), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_last == NULL) {
            return;
        }
    }
    *s_last = *notification;
    s_last->remember = false;
}

bool ui_notification_popup_show_last(void)
{
    if (s_last == NULL) {
        return false;
    }
    app_notification_t *copy = calloc(1, sizeof(*copy));
    if (copy == NULL) {
        return false;
    }
    *copy = *s_last;
    copy->sound = CHIME_NONE;
    /* A door that was opened by a button 15 minutes ago should not open again from a stale card. */
    copy->action_entity_id[0] = '\0';
    copy->action_label[0] = '\0';
    copy->action_open = false;
    ui_notification_popup_show(copy);
    return true;
}

void ui_notification_popup_show(app_notification_t *notification)
{
    if (notification == NULL) {
        return;
    }
    if (notification->message[0] == '\0') {
        free(notification);
        return;
    }

    if (notification->remember) {
        notif_remember(notification);
    }
    display_note_activity();
    (void)chime_request(notification->sound, false, 0);

    if (s_notif.overlay != NULL) {
        notif_apply_texts(notification);
    } else {
        notif_create_overlay(notification);
    }
    notif_start_timer(notification->timeout_sec);

    ESP_LOGI(TAG_UI, "notification shown: id=%s timeout=%us",
             notification->message_id[0] != '\0' ? notification->message_id : "-",
             (unsigned)notification->timeout_sec);
    free(notification);
}

void ui_notification_popup_dismiss(void)
{
    notif_dismiss_internal();
}

bool ui_notification_popup_is_active(void)
{
    return s_notif.overlay != NULL;
}
