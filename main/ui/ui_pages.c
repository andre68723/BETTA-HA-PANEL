/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/display_geometry.h"
#include "ui/ui_pages.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "net/mqtt_mgr.h"
#include "net/update_check.h"
#include "net/wifi_mgr.h"
#include "settings/runtime_settings.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/nav_layout.h"
#include "ui/ui_conn_popup.h"
#include "ui/ui_notification_popup.h"
#include "ui/ui_popup.h"
#include "ui/fonts/mdi_font_registry.h"
#include "ui/ui_i18n.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "component_versions.h"
#include "esp_app_desc.h"
#include "build_stamp.h"
#include "net/wifi_signal_level.h"


typedef struct {
    char id[APP_MAX_PAGE_ID_LEN];
    char title[APP_MAX_NAME_LEN];
    lv_obj_t *container;
    char icon[16]; /* icon key ("" = automatic, "none" = no icon) */
} ui_page_entry_t;

static ui_page_entry_t s_pages[APP_MAX_PAGES];
static uint16_t s_page_count = 0;
static int16_t s_current_index = -1;
static ui_pages_show_cb_t s_show_cb = NULL;

void ui_pages_set_show_callback(ui_pages_show_cb_t cb)
{
    s_show_cb = cb;
}

static lv_obj_t *s_background = NULL;
static lv_obj_t *s_topbar = NULL;
static lv_obj_t *s_content_box = NULL;
static lv_obj_t *s_date_label = NULL;
static lv_obj_t *s_time_label = NULL;
static lv_obj_t *s_stock_chip = NULL;
static lv_obj_t *s_stock_rows[3] = {0};
static lv_obj_t *s_stock_labels[3] = {0};
static lv_obj_t *s_stock_euro_labels[3] = {0};
static lv_obj_t *s_weather_chip = NULL;
static lv_obj_t *s_weather_temp_label = NULL;
static lv_obj_t *s_weather_icon_label = NULL;
static char s_topbar_left_slots[APP_TOPBAR_LEFT_SLOTS][APP_MAX_UI_OPTION_LEN] = { "date", "", "" };
static bool s_topbar_weather_has_value = false;
static bool s_topbar_stock_has_value = false;
static char s_stock_symbol[24];
static char s_stock_value[16];
static bool s_stock_euro = false;
static lv_obj_t *s_wifi_icon = NULL;
static lv_obj_t *s_api_chip = NULL;      /* Home Assistant chip */
static lv_obj_t *s_mqtt_chip = NULL;     /* MQTT chip, visible while MQTT is started */
static lv_obj_t *s_crash_warning = NULL;
static lv_coord_t s_wifi_chip_w = 0;    /* the Wi-Fi chip's width as set (its box is only measured after a layout pass) */
static lv_obj_t *s_api_icon = NULL;      /* text fallback inside s_api_chip when the glyph font is missing */
static lv_obj_t *s_api_ha_glyph = NULL;
static lv_obj_t *s_api_mqtt_glyph = NULL;
static lv_timer_t *s_mqtt_status_timer = NULL;
static lv_obj_t *s_nav_bar = NULL;
static lv_obj_t *s_nav_home_button = NULL;
static lv_obj_t *s_nav_home_label = NULL;
static lv_obj_t *s_nav_home_title = NULL; /* title of the first page next to the home icon */
static lv_obj_t *s_nav_extra_buttons[APP_MAX_PAGES - 1] = {0};
static lv_obj_t *s_nav_extra_labels[APP_MAX_PAGES - 1] = {0};
static lv_obj_t *s_nav_extra_icons[APP_MAX_PAGES - 1] = {0}; /* house icon before the title of the house page */
static uint16_t s_nav_extra_page_index[APP_MAX_PAGES - 1] = {0};

/* ---- Easter egg: 7 taps on the home nav button reveal a swimming Betta. */
#if LV_USE_LOTTIE && APP_UI_BETTA_LOTTIE_ASSET
extern const uint8_t betta_lottie_start[] asm("_binary_betta_json_start");
extern const uint8_t betta_lottie_end[]   asm("_binary_betta_json_end");
#define UI_BETTA_TAP_TARGET    7U
#define UI_BETTA_TAP_WINDOW_MS 2500U

static lv_obj_t *s_betta_overlay = NULL;
static lv_obj_t *s_betta_lottie  = NULL;
static void     *s_betta_buf     = NULL;
static uint8_t   s_betta_taps    = 0;
static uint32_t  s_betta_last_ms = 0;
#endif

/* Narrow (4") topbar is tightly packed: the time label's box sits right up
 * against the weather chip, so it can't just grow wider to fit "22:49"-style
 * strings without overlapping it -- use a smaller font instead. */
#define TOPBAR_TIME_FONT ui_font_role(UI_TEXT_VALUE)

/* Two sizes in the top bar: the clock (TOPBAR_TIME_FONT) and this one for everything else
 * (date, temperature, stock quotes). */
#define TOPBAR_DATE_ROLE UI_TEXT_META
#define TOPBAR_DATE_FONT ui_font_role(TOPBAR_DATE_ROLE)

#define TOPBAR_ICON_FONT APP_FONT_SYMBOL_MEDIUM

/* Right-hand status chips, from the screen edge inwards: Wi-Fi, MQTT (only while MQTT is started) and Home
 * Assistant, one chip per icon so each frame hugs its symbol. Every chip has the same width; the touch area is
 * enlarged beyond the frame (TOPBAR_CHIP_TOUCH_PAD). */
#define TOPBAR_STATUS_CHIP_W (APP_VIEW_WIDTH < 1000 ? 52 : 60)
#define TOPBAR_WIFI_AP_CHIP_W 96 /* "AP" + symbol while the setup access point is up */
#if defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
/* The Tab5 has about 294 ppi, twice the 10-inch boards' 149 ppi: a 40 px chip is only 3.5 mm tall there and the popups
 * were hard to hit. The touch area fills the whole 60 px bar (40 + 2 x 10) and is 80 px wide; the 20 px gap keeps the
 * neighbouring areas from overlapping. The frame itself stays as it was. */
#define TOPBAR_CHIP_GAP      20
#define TOPBAR_CHIP_TOUCH_PAD 10
#else
#define TOPBAR_CHIP_GAP      8
#define TOPBAR_CHIP_TOUCH_PAD 6
#endif
#define TOPBAR_EDGE_PAD      12
#define TOPBAR_WEATHER_X     (-(TOPBAR_EDGE_PAD + 3 * (TOPBAR_STATUS_CHIP_W + TOPBAR_CHIP_GAP)))
/* "Connected" color of the HA / MQTT glyphs (Material light blue 500, also
 * Home Assistant's default accent). Fixed rather than themed: it has to read
 * on both the dark and the light theme palettes. */
#define TOPBAR_STATUS_CONNECTED_COLOR 0x03A9F4

#define NAV_TEXT_FONT ui_font_role(UI_TEXT_LABEL)

/* Page icons of the navigation bar (MDI glyphs in the status fonts). */
static const struct {
    const char *key;
    const char *glyph;
} k_page_icons[] = {
    {"room", "\xF3\xB0\xA0\x9C"},      /* door-open */
    {"floorplan", "\xF3\xB0\xA0\xA1"}, /* floor-plan */
    {"house", "\xF3\xB0\x8B\x9C"},     /* home */
    {"energy", "\xF3\xB1\x90\x8B"},    /* lightning-bolt */
    {"dashboard", "\xF3\xB0\x95\xAE"}, /* view-dashboard */
    {"bed", "\xF3\xB0\x8B\xA3"},       /* bed */
    {"weather", "\xF3\xB0\x96\x95"},   /* weather-partly-cloudy */
    {"music", "\xF3\xB0\x9D\x9A"},     /* music */
    {"climate", "\xF3\xB0\x94\x8F"},   /* thermometer */
    {"sofa", "\xF3\xB0\x92\xB9"},      /* sofa */
    {"tv", "\xF3\xB0\x94\x82"},        /* television */
    {"garage", "\xF3\xB0\x9B\x99"},    /* garage */
    {"light", "\xF3\xB0\x8C\xB5"},     /* lightbulb */
};

/* Icon key of a page: its own setting, else the room icon for the first page, else none. */
static const char *ui_pages_icon_key(uint16_t index)
{
    if (index >= s_page_count) {
        return "";
    }
    const char *key = s_pages[index].icon;
    if (strcmp(key, "none") == 0) {
        return "";
    }
    if (key[0] == '\0') {
        return index == 0 ? "room" : "";
    }
    return key;
}

/* Show the page's icon in label; returns false (and hides the label) when it has none. */
static bool ui_pages_apply_nav_icon(lv_obj_t *label, uint16_t page_index)
{
    const char *key = ui_pages_icon_key(page_index);
    const char *glyph = NULL;
    for (size_t i = 0; i < sizeof(k_page_icons) / sizeof(k_page_icons[0]); i++) {
        if (strcmp(k_page_icons[i].key, key) == 0) {
            glyph = k_page_icons[i].glyph;
            break;
        }
    }
    if (glyph == NULL) {
        lv_label_set_text(label, "");
        lv_obj_set_hidden(label, true);
        return false;
    }
    const lv_font_t *font = APP_VIEW_WIDTH < 1000 ? mdi_font_status_20() : mdi_font_status_24();
    if (font != NULL) {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        lv_label_set_text(label, glyph);
    } else {
        lv_obj_set_style_text_font(label, APP_FONT_SYMBOL_SMALL, LV_PART_MAIN);
        lv_label_set_text(label, APP_SYMBOL_LIST);
    }
    lv_obj_set_hidden(label, false);
    return true;
}

static void ui_pages_style_nav_button(lv_obj_t *btn, lv_obj_t *label, bool selected, bool is_home)
{
    if (btn == NULL || label == NULL) {
        return;
    }

    const lv_color_t chip_bg = lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BG);
    const lv_color_t chip_border = lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER);
    lv_obj_set_style_bg_color(btn, chip_bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, selected ? LV_OPA_80 : LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, selected ? LV_OPA_COVER : LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, chip_border, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, ui_style_radius(12), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(btn, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (is_home) {
        lv_obj_set_style_text_color(
            label,
            selected ? lv_color_hex(APP_UI_COLOR_NAV_HOME_ACTIVE) : lv_color_hex(APP_UI_COLOR_NAV_HOME_IDLE),
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_text_color(
            label,
            selected ? lv_color_hex(APP_UI_COLOR_NAV_TAB_ACTIVE) : lv_color_hex(APP_UI_COLOR_NAV_TAB_IDLE),
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/* A tab's page number for a compact tab whose page has no icon: nothing else tells the tabs apart. */
static char s_nav_number[APP_MAX_PAGES][4];

static void ui_pages_apply_tab_style(uint16_t selected_index)
{
    if (s_nav_bar == NULL || s_nav_home_button == NULL || s_nav_home_label == NULL) {
        return;
    }

    const lv_coord_t nav_btn_h = 42;
    const lv_coord_t nav_btn_y = 9;

    /* Tab positions come from ui/nav_layout.c: full tabs (icon and title) while every one is wide enough, else compact
     * tabs, where the shown page keeps its title and the others show their icon (or their number). The pages sit left to
     * right in their layout order, the same order the editor lists them in. */
    int title_w[NAV_MAX_TABS] = {0};
    for (uint16_t i = 0; i < s_page_count && i < NAV_MAX_TABS; i++) {
        lv_point_t sz;
        lv_text_get_size(&sz, s_pages[i].title, NAV_TEXT_FONT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        title_w[i] = sz.x;
    }
    nav_layout_t layout;
    const bool scroll_nav = display_geometry_portrait() && s_page_count > 8;
    const int nav_width = scroll_nav ? 1280 : APP_VIEW_WIDTH;
    lv_obj_set_scrollable(s_nav_bar, scroll_nav);
    lv_obj_set_scroll_dir(s_nav_bar, LV_DIR_HOR);
    nav_layout_compute(&layout, nav_width, s_page_count, selected_index, title_w);

    const nav_tab_t home = layout.count > 0 ? layout.tab[0] : (nav_tab_t){.x = NAV_MARGIN, .w = NAV_HOME_MIN_W, .show_title = false};
    const char *home_title = (s_page_count > 0) ? s_pages[0].title : "";
    if (s_nav_home_title != NULL && home.show_title && home_title != NULL && home_title[0] != '\0') {
        lv_label_set_text(s_nav_home_title, home_title);
        /* Width of the text (at most the room left by icon and padding), so icon and title sit together in the middle of the
         * tab like in the other tabs; a long title ends in dots at the maximum. */
        lv_obj_set_width(s_nav_home_title, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(s_nav_home_title, home.w - NAV_TAB_OVERHEAD, LV_PART_MAIN);
        lv_obj_set_height(s_nav_home_title, lv_font_get_line_height(NAV_TEXT_FONT)); /* a long title ends in dots, it does not wrap */
        lv_obj_set_hidden(s_nav_home_title, false);
        lv_obj_set_style_text_color(s_nav_home_title,
            (selected_index == 0) ? lv_color_hex(APP_UI_COLOR_NAV_TAB_ACTIVE) : lv_color_hex(APP_UI_COLOR_NAV_TAB_IDLE),
            LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (s_nav_home_title != NULL) {
        lv_obj_set_hidden(s_nav_home_title, true);
    }

    ui_pages_apply_nav_icon(s_nav_home_label, 0);
    lv_obj_set_size(s_nav_home_button, home.w, nav_btn_h);
    lv_obj_set_pos(s_nav_home_button, home.x, nav_btn_y);
    ui_pages_style_nav_button(s_nav_home_button, s_nav_home_label, (selected_index == 0), true);
    lv_obj_set_hidden(s_nav_home_button, false);

    uint16_t slot = 0;
    for (uint16_t page_index = 1; page_index < s_page_count && page_index < NAV_MAX_TABS && slot < (APP_MAX_PAGES - 1);
         page_index++, slot++) {
        lv_obj_t *btn = s_nav_extra_buttons[slot];
        lv_obj_t *label = s_nav_extra_labels[slot];
        if (btn == NULL || label == NULL) {
            continue;
        }

        const nav_tab_t tab = layout.tab[page_index];
        s_nav_extra_page_index[slot] = page_index;
        lv_obj_set_size(btn, tab.w, nav_btn_h);
        lv_obj_set_pos(btn, tab.x, nav_btn_y);
        lv_obj_t *icon = s_nav_extra_icons[slot];
        const bool with_icon = icon != NULL && ui_pages_apply_nav_icon(icon, page_index);
        if (tab.show_title) {
            lv_label_set_text(label, s_pages[page_index].title);
        } else if (!with_icon) {
            snprintf(s_nav_number[slot], sizeof(s_nav_number[slot]), "%u", (unsigned)(page_index + 1U));
            lv_label_set_text(label, s_nav_number[slot]);
        }
        /* a compact tab shows its icon alone; without an icon its number */
        lv_obj_set_hidden(label, !tab.show_title && with_icon);
        /* One line with an ellipsis; without a fixed height a long title wraps and is cut off. Content width up to the
         * button width, so icon and title sit together in the middle. */
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        lv_obj_set_height(label, lv_font_get_line_height(NAV_TEXT_FONT));
        lv_obj_set_style_max_width(label, tab.w - 20 - (with_icon ? 32 : 0), LV_PART_MAIN);
        lv_obj_set_hidden(btn, false);
        ui_pages_style_nav_button(btn, label, (page_index == selected_index), false);
        if (with_icon) {
            lv_obj_set_style_text_color(icon, lv_obj_get_style_text_color(label, LV_PART_MAIN), LV_PART_MAIN);
        }
    }

    if (scroll_nav) {
        lv_obj_t *selected = selected_index == 0 ? s_nav_home_button : s_nav_extra_buttons[selected_index - 1];
        if (selected) lv_obj_scroll_to_view(selected, LV_ANIM_OFF);
    }

    for (; slot < (APP_MAX_PAGES - 1); slot++) {
        s_nav_extra_page_index[slot] = APP_MAX_PAGES;
        if (s_nav_extra_buttons[slot] != NULL) {
            lv_obj_set_hidden(s_nav_extra_buttons[slot], true);
        }
    }
}

static void (*s_stock_chip_handler)(void);

void ui_pages_set_stock_chip_handler(void (*handler)(void))
{
    s_stock_chip_handler = handler;
}

static void ui_pages_stock_chip_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_DOUBLE_CLICKED || s_stock_chip_handler == NULL) {
        return;
    }
    ESP_LOGI("ui_pages", "top bar chip double tapped: stock");
    s_stock_chip_handler();
}

static void ui_pages_wifi_chip_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    ESP_LOGI("ui_pages", "top bar chip tapped: wifi");
    ui_conn_popup_show_wifi();
}

static void ui_pages_ha_chip_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    ESP_LOGI("ui_pages", "top bar chip tapped: ha");
    ui_conn_popup_show_ha();
}

static void ui_pages_mqtt_chip_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    ESP_LOGI("ui_pages", "top bar chip tapped: mqtt");
    ui_conn_popup_show_mqtt();
}

static void ui_pages_style_topbar_chip(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(obj, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, ui_style_radius(12), LV_PART_MAIN);
    lv_obj_set_style_pad_left(obj, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(obj, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(obj, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(obj, 4, LV_PART_MAIN);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, TOPBAR_ICON_FONT, LV_PART_MAIN);
}

static bool ui_pages_topbar_slot_enabled(const char *slot)
{
    if (slot == NULL || slot[0] == '\0' || strcmp(slot, "none") == 0) {
        return false;
    }
    for (size_t i = 0; i < APP_TOPBAR_LEFT_SLOTS; i++) {
        if (strcmp(s_topbar_left_slots[i], slot) == 0) {
            return true;
        }
    }
    return false;
}

/* Puts the quote into its chip and returns the chip width. The chip sits between the date and the clock; a quote such
 * as "SAP.DE 184.02 EUR" is wider than that gap on a 4-inch panel and was cut off. So it is measured, from the full
 * text towards less: without the exchange suffix (".DE"), then on the 4-inch class (where 20 px is the smallest text,
 * docs/appearance.md "Panel4 readability") without the currency sign and finally the quote alone, each still at 20 px;
 * other boards go down to 16 px instead. Only when nothing fits the text is clipped. */
typedef struct {
    ui_text_role_t role;
    bool full_symbol;   /* the symbol as delivered (with its exchange suffix), else the part before the dot */
    bool symbol;        /* the symbol at all */
    bool euro;          /* the currency sign of a euro quote */
} ui_stock_variant_t;

#if defined(CONFIG_APP_PANEL_VARIANT_4INCH_720) || defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
static const ui_stock_variant_t k_stock_variants[] = {
    {TOPBAR_DATE_ROLE, true, true, true},
    {TOPBAR_DATE_ROLE, false, true, true},
    {TOPBAR_DATE_ROLE, false, true, false},
    {TOPBAR_DATE_ROLE, false, false, false},
};
#else
static const ui_stock_variant_t k_stock_variants[] = {
    {TOPBAR_DATE_ROLE, true, true, true},
    {TOPBAR_DATE_ROLE, false, true, true},
    {UI_TEXT_META, false, true, true},
};
#endif

static lv_coord_t ui_pages_apply_stock_text(lv_coord_t avail)
{
    char short_symbol[sizeof(s_stock_symbol)];
    strlcpy(short_symbol, s_stock_symbol, sizeof(short_symbol));
    char *dot = strchr(short_symbol, '.');
    if (dot != NULL && dot != short_symbol) {
        *dot = '\0';
    }
    const int count = (int)(sizeof(k_stock_variants) / sizeof(k_stock_variants[0]));
    char text[sizeof(s_stock_symbol) + sizeof(s_stock_value) + 2];
    lv_coord_t need = 0;
    int chosen = count - 1;
    for (int v = 0; v < count; v++) {
        const ui_stock_variant_t *variant = &k_stock_variants[v];
        if (variant->symbol) {
            snprintf(text, sizeof(text), "%s %s", variant->full_symbol ? s_stock_symbol : short_symbol, s_stock_value);
        } else {
            snprintf(text, sizeof(text), "%s", s_stock_value);
        }
        const bool euro = s_stock_euro && variant->euro;
        lv_point_t size, euro_size = {0, 0};
        lv_text_get_size(&size, text, ui_font_role(variant->role), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (euro) {
            lv_text_get_size(&euro_size, "\xE2\x82\xAC", ui_font_role(variant->role), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        }
        need = (lv_coord_t)(size.x + (euro ? 3 + euro_size.x : 0) + 16 + 4); /* chip padding, LVGL rounding */
        if (need <= avail || v == count - 1) {
            chosen = v;
            break;
        }
    }
    const ui_stock_variant_t *picked = &k_stock_variants[chosen];
    if (picked->symbol) {
        snprintf(text, sizeof(text), "%s %s", picked->full_symbol ? s_stock_symbol : short_symbol, s_stock_value);
    } else {
        snprintf(text, sizeof(text), "%s", s_stock_value);
    }
    if (s_stock_labels[0] != NULL) {
        lv_label_set_text(s_stock_labels[0], text);
        lv_obj_set_style_text_font(s_stock_labels[0], ui_font_role(picked->role), LV_PART_MAIN);
    }
    if (s_stock_euro_labels[0] != NULL) {
        lv_obj_set_style_text_font(s_stock_euro_labels[0], ui_font_role(picked->role), LV_PART_MAIN);
        lv_obj_set_hidden(s_stock_euro_labels[0], !(s_stock_euro && picked->euro));
    }
    return need <= avail ? need : avail;
}

static void ui_pages_layout_topbar_left(void)
{
    lv_coord_t x = 16;
    uint8_t shown = 0;

    if (s_date_label != NULL) {
        lv_obj_set_hidden(s_date_label, true);
    }
    if (s_weather_chip != NULL) {
        lv_obj_set_hidden(s_weather_chip, true);
    }
    if (s_stock_chip != NULL) {
        lv_obj_set_hidden(s_stock_chip, true);
    }

    /* How many items fit left of the centred clock: one on a narrow screen, two on the 4-inch panel (720 px),
     * three on the 1280 px boards (date 220 + weather 116 + a quote of about 200 px). */
    const uint8_t max_shown = APP_VIEW_WIDTH < 700 ? 1 : (APP_VIEW_WIDTH < 1000 ? 2 : 3);
    const lv_coord_t clock_w = s_time_label != NULL ? lv_obj_get_width(s_time_label) : 88;
    const lv_coord_t limit = (APP_VIEW_WIDTH - clock_w) / 2 - 8; /* up to the clock, with a gap of 8 px */

    for (size_t i = 0; i < APP_TOPBAR_LEFT_SLOTS; i++) {
        if (shown >= max_shown) {
            break;
        }
        const char *slot = s_topbar_left_slots[i];
        if (slot[0] == '\0' || strcmp(slot, "none") == 0) {
            continue;
        }
        bool repeated = false;
        for (size_t j = 0; j < i; j++) {
            repeated = repeated || strcmp(slot, s_topbar_left_slots[j]) == 0;
        }
        if (repeated) {
            continue;
        }
        lv_obj_t *obj = NULL;
        lv_coord_t w = 0;
        if (strcmp(slot, "date") == 0) {
            obj = s_date_label;
            w = APP_VIEW_WIDTH < 700 ? 116 : (APP_VIEW_WIDTH < 1000 ? 148 : 168);
            /* A larger text step must not wrap the date into a second, clipped line (Tab5 at Largest: 32 px "28.09.2026" is wider than the slot). */
            lv_obj_set_style_text_font(obj, ui_text_fit(lv_label_get_text(obj), ui_font_role(UI_TEXT_META), w, 56), LV_PART_MAIN);
        } else if (strcmp(slot, "weather") == 0 && s_topbar_weather_has_value) {
            obj = s_weather_chip;
            w = APP_VIEW_WIDTH < 1000 ? 104 : 116;
        } else if (strcmp(slot, "stock") == 0 && s_topbar_stock_has_value) {
            obj = s_stock_chip;
            w = ui_pages_apply_stock_text(limit - x);
        }
        if (obj == NULL || x + w > limit) {
            continue; /* nothing to show, or it would run into the clock */
        }
        lv_obj_set_width(obj, w);
        lv_obj_align(obj, LV_ALIGN_LEFT_MID, x, 0);
        lv_obj_set_hidden(obj, false);
        x += w + 8;
        shown++;
    }
}

static bool ui_pages_utf8_from_codepoint(uint32_t codepoint, char out[5])
{
    if (out == NULL || codepoint == 0U || codepoint > 0x10FFFFU) {
        return false;
    }
    if (codepoint <= 0x7FU) {
        out[0] = (char)codepoint;
        out[1] = '\0';
        return true;
    }
    if (codepoint <= 0x7FFU) {
        out[0] = (char)(0xC0U | ((codepoint >> 6) & 0x1FU));
        out[1] = (char)(0x80U | (codepoint & 0x3FU));
        out[2] = '\0';
        return true;
    }
    if (codepoint <= 0xFFFFU) {
        out[0] = (char)(0xE0U | ((codepoint >> 12) & 0x0FU));
        out[1] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        out[2] = (char)(0x80U | (codepoint & 0x3FU));
        out[3] = '\0';
        return true;
    }
    out[0] = (char)(0xF0U | ((codepoint >> 18) & 0x07U));
    out[1] = (char)(0x80U | ((codepoint >> 12) & 0x3FU));
    out[2] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
    out[3] = (char)(0x80U | (codepoint & 0x3FU));
    out[4] = '\0';
    return true;
}

#if LV_USE_LOTTIE && APP_UI_BETTA_LOTTIE_ASSET
static void ui_pages_betta_hide(void);

static void ui_pages_betta_dismiss_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    ui_pages_betta_hide();
}

static void ui_pages_betta_show(void)
{
    if (s_betta_overlay != NULL) {
        return;
    }
    lv_obj_t *screen = lv_scr_act();
    if (screen == NULL) {
        return;
    }

    lv_coord_t side = APP_VIEW_HEIGHT < APP_VIEW_WIDTH ? APP_VIEW_HEIGHT : APP_VIEW_WIDTH;
    side -= 120;
    /* ThorVG renders the lottie on the CPU. Pixel cost scales quadratically;
     * the weather tiles run smoothly at ~130 px, so keep the betta close to that
     * even though the screen is much larger. */
    if (side > 220) {
        side = 220;
    }
    if (side < 160) {
        side = 160;
    }

    size_t buf_bytes = (size_t)side * (size_t)side * 4U + (size_t)LV_DRAW_BUF_ALIGN;
    s_betta_buf = lv_malloc(buf_bytes);
    if (s_betta_buf == NULL) {
        return;
    }
    memset(s_betta_buf, 0, buf_bytes);

    s_betta_overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(s_betta_overlay);
    lv_obj_set_size(s_betta_overlay, APP_VIEW_WIDTH, APP_VIEW_HEIGHT);
    lv_obj_set_pos(s_betta_overlay, 0, 0);
    lv_obj_set_scrollable(s_betta_overlay, false);
    lv_obj_set_style_bg_color(s_betta_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_betta_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_clickable(s_betta_overlay, true);
    lv_obj_add_event_cb(s_betta_overlay, ui_pages_betta_dismiss_cb, LV_EVENT_CLICKED, NULL);

    s_betta_lottie = lv_lottie_create(s_betta_overlay);
    lv_lottie_set_buffer(s_betta_lottie, side, side, s_betta_buf);
    lv_lottie_set_src_data(s_betta_lottie, betta_lottie_start,
                           (size_t)(betta_lottie_end - betta_lottie_start));
    lv_obj_set_size(s_betta_lottie, side, side);
    lv_obj_center(s_betta_lottie);

    lv_obj_move_foreground(s_betta_overlay);
}

static void ui_pages_betta_hide(void)
{
    if (s_betta_overlay != NULL) {
        lv_obj_del(s_betta_overlay);
        s_betta_overlay = NULL;
        s_betta_lottie  = NULL;
    }
    if (s_betta_buf != NULL) {
        lv_free(s_betta_buf);
        s_betta_buf = NULL;
    }
    s_betta_taps    = 0U;
    s_betta_last_ms = 0U;
}
#endif

/* Hidden: a double tap on the clock shows the last notification from Home Assistant again. */
#define UI_CLOCK_DOUBLE_TAP_MS 500U
static void ui_pages_clock_event_cb(lv_event_t *event)
{
    static uint32_t last_tap_ms = 0U;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    const uint32_t now = lv_tick_get();
    if (last_tap_ms != 0U && (now - last_tap_ms) <= UI_CLOCK_DOUBLE_TAP_MS) {
        last_tap_ms = 0U;
        (void)ui_notification_popup_show_last();
        return;
    }
    last_tap_ms = now;
}

static void ui_nav_home_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_page_count == 0) {
        return;
    }
    ui_pages_show_index(0);
#if LV_USE_LOTTIE && APP_UI_BETTA_LOTTIE_ASSET
    /* Easter egg: count taps on the home button. */
    uint32_t now = lv_tick_get();
    if (s_betta_last_ms != 0U && (now - s_betta_last_ms) > UI_BETTA_TAP_WINDOW_MS) {
        s_betta_taps = 0U;
    }
    s_betta_last_ms = now;
    s_betta_taps++;
    if (s_betta_taps >= UI_BETTA_TAP_TARGET) {
        s_betta_taps = 0U;
        ui_pages_betta_show();
    }
#endif
}

static void ui_nav_extra_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    uintptr_t slot = (uintptr_t)lv_event_get_user_data(event);
    if (slot >= (APP_MAX_PAGES - 1)) {
        return;
    }

    uint16_t page_index = s_nav_extra_page_index[slot];
    if (page_index >= s_page_count) {
        return;
    }
    ui_pages_show_index(page_index);
}

/* Glyph color for an integration: blue when connected, the theme's "off"
 * gray otherwise. */
static lv_color_t ui_pages_status_glyph_color(bool connected)
{
    return connected ? lv_color_hex(TOPBAR_STATUS_CONNECTED_COLOR) : lv_color_hex(APP_UI_COLOR_TOPBAR_STATUS_OFF);
}

/* Positions the right-hand chips from the screen edge inwards: Wi-Fi, MQTT when started, Home Assistant. Called
 * whenever the Wi-Fi chip changes width (the setup AP's "AP" text) or MQTT starts or stops. */
static void ui_pages_layout_topbar_right(void)
{
    if (s_wifi_icon == NULL || s_api_chip == NULL) {
        return;
    }
    lv_coord_t x = -TOPBAR_EDGE_PAD;
    lv_obj_align(s_wifi_icon, LV_ALIGN_RIGHT_MID, x, 0);
    x -= s_wifi_chip_w + TOPBAR_CHIP_GAP;
    if (s_mqtt_chip != NULL && !lv_obj_is_hidden(s_mqtt_chip)) {
        lv_obj_align(s_mqtt_chip, LV_ALIGN_RIGHT_MID, x, 0);
        x -= TOPBAR_STATUS_CHIP_W + TOPBAR_CHIP_GAP;
    }
    lv_obj_align(s_api_chip, LV_ALIGN_RIGHT_MID, x, 0);
    if (s_crash_warning != NULL) {
        x -= TOPBAR_STATUS_CHIP_W + TOPBAR_CHIP_GAP;
        lv_obj_align(s_crash_warning, LV_ALIGN_RIGHT_MID, x, 0);
    }
}

void ui_pages_set_crash_warning(bool visible)
{
    if (s_crash_warning != NULL && lv_obj_is_hidden(s_crash_warning) == visible) {
        lv_obj_set_hidden(s_crash_warning, !visible);
    }
}

/* MQTT has no push notification into the UI, so the glyph is polled: hidden
 * while MQTT is disabled (client never started), blue when connected,
 * "off" colored while a started client is disconnected. */
static void ui_pages_refresh_mqtt_glyph(void)
{
    if (s_api_mqtt_glyph == NULL || s_mqtt_chip == NULL || s_api_chip == NULL) {
        return;
    }
    const bool started = mqtt_mgr_is_started();
    if (lv_obj_is_hidden(s_mqtt_chip) == started) {
        lv_obj_set_hidden(s_mqtt_chip, !started);
        ui_pages_layout_topbar_right(); /* the HA chip makes room for the MQTT chip */
    }
    if (started) {
        lv_obj_set_style_text_color(s_api_mqtt_glyph, ui_pages_status_glyph_color(mqtt_mgr_is_connected()), LV_PART_MAIN);
    }
}

static void ui_pages_mqtt_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_pages_refresh_mqtt_glyph();
}

/* One status chip: a framed square-ish box that centers a single glyph. */
static lv_obj_t *ui_pages_create_status_chip(lv_event_cb_t clicked)
{
    lv_obj_t *chip = lv_obj_create(s_topbar);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, TOPBAR_STATUS_CHIP_W, 40);
    ui_pages_style_topbar_chip(chip);
    lv_obj_set_style_pad_left(chip, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(chip, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(chip, false);
    lv_obj_set_clickable(chip, true);
    lv_obj_set_ext_click_area(chip, TOPBAR_CHIP_TOUCH_PAD);
    lv_obj_add_event_cb(chip, clicked, LV_EVENT_CLICKED, NULL);
    return chip;
}

static void ui_pages_create_topbar(lv_obj_t *screen)
{
    const lv_coord_t topbar_h = APP_CONTENT_BOX_Y;

    s_topbar = lv_obj_create(screen);
    lv_obj_remove_style_all(s_topbar);
    lv_obj_set_size(s_topbar, APP_VIEW_WIDTH, topbar_h);
    lv_obj_set_pos(s_topbar, 0, 0);
    lv_obj_set_scrollable(s_topbar, false);
    lv_obj_set_style_radius(s_topbar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_topbar, lv_color_hex(APP_UI_COLOR_TOPBAR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_topbar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_topbar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_topbar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_topbar, lv_color_hex(APP_UI_COLOR_TOPBAR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_topbar, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_topbar, 0, LV_PART_MAIN);

    s_date_label = lv_label_create(s_topbar);
    lv_obj_set_width(s_date_label, APP_VIEW_WIDTH < 1000 ? 148 : 220);
    lv_obj_align(s_date_label, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(APP_UI_COLOR_TOPBAR_MUTED), LV_PART_MAIN);
    ui_text_set(s_date_label, UI_TEXT_META, true);
    lv_label_set_long_mode(s_date_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_date_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_text(s_date_label, "--.--.----");

    s_stock_chip = lv_obj_create(s_topbar);
    lv_obj_remove_style_all(s_stock_chip);
    lv_obj_set_size(s_stock_chip, APP_VIEW_WIDTH < 1000 ? 126 : 158, 40);
    lv_obj_align(s_stock_chip, LV_ALIGN_LEFT_MID, APP_VIEW_WIDTH < 1000 ? 166 : 232, 0);
    ui_pages_style_topbar_chip(s_stock_chip);
    lv_obj_set_style_pad_left(s_stock_chip, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_stock_chip, 8, LV_PART_MAIN);
    lv_obj_set_scrollable(s_stock_chip, false);
    lv_obj_set_hidden(s_stock_chip, true);
    lv_obj_set_clickable(s_stock_chip, true);
    lv_obj_add_event_cb(s_stock_chip, ui_pages_stock_chip_event_cb, LV_EVENT_DOUBLE_CLICKED, NULL);
    for (size_t i = 0; i < 3U; i++) {
        s_stock_rows[i] = lv_obj_create(s_stock_chip);
        lv_obj_remove_style_all(s_stock_rows[i]);
        lv_obj_set_size(s_stock_rows[i], LV_PCT(100), LV_PCT(100));
        lv_obj_center(s_stock_rows[i]);
        lv_obj_set_scrollable(s_stock_rows[i], false);
        lv_obj_set_clickable(s_stock_rows[i], false); /* the taps reach the chip */
        lv_obj_set_layout(s_stock_rows[i], LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(s_stock_rows[i], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(s_stock_rows[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(s_stock_rows[i], 3, LV_PART_MAIN);
        lv_obj_set_hidden(s_stock_rows[i], true);

        s_stock_labels[i] = lv_label_create(s_stock_rows[i]);
        lv_obj_set_width(s_stock_labels[i], LV_SIZE_CONTENT);
        lv_obj_set_style_text_color(s_stock_labels[i], lv_color_hex(APP_UI_COLOR_TOPBAR_TEXT), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_stock_labels[i], TOPBAR_DATE_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_align(s_stock_labels[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(s_stock_labels[i], LV_LABEL_LONG_CLIP);
        lv_label_set_text(s_stock_labels[i], "");

        s_stock_euro_labels[i] = lv_label_create(s_stock_rows[i]);
        lv_obj_set_width(s_stock_euro_labels[i], LV_SIZE_CONTENT);
        lv_obj_set_style_text_color(s_stock_euro_labels[i], lv_color_hex(APP_UI_COLOR_TOPBAR_TEXT), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_stock_euro_labels[i], TOPBAR_DATE_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_align(s_stock_euro_labels[i], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_label_set_text(s_stock_euro_labels[i], "\xE2\x82\xAC");
        lv_obj_set_hidden(s_stock_euro_labels[i], true);
    }

    s_time_label = lv_label_create(s_topbar);
    lv_obj_set_width(s_time_label, APP_VIEW_WIDTH < 1000 ? 88 : 180);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(APP_UI_COLOR_TOPBAR_TEXT), LV_PART_MAIN);
    ui_text_set(s_time_label, UI_TEXT_VALUE, true);
    lv_obj_set_style_text_align(s_time_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(s_time_label, "--:--");
    lv_obj_set_clickable(s_time_label, true);
    lv_obj_add_event_cb(s_time_label, ui_pages_clock_event_cb, LV_EVENT_CLICKED, NULL);

    s_weather_chip = lv_obj_create(s_topbar);
    lv_obj_remove_style_all(s_weather_chip);
    lv_obj_set_size(s_weather_chip, APP_VIEW_WIDTH < 1000 ? 104 : 116, 40);
    lv_obj_align(s_weather_chip, LV_ALIGN_RIGHT_MID, TOPBAR_WEATHER_X, 0);
    ui_pages_style_topbar_chip(s_weather_chip);
    lv_obj_set_style_pad_left(s_weather_chip, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_weather_chip, 8, LV_PART_MAIN);
    lv_obj_set_scrollable(s_weather_chip, false);
    lv_obj_set_hidden(s_weather_chip, true);

    s_weather_temp_label = lv_label_create(s_weather_chip);
    lv_obj_set_width(s_weather_temp_label, APP_VIEW_WIDTH < 1000 ? 42 : 50);
    lv_obj_align(s_weather_temp_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(s_weather_temp_label, lv_color_hex(APP_UI_COLOR_TOPBAR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_weather_temp_label, TOPBAR_DATE_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_weather_temp_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_text(s_weather_temp_label, "--");

    s_weather_icon_label = lv_label_create(s_weather_chip);
    lv_obj_set_width(s_weather_icon_label, APP_VIEW_WIDTH < 1000 ? 28 : 34);
    lv_obj_align(s_weather_icon_label, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_text_color(s_weather_icon_label, lv_color_hex(APP_UI_COLOR_WEATHER_ICON), LV_PART_MAIN);
    const lv_font_t *weather_font = mdi_font_weather_20();
    if (weather_font == NULL) {
        weather_font = mdi_font_weather_small();
    }
    if (weather_font != NULL) {
        lv_obj_set_style_text_font(s_weather_icon_label, weather_font, LV_PART_MAIN);
    }
    lv_obj_set_style_text_align(s_weather_icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(s_weather_icon_label, "");

    /* Home Assistant and MQTT chips: one MDI glyph each. Without the generated status font a plain "HA" text
     * label takes the Home Assistant glyph's place and MQTT shows no chip. */
    s_api_chip = ui_pages_create_status_chip(ui_pages_ha_chip_event_cb);
    s_mqtt_chip = ui_pages_create_status_chip(ui_pages_mqtt_chip_event_cb);
    lv_obj_set_hidden(s_mqtt_chip, true); /* shown once MQTT is started */
    s_crash_warning = lv_label_create(s_topbar);
    lv_label_set_text(s_crash_warning, APP_SYMBOL_ALERT);
    lv_obj_set_width(s_crash_warning, 32);
    lv_obj_set_style_text_font(s_crash_warning, TOPBAR_ICON_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_crash_warning, lv_color_hex(0xFBC02D), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_crash_warning, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_hidden(s_crash_warning, true);

    const lv_font_t *status_font = APP_VIEW_WIDTH < 1000 ? mdi_font_status_20() : mdi_font_status_24();

    if (status_font != NULL) {
        s_api_ha_glyph = lv_label_create(s_api_chip);
        lv_obj_set_style_text_font(s_api_ha_glyph, status_font, LV_PART_MAIN);
        lv_label_set_text(s_api_ha_glyph, MDI_GLYPH_HOME_ASSISTANT);
        lv_obj_set_style_text_color(s_api_ha_glyph, ui_pages_status_glyph_color(false), LV_PART_MAIN);

        s_api_mqtt_glyph = lv_label_create(s_mqtt_chip);
        lv_obj_set_style_text_font(s_api_mqtt_glyph, status_font, LV_PART_MAIN);
        lv_label_set_text(s_api_mqtt_glyph, MDI_GLYPH_MQTT);
        lv_obj_set_style_text_color(s_api_mqtt_glyph, ui_pages_status_glyph_color(false), LV_PART_MAIN);
    } else {
        s_api_icon = lv_label_create(s_api_chip);
        lv_obj_set_style_text_font(s_api_icon, TOPBAR_ICON_FONT, LV_PART_MAIN);
        lv_label_set_text(s_api_icon, ui_i18n_get("topbar.ha", "HA"));
        lv_obj_set_style_text_color(s_api_icon, ui_pages_status_glyph_color(false), LV_PART_MAIN);
    }

    ui_pages_refresh_mqtt_glyph();
    if (s_mqtt_status_timer == NULL) {
        s_mqtt_status_timer = lv_timer_create(ui_pages_mqtt_status_timer_cb, 2000, NULL);
    }

    s_wifi_icon = lv_label_create(s_topbar);
    s_wifi_chip_w = TOPBAR_STATUS_CHIP_W;
    lv_obj_set_width(s_wifi_icon, s_wifi_chip_w);
    ui_pages_style_topbar_chip(s_wifi_icon);
    lv_obj_set_ext_click_area(s_wifi_icon, TOPBAR_CHIP_TOUCH_PAD);
    lv_label_set_text(s_wifi_icon, APP_SYMBOL_WIFI_OFF);
    lv_obj_set_clickable(s_wifi_icon, true);
    lv_obj_add_event_cb(s_wifi_icon, ui_pages_wifi_chip_event_cb, LV_EVENT_CLICKED, NULL);
    ui_pages_layout_topbar_right();
}

static void ui_pages_create_nav(lv_obj_t *screen)
{
    s_nav_bar = lv_obj_create(screen);
    lv_obj_remove_style_all(s_nav_bar);
    lv_obj_set_size(s_nav_bar, APP_VIEW_WIDTH, 60);
    lv_obj_set_pos(s_nav_bar, 0, APP_VIEW_HEIGHT - 60);
    lv_obj_set_scrollable(s_nav_bar, false);
    lv_obj_set_style_radius(s_nav_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_nav_bar, lv_color_hex(APP_UI_COLOR_TOPBAR_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_nav_bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_nav_bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(s_nav_bar, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_nav_bar, lv_color_hex(APP_UI_COLOR_TOPBAR_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_nav_bar, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_nav_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(s_nav_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_nav_home_button = lv_obj_create(s_nav_bar);
    lv_obj_remove_style_all(s_nav_home_button);
    lv_obj_set_ext_click_area(s_nav_home_button, 14);
    lv_obj_set_clickable(s_nav_home_button, true);
    lv_obj_set_scrollable(s_nav_home_button, false);
    lv_obj_add_event_cb(s_nav_home_button, ui_nav_home_button_event_cb, LV_EVENT_CLICKED, NULL);

    s_nav_home_label = lv_label_create(s_nav_home_button);
    lv_label_set_text(s_nav_home_label, APP_SYMBOL_HOME);
    lv_obj_set_style_text_font(s_nav_home_label, TOPBAR_ICON_FONT, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_nav_home_button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_nav_home_button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_nav_home_button, 8, LV_PART_MAIN);

    s_nav_home_title = lv_label_create(s_nav_home_button);
    lv_label_set_text(s_nav_home_title, "");
    lv_obj_set_style_text_font(s_nav_home_title, NAV_TEXT_FONT, LV_PART_MAIN);
    lv_label_set_long_mode(s_nav_home_title, LV_LABEL_LONG_DOT);
    lv_obj_set_hidden(s_nav_home_title, true);

    for (uint16_t i = 0; i < (APP_MAX_PAGES - 1); i++) {
        lv_obj_t *btn = lv_obj_create(s_nav_bar);
        lv_obj_remove_style_all(btn);
        lv_obj_set_ext_click_area(btn, 10);
        lv_obj_set_clickable(btn, true);
        lv_obj_set_scrollable(btn, false);
        lv_obj_add_event_cb(btn, ui_nav_extra_button_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(btn, 6, LV_PART_MAIN);

        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, "");
        lv_obj_set_hidden(icon, true);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, "");
        lv_obj_set_style_text_font(label, NAV_TEXT_FONT, LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

        s_nav_extra_icons[i] = icon;
        s_nav_extra_buttons[i] = btn;
        s_nav_extra_labels[i] = label;
        s_nav_extra_page_index[i] = APP_MAX_PAGES;
        lv_obj_set_hidden(btn, true);
    }
}

void ui_pages_init(void)
{
    memset(s_pages, 0, sizeof(s_pages));
    s_page_count = 0;
    s_current_index = -1;
    s_background = NULL;
    s_topbar = NULL;
    s_content_box = NULL;
    s_date_label = NULL;
    s_time_label = NULL;
    s_stock_chip = NULL;
    memset(s_stock_rows, 0, sizeof(s_stock_rows));
    memset(s_stock_labels, 0, sizeof(s_stock_labels));
    memset(s_stock_euro_labels, 0, sizeof(s_stock_euro_labels));
    s_weather_chip = NULL;
    s_weather_temp_label = NULL;
    s_weather_icon_label = NULL;
    strlcpy(s_topbar_left_slots[0], "date", sizeof(s_topbar_left_slots[0]));
    for (size_t i = 1; i < APP_TOPBAR_LEFT_SLOTS; i++) {
        s_topbar_left_slots[i][0] = '\0';
    }
    s_topbar_weather_has_value = false;
    s_topbar_stock_has_value = false;
    s_wifi_icon = NULL;
    s_api_icon = NULL;
    s_api_chip = NULL;
    s_mqtt_chip = NULL;
    s_crash_warning = NULL;
    s_api_ha_glyph = NULL;
    s_api_mqtt_glyph = NULL;
    if (s_mqtt_status_timer != NULL) {
        lv_timer_delete(s_mqtt_status_timer);
        s_mqtt_status_timer = NULL;
    }
    s_nav_bar = NULL;
    s_nav_home_button = NULL;
    s_nav_home_label = NULL;
    s_nav_home_title = NULL;
    memset(s_nav_extra_buttons, 0, sizeof(s_nav_extra_buttons));
    memset(s_nav_extra_labels, 0, sizeof(s_nav_extra_labels));
    memset(s_nav_extra_icons, 0, sizeof(s_nav_extra_icons));
    memset(s_nav_extra_page_index, 0, sizeof(s_nav_extra_page_index));

#if LV_USE_LOTTIE && APP_UI_BETTA_LOTTIE_ASSET
    /* Screen will be cleaned below; the overlay is owned by the active screen.
     * Drop our handles and free the lottie render buffer to avoid a leak. */
    s_betta_overlay = NULL;
    s_betta_lottie  = NULL;
    if (s_betta_buf != NULL) {
        lv_free(s_betta_buf);
        s_betta_buf = NULL;
    }
    s_betta_taps    = 0U;
    s_betta_last_ms = 0U;
#endif

    lv_obj_t *screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_hex(APP_UI_COLOR_SCREEN_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    /* inherited by every label without a font of its own: readable, never LVGL's tiny compile-time default */
    lv_obj_set_style_text_font(screen, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

    s_background = lv_obj_create(screen);
    lv_obj_remove_style_all(s_background);
    lv_obj_set_size(s_background, APP_VIEW_WIDTH, APP_VIEW_HEIGHT);
    lv_obj_set_pos(s_background, 0, 0);
    lv_obj_set_scrollable(s_background, false);
    lv_obj_set_style_radius(s_background, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_background, lv_color_hex(APP_UI_COLOR_SCREEN_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_background, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_background, 0, LV_PART_MAIN);

    s_content_box = lv_obj_create(screen);
    lv_obj_remove_style_all(s_content_box);
    lv_obj_set_size(s_content_box, APP_VIEW_CONTENT_WIDTH, APP_VIEW_CONTENT_HEIGHT);
    lv_obj_set_pos(s_content_box, APP_CONTENT_BOX_X, APP_CONTENT_BOX_Y);
    lv_obj_set_scrollable(s_content_box, false);
    lv_obj_set_style_radius(s_content_box, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_content_box, ui_style_page_bg(lv_color_hex(APP_UI_COLOR_CONTENT_BG)), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_content_box, LV_OPA_COVER, LV_PART_MAIN);
#if APP_UI_REWORK_V2
    lv_obj_set_style_border_width(s_content_box, ui_style_traits()->frameless_page ? 0 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_content_box, lv_color_hex(APP_UI_COLOR_CONTENT_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_content_box, LV_OPA_70, LV_PART_MAIN);
#else
    lv_obj_set_style_border_width(s_content_box, 0, LV_PART_MAIN);
#endif
    lv_obj_set_style_pad_all(s_content_box, 0, LV_PART_MAIN);
    ui_style_glass_background(s_content_box, lv_color_hex(APP_UI_COLOR_CONTENT_BG), lv_color_hex(APP_UI_COLOR_NAV_TAB_ACTIVE));

    ui_pages_create_topbar(screen);
    ui_pages_create_nav(screen);

    time_t now = time(NULL);
    struct tm info = {0};
    localtime_r(&now, &info);
    ui_pages_set_topbar_datetime(&info);
    ui_pages_set_topbar_status(false, false, false, false);
    ui_pages_apply_tab_style(0);
}

void ui_pages_reset(void)
{
    ui_pages_init();
}

lv_obj_t *ui_pages_add(const char *page_id, const char *title)
{
    if (s_page_count >= APP_MAX_PAGES || page_id == NULL || page_id[0] == '\0' || s_content_box == NULL) {
        return NULL;
    }

    uint16_t index = s_page_count;
    snprintf(s_pages[index].id, sizeof(s_pages[index].id), "%s", page_id);
    snprintf(s_pages[index].title, sizeof(s_pages[index].title), "%s", (title && title[0]) ? title : page_id);

    lv_obj_t *container = lv_obj_create(s_content_box);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, APP_VIEW_CONTENT_WIDTH, APP_VIEW_CONTENT_HEIGHT);
    lv_obj_set_pos(container, 0, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(container, display_geometry_portrait());
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_hidden(container, true);
    s_pages[index].container = container;

    s_page_count++;
    ui_pages_apply_tab_style((uint16_t)(s_current_index >= 0 ? s_current_index : 0));
    return container;
}

void ui_pages_set_icon(const char *page_id, const char *icon_key)
{
    for (uint16_t i = 0; i < s_page_count; i++) {
        if (page_id != NULL && strncmp(page_id, s_pages[i].id, APP_MAX_PAGE_ID_LEN) == 0) {
            snprintf(s_pages[i].icon, sizeof(s_pages[i].icon), "%s", icon_key != NULL ? icon_key : "");
            ui_pages_apply_tab_style((uint16_t)(s_current_index >= 0 ? s_current_index : 0));
            return;
        }
    }
}

/* Time from a page switch to the first finished frame (draw plus flush), logged for tuning. */
static int64_t s_switch_us = 0;
static char s_switch_page[APP_MAX_PAGE_ID_LEN];

static void ui_pages_refr_finish_cb(lv_event_t *e)
{
    (void)e;
    /* Every finished refresh within 700 ms of a switch: the last one is when the page is complete. */
    if (s_switch_us != 0) {
        const int ms = (int)((esp_timer_get_time() - s_switch_us) / 1000);
        if (ms > 700) {
            s_switch_us = 0;
        } else {
            ESP_LOGI("ui_pages", "page %s: frame done %d ms after the switch", s_switch_page, ms);
        }
    }
}

bool ui_pages_show_index(uint16_t index)
{
    if (index >= s_page_count) {
        return false;
    }

    for (uint16_t i = 0; i < s_page_count; i++) {
        if (s_pages[i].container == NULL) {
            continue;
        }
        if (i == index) {
            lv_obj_set_hidden(s_pages[i].container, false);
        } else {
            lv_obj_set_hidden(s_pages[i].container, true);
        }
    }
    s_current_index = (int16_t)index;
    {
        static bool s_refr_cb_added = false;
        lv_display_t *disp = lv_display_get_default();
        if (!s_refr_cb_added && disp != NULL) {
            lv_display_add_event_cb(disp, ui_pages_refr_finish_cb, LV_EVENT_REFR_READY, NULL);
            s_refr_cb_added = true;
        }
        strlcpy(s_switch_page, s_pages[index].id, sizeof(s_switch_page));
        s_switch_us = esp_timer_get_time();
    }
    ui_pages_apply_tab_style(index);
    if (s_show_cb != NULL) {
        s_show_cb(s_pages[index].id, index);
    }
    return true;
}

bool ui_pages_show(const char *page_id)
{
    if (page_id == NULL) {
        return false;
    }
    for (uint16_t i = 0; i < s_page_count; i++) {
        if (strncmp(page_id, s_pages[i].id, APP_MAX_PAGE_ID_LEN) == 0) {
            return ui_pages_show_index(i);
        }
    }
    return false;
}

bool ui_pages_next(void)
{
    if (s_page_count == 0) {
        return false;
    }
    uint16_t next = (uint16_t)(((s_current_index < 0 ? 0 : s_current_index) + 1) % s_page_count);
    return ui_pages_show_index(next);
}

const char *ui_pages_current_id(void)
{
    if (s_current_index < 0 || (uint16_t)s_current_index >= s_page_count) {
        return "";
    }
    return s_pages[s_current_index].id;
}

uint16_t ui_pages_count(void)
{
    return s_page_count;
}

/* The Wi-Fi symbol shows the signal in three steps while connected, polled every 30 s without
 * blocking (wifi_mgr_try_get_sta_rssi(); a busy Wi-Fi API just skips a round). */
#define TOPBAR_WIFI_SIGNAL_POLL_MS 30000
static lv_timer_t *s_wifi_signal_timer = NULL;
static int s_wifi_signal_level = WIFI_SIGNAL_UNKNOWN;
static bool s_wifi_show_signal = false;

static const char *ui_pages_wifi_signal_symbol(int level)
{
    switch (level) {
    case WIFI_SIGNAL_WEAK:
        return APP_SYMBOL_WIFI_WEAK;
    case WIFI_SIGNAL_MEDIUM:
        return APP_SYMBOL_WIFI_MEDIUM;
    default:
        return APP_SYMBOL_WIFI_STRONG; /* also before the first reading */
    }
}

static void ui_pages_wifi_signal_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_wifi_show_signal || s_wifi_icon == NULL) {
        return;
    }
    int8_t rssi = 0;
    if (wifi_mgr_try_get_sta_rssi(&rssi) != ESP_OK) {
        return;
    }
    const int level = wifi_signal_level(rssi, s_wifi_signal_level);
    if (level != s_wifi_signal_level) {
        s_wifi_signal_level = level;
        lv_label_set_text(s_wifi_icon, ui_pages_wifi_signal_symbol(level));
    }
}

void ui_pages_set_topbar_status(
    bool wifi_connected, bool wifi_setup_ap_active, bool api_connected, bool api_initial_sync_done)
{
    ui_conn_popup_set_api_status(api_connected, api_initial_sync_done);
    lv_color_t on = lv_color_hex(APP_UI_COLOR_TOPBAR_STATUS_ON);
    lv_color_t off = lv_color_hex(APP_UI_COLOR_TOPBAR_STATUS_OFF);

    if (s_wifi_icon != NULL) {
        char wifi_text[32] = {0};
        snprintf(wifi_text, sizeof(wifi_text), "%s", APP_SYMBOL_WIFI_OFF);
        lv_color_t wifi_color = off;
        if (wifi_setup_ap_active) {
            snprintf(wifi_text, sizeof(wifi_text), "%s %s", ui_i18n_get("topbar.ap", "AP"), APP_SYMBOL_WIFI);
            wifi_color = on;
        } else if (wifi_connected) {
            /* Same blue as the Home Assistant/MQTT glyphs once actually joined to a network,
             * not just the neutral "on" gray -- matches ui_pages_status_glyph_color() below. */
            if (!s_wifi_show_signal) {
                s_wifi_signal_level = WIFI_SIGNAL_UNKNOWN; /* a new connection: no reading yet */
            }
            snprintf(wifi_text, sizeof(wifi_text), "%s", ui_pages_wifi_signal_symbol(s_wifi_signal_level));
            wifi_color = ui_pages_status_glyph_color(true);
        }
        const bool show_signal = wifi_connected && !wifi_setup_ap_active;
        if (show_signal && !s_wifi_show_signal) {
            if (s_wifi_signal_timer == NULL) {
                s_wifi_signal_timer = lv_timer_create(ui_pages_wifi_signal_timer_cb, TOPBAR_WIFI_SIGNAL_POLL_MS, NULL);
            }
            if (s_wifi_signal_timer != NULL) {
                lv_timer_ready(s_wifi_signal_timer); /* first reading right after connecting */
            }
        }
        s_wifi_show_signal = show_signal;
        lv_label_set_text(s_wifi_icon, wifi_text);
        lv_obj_set_style_text_color(s_wifi_icon, wifi_color, LV_PART_MAIN);
        const lv_coord_t wifi_w = wifi_setup_ap_active ? TOPBAR_WIFI_AP_CHIP_W : TOPBAR_STATUS_CHIP_W;
        if (s_wifi_chip_w != wifi_w) {
            s_wifi_chip_w = wifi_w;
            lv_obj_set_width(s_wifi_icon, wifi_w);
            ui_pages_layout_topbar_right();
        }
    }

    /* Connected and synced: solid blue. Connected but still syncing: dimmed
     * blue. Otherwise the "off" gray. */
    const lv_color_t api_color = ui_pages_status_glyph_color(api_connected);
    const lv_opa_t api_opa = (api_connected && !api_initial_sync_done) ? LV_OPA_60 : LV_OPA_COVER;
    if (s_api_ha_glyph != NULL) {
        lv_obj_set_style_text_color(s_api_ha_glyph, api_color, LV_PART_MAIN);
        lv_obj_set_style_text_opa(s_api_ha_glyph, api_opa, LV_PART_MAIN);
    }
    if (s_api_icon != NULL) {
        lv_obj_set_style_text_color(s_api_icon, api_color, LV_PART_MAIN);
        lv_obj_set_style_text_opa(s_api_icon, api_opa, LV_PART_MAIN);
    }
    ui_pages_refresh_mqtt_glyph();
}

void ui_pages_set_topbar_left_slots(const char *slot_1, const char *slot_2, const char *slot_3)
{
    const char *slots[APP_TOPBAR_LEFT_SLOTS] = {slot_1, slot_2, slot_3};
    for (size_t i = 0; i < APP_TOPBAR_LEFT_SLOTS; i++) {
        snprintf(s_topbar_left_slots[i], sizeof(s_topbar_left_slots[i]), "%s", slots[i] != NULL ? slots[i] : "");
    }
    ui_pages_layout_topbar_left();
}

void ui_pages_set_topbar_datetime(const struct tm *timeinfo)
{
    if (timeinfo == NULL) {
        return;
    }

    char date_buf[32] = {0};
    char time_buf[16] = {0};
    snprintf(
        date_buf, sizeof(date_buf), "%02d.%02d.%04d", timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);

    if (s_date_label != NULL) {
        lv_label_set_text(s_date_label, date_buf);
    }
    if (s_time_label != NULL) {
        lv_label_set_text(s_time_label, time_buf);
    }
    ui_pages_layout_topbar_left();
}

void ui_pages_set_topbar_weather(bool visible, float temperature, const char *unit, uint32_t icon_codepoint)
{
    (void)unit;
    if (s_weather_chip == NULL || s_weather_temp_label == NULL || s_weather_icon_label == NULL) {
        return;
    }
    if (!visible || !ui_pages_topbar_slot_enabled("weather")) {
        s_topbar_weather_has_value = false;
        ui_pages_layout_topbar_left();
        return;
    }

    char temp_text[16] = {0};
    /* Trailing space: the label is right aligned, so it keeps the temperature off the icon. */
    snprintf(temp_text, sizeof(temp_text), "%.0f\xC2\xB0 ", (double)temperature);
    lv_label_set_text(s_weather_temp_label, temp_text);

    char icon_text[5] = {0};
    if (ui_pages_utf8_from_codepoint(icon_codepoint, icon_text)) {
        lv_label_set_text(s_weather_icon_label, icon_text);
    } else {
        lv_label_set_text(s_weather_icon_label, "");
    }
    s_topbar_weather_has_value = true;
    ui_pages_layout_topbar_left();
}

void ui_pages_set_topbar_stocks(const ui_pages_stock_item_t *items, size_t count)
{
    if (s_stock_chip == NULL) {
        return;
    }
    if (items == NULL || count == 0 || !ui_pages_topbar_slot_enabled("stock")) {
        s_topbar_stock_has_value = false;
        ui_pages_layout_topbar_left();
        return;
    }

    size_t visible_count = 0;
    size_t max_items = 1U;
    for (size_t i = 0; i < 3U; i++) {
        if (s_stock_labels[i] == NULL) {
            continue;
        }
        if (i < max_items && i < count && items[i].symbol[0] != '\0' && items[i].value[0] != '\0') {
            strlcpy(s_stock_symbol, items[i].symbol, sizeof(s_stock_symbol));
            strlcpy(s_stock_value, items[i].value, sizeof(s_stock_value));
            s_stock_euro = items[i].value_is_euro;
            lv_label_set_text_fmt(s_stock_labels[i], "%s %s", s_stock_symbol, s_stock_value); /* refitted by the layout */
            if (s_stock_rows[i] != NULL) {
                lv_obj_set_hidden(s_stock_rows[i], false);
            }
            lv_obj_set_hidden(s_stock_labels[i], false);
            if (s_stock_euro_labels[i] != NULL) {
                if (items[i].value_is_euro) {
                    lv_obj_set_hidden(s_stock_euro_labels[i], false);
                } else {
                    lv_obj_set_hidden(s_stock_euro_labels[i], true);
                }
            }
            visible_count++;
        } else {
            lv_label_set_text(s_stock_labels[i], "");
            if (s_stock_rows[i] != NULL) {
                lv_obj_set_hidden(s_stock_rows[i], true);
            }
            lv_obj_set_hidden(s_stock_labels[i], true);
            if (s_stock_euro_labels[i] != NULL) {
                lv_obj_set_hidden(s_stock_euro_labels[i], true);
            }
        }
    }

    if (visible_count == 0) {
        s_topbar_stock_has_value = false;
    } else {
        s_topbar_stock_has_value = true;
    }
    ui_pages_layout_topbar_left();
}
