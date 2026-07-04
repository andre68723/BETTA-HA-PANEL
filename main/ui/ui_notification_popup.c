/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 *
 * Full-screen notification overlay driven by POST /api/notifications.
 * One notification at a time: a new one replaces the current overlay
 * content in place. Lives on lv_layer_top() so it renders above all
 * pages and widgets; a scrim blocks touches behind it.
 */
#include "ui/ui_notification_popup.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "app_config.h"
#include "drivers/display_init.h"
#include "ui/fonts/app_text_fonts.h"
#include "ui/theme/theme_default.h"
#include "ui/ui_i18n.h"
#include "util/log_tags.h"

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *message_label;
    lv_obj_t *dismiss_btn;
    lv_timer_t *timeout_timer;
    char message_id[APP_NOTIFICATION_MAX_ID_LEN];
} ui_notification_state_t;

static ui_notification_state_t s_notif = {0};

static bool notif_utf8_decode(const char *src, uint32_t *cp, size_t *len)
{
    const unsigned char *s = (const unsigned char *)src;
    if (s[0] == '\0') {
        return false;
    }
    if (s[0] < 0x80U) {
        *cp = s[0];
        *len = 1;
        return true;
    }
    if ((s[0] & 0xE0U) == 0xC0U && (s[1] & 0xC0U) == 0x80U) {
        *cp = ((uint32_t)(s[0] & 0x1FU) << 6) | (uint32_t)(s[1] & 0x3FU);
        *len = 2;
        return true;
    }
    if ((s[0] & 0xF0U) == 0xE0U && (s[1] & 0xC0U) == 0x80U && (s[2] & 0xC0U) == 0x80U) {
        *cp = ((uint32_t)(s[0] & 0x0FU) << 12) | ((uint32_t)(s[1] & 0x3FU) << 6) |
              (uint32_t)(s[2] & 0x3FU);
        *len = 3;
        return true;
    }
    if ((s[0] & 0xF8U) == 0xF0U && (s[1] & 0xC0U) == 0x80U && (s[2] & 0xC0U) == 0x80U &&
        (s[3] & 0xC0U) == 0x80U) {
        *cp = ((uint32_t)(s[0] & 0x07U) << 18) | ((uint32_t)(s[1] & 0x3FU) << 12) |
              ((uint32_t)(s[2] & 0x3FU) << 6) | (uint32_t)(s[3] & 0x3FU);
        *len = 4;
        return true;
    }
    *cp = s[0];
    *len = 1;
    return true;
}

static bool notif_codepoint_is_emoji_or_symbol(uint32_t cp)
{
    if (cp == 0xFE0FU || cp == 0xFE0EU || cp == 0x200DU) {
        return true;
    }
    if ((cp >= 0x1F000U && cp <= 0x1FAFFU) || (cp >= 0x2600U && cp <= 0x27BFU) ||
        (cp >= 0x2190U && cp <= 0x21FFU)) {
        return true;
    }
    return false;
}

static const char *notif_symbol_replacement(uint32_t cp)
{
    switch (cp) {
    case 0x2194U: /* left-right arrow */
        return "~";
    default:
        return "";
    }
}

static void notif_append_char(char *dst, size_t dst_size, size_t *out, char ch)
{
    if (*out + 1U >= dst_size) {
        return;
    }
    dst[*out] = ch;
    (*out)++;
    dst[*out] = '\0';
}

static void notif_append_text(char *dst, size_t dst_size, size_t *out, const char *text)
{
    while (*text != '\0' && *out + 1U < dst_size) {
        dst[*out] = *text;
        (*out)++;
        text++;
    }
    dst[*out] = '\0';
}

static void notif_sanitize_label_text(const char *src, char *dst, size_t dst_size)
{
    if (dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }

    size_t out = 0;
    bool at_line_start = true;
    bool pending_symbol_space = false;

    while (*src != '\0' && out + 1U < dst_size) {
        uint32_t cp = 0;
        size_t len = 0;
        if (!notif_utf8_decode(src, &cp, &len) || len == 0) {
            break;
        }

        if (notif_codepoint_is_emoji_or_symbol(cp)) {
            const char *replacement = notif_symbol_replacement(cp);
            if (replacement[0] != '\0') {
                if (!at_line_start && out > 0 && dst[out - 1U] != ' ' && dst[out - 1U] != '\n') {
                    notif_append_char(dst, dst_size, &out, ' ');
                }
                notif_append_text(dst, dst_size, &out, replacement);
                pending_symbol_space = true;
                at_line_start = false;
            } else if (!at_line_start && out > 0 && dst[out - 1U] != ' ' && dst[out - 1U] != '\n') {
                pending_symbol_space = true;
            }
            src += len;
            continue;
        }

        if (*src == '\r') {
            src += len;
            continue;
        }

        if (*src == '\n') {
            while (out > 0 && dst[out - 1U] == ' ') {
                out--;
                dst[out] = '\0';
            }
            notif_append_char(dst, dst_size, &out, '\n');
            at_line_start = true;
            pending_symbol_space = false;
            src += len;
            continue;
        }

        if ((*src == ' ' || *src == '\t') && at_line_start) {
            src += len;
            continue;
        }

        if (pending_symbol_space && !at_line_start && *src != ' ' && *src != '\t' && out + 1U < dst_size) {
            notif_append_char(dst, dst_size, &out, ' ');
        }
        pending_symbol_space = false;

        if (out + len >= dst_size) {
            break;
        }
        memcpy(dst + out, src, len);
        out += len;
        dst[out] = '\0';
        at_line_start = false;
        src += len;
    }

    while (out > 0 && (dst[out - 1U] == ' ' || dst[out - 1U] == '\n')) {
        out--;
        dst[out] = '\0';
    }
}

static void notif_stop_timer(void)
{
    if (s_notif.timeout_timer != NULL) {
        lv_timer_del(s_notif.timeout_timer);
        s_notif.timeout_timer = NULL;
    }
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

static void notif_overlay_delete_event_cb(lv_event_t *event)
{
    (void)event;
    notif_stop_timer();
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
            lv_obj_clear_flag(s_notif.title_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(s_notif.title_label, "");
            lv_obj_add_flag(s_notif.title_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_notif.message_label != NULL) {
        lv_label_set_text(s_notif.message_label, sanitized_message);
    }
    strlcpy(s_notif.message_id, notification->message_id, sizeof(s_notif.message_id));
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
        screen_w = APP_SCREEN_WIDTH;
    }
    if (screen_h <= 0) {
        screen_h = APP_SCREEN_HEIGHT;
    }

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    s_notif.overlay = overlay;
    lv_obj_set_size(overlay, screen_w, screen_h);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
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

    lv_obj_t *card = lv_obj_create(overlay);
    s_notif.card = card;
    lv_obj_set_width(card, card_w);
    if (large_display && strlen(notification->message) > 320U) {
        lv_obj_set_height(card, max_card_h);
    } else {
        lv_obj_set_height(card, LV_SIZE_CONTENT);
    }
    lv_obj_set_style_max_height(card, max_card_h, LV_PART_MAIN);
    lv_obj_center(card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CONTENT_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CONTENT_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    /* Cross-axis center only affects the dismiss button; the labels span 100%. */
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 12, LV_PART_MAIN);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, notif_dismiss_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(card);
    s_notif.title_label = title;
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, APP_FONT_TEXT_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, theme_default_color_text_primary(), LV_PART_MAIN);

    lv_obj_t *message = lv_label_create(card);
    s_notif.message_label = message;
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(message, APP_FONT_TEXT_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(message, theme_default_color_text_primary(), LV_PART_MAIN);

    lv_obj_t *btn = lv_btn_create(card);
    s_notif.dismiss_btn = btn;
    theme_default_style_button(btn, false);
    lv_obj_set_height(btn, 44);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(btn, 24, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, notif_dismiss_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, ui_i18n_get("notification.dismiss", "Dismiss"));
    lv_obj_set_style_text_font(btn_label, APP_FONT_TEXT_16, LV_PART_MAIN);
    lv_obj_center(btn_label);

    notif_apply_texts(notification);
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

    display_note_activity();

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
