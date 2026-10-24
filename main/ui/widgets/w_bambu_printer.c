/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Bambu printer widget (bambu_printer): the print status of the Home Assistant Bambu Lab integration, an original
 * stylised printer with the nozzle and bed temperatures, progress and remaining minutes, and three buttons: Pause or
 * Resume, Status (a read-only detail popup) and Stop (after a confirmation). Every value and every button is an
 * explicit entity binding (layout/bambu_printer_slots.h); a command is a button.press through the panel's
 * Home Assistant client. The printer state shown is always Home Assistant's: a command is only "not confirmed" when
 * the expected status does not follow within 15 seconds, never retried and never assumed.
 *
 * The state rules are in bambu_printer_policy.c (host tested), the drawing in bambu_printer_illustration.c.
 */
#include "ui/ui_widget_factory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "ha/ha_client.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_option_picker.h"
#include "ui/ui_popup.h"
#include "ui/ui_camera_view.h"
#include "ui/widgets/bambu_printer_illustration.h"
#include "ui/widgets/bambu_printer_policy.h"

static const char *TAG = "w_bambu";

#define BAMBU_PAD 12
#define BAMBU_GAP 8
#define BAMBU_BTN_MIN_H 48
#define BAMBU_BAR_H 8
#define BAMBU_PENDING_MS 15000U
#define BAMBU_NOTICE_MS 6000U
#define BAMBU_TICK_MS 500U
#define BAMBU_WIDE_MIN_W 520
#define BAMBU_STAGE_ROW_MIN_H 360

enum {
    ROW_JOB,
    ROW_STATUS,
    ROW_STAGE,
    ROW_NOZZLE,
    ROW_BED,
    ROW_PROGRESS,
    ROW_REMAINING,
    ROW_COUNT,
};

enum {
    BTN_PRIMARY,
    BTN_STATUS,
    BTN_STOP,
    BTN_COUNT,
};

typedef struct {
    char entity[UI_BAMBU_EXTRA_JOB + 4][APP_MAX_ENTITY_ID_LEN]; /* see ent_* below */
    char title[APP_MAX_NAME_LEN];
    /* cached Home Assistant values, "" = nothing received */
    char raw_state[24];
    char nozzle[24];
    char nozzle_unit[8];
    char bed[24];
    char bed_unit[8];
    char progress[24];
    char remaining[24];
    char remaining_unit[12];
    char stage[48];
    char job[80];
    char button_state[3][16]; /* pause, resume, stop */
    bambu_state_t state;
    bool online;
    bool hide_name;
    bool stage_row;
    bool suppress_click;
    int pending; /* bambu_command_t, or -1 */
    uint32_t pending_deadline;
    uint32_t notice_deadline;
    char notice[48];
    lv_timer_t *timer;
    lv_obj_t *card;
    lv_obj_t *name_label;
    lv_obj_t *status_label;
    lv_obj_t *stage_label;
    lv_obj_t *bar;
    lv_obj_t *progress_label;
    lv_obj_t *remaining_label;
    lv_obj_t *nozzle_label;
    lv_obj_t *bed_label;
    lv_obj_t *btn[BTN_COUNT];
    lv_obj_t *btn_label[BTN_COUNT];
    bambu_illustration_t *art;
    lv_obj_t *picker;
    lv_obj_t *detail;
    lv_obj_t *camera;
    char camera_entity[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *detail_value[ROW_COUNT];
    char stop_title[48];
} w_bambu_ctx_t;

/* Indices into ctx->entity[] (the widget's own order; the layout order is in bambu_printer_slots.h). */
enum {
    ENT_STATE,
    ENT_NOZZLE,
    ENT_BED,
    ENT_PROGRESS,
    ENT_REMAINING,
    ENT_PAUSE,
    ENT_RESUME,
    ENT_STOP,
    ENT_STAGE,
    ENT_JOB,
    ENT_COUNT,
};
_Static_assert(ENT_COUNT <= UI_BAMBU_EXTRA_JOB + 4, "entity table too small");
_Static_assert(UI_BAMBU_EXTRA_CAMERA < UI_WIDGET_EXTRA_ENTITIES, "bambu_printer needs eight extra entity slots");

static ha_state_t *s_scratch; /* sequential local model reads; ha_state_t is far too big for the stack */

static void bambu_render(w_bambu_ctx_t *ctx);
static void bambu_layout(w_bambu_ctx_t *ctx);

/* ---- text helpers ------------------------------------------------------------------------------------------- */

static const char *tr(const char *key, const char *fallback)
{
    return ui_i18n_get(key, fallback);
}

static void format_temp(const char *value, const char *unit, char *out, size_t out_len)
{
    float number = 0.0f;
    if (!bambu_parse_number(value, &number)) {
        snprintf(out, out_len, "\xE2\x80\x94"); /* an em dash: never a zero for a missing reading */
        return;
    }
    snprintf(out, out_len, "%d %s", (int)lroundf(number), unit[0] != '\0' ? unit : "\xC2\xB0" "C");
}

static bool job_readings_visible(const w_bambu_ctx_t *ctx)
{
    return bambu_state_job_active(ctx->state) || bambu_state_job_last(ctx->state);
}

static void format_progress(const w_bambu_ctx_t *ctx, char *out, size_t out_len)
{
    int percent = 0;
    if (bambu_progress_percent(ctx->progress, &percent)) {
        snprintf(out, out_len, "%d%%", percent);
    } else {
        snprintf(out, out_len, "\xE2\x80\x94");
    }
}

static void format_remaining(const w_bambu_ctx_t *ctx, char *out, size_t out_len)
{
    int minutes = 0;
    if (bambu_remaining_minutes(ctx->remaining, ctx->remaining_unit, &minutes)) {
        snprintf(out, out_len, "%d %s", minutes, tr("bambu.min", "min"));
    } else {
        snprintf(out, out_len, "\xE2\x80\x94");
    }
}

/* The stage in the panel language (bambu.step.<key> of the translation table), else the key as words: "heating_hotend"
 * -> "Heating hotend"; the stages that only repeat the status are dropped. */
static void format_stage(const w_bambu_ctx_t *ctx, char *out, size_t out_len)
{
    out[0] = '\0';
    const char *raw = ctx->stage;
    if (raw[0] == '\0' || strcmp(raw, "unavailable") == 0 || strcmp(raw, "unknown") == 0 || strcmp(raw, "printing") == 0 ||
        strcmp(raw, "idle") == 0 || !bambu_state_job_active(ctx->state)) {
        return;
    }
    char key[96];
    snprintf(key, sizeof(key), "bambu.step.%s", raw);
    const char *named = tr(key, NULL);
    if (named[0] != '\0') {
        snprintf(out, out_len, "%s", named);
        return;
    }
    size_t i = 0;
    for (; raw[i] != '\0' && i + 1 < out_len; i++) {
        out[i] = raw[i] == '_' ? ' ' : raw[i];
    }
    out[i] = '\0';
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 'a' + 'A');
    }
}

static const char *status_text(const w_bambu_ctx_t *ctx)
{
    if (ctx->state == BAMBU_STATE_OTHER && ctx->raw_state[0] != '\0') {
        return ctx->raw_state; /* an unknown status stays distinguishable */
    }
    return tr(bambu_state_key(ctx->state), bambu_state_fallback(ctx->state));
}

static lv_color_t status_color(const w_bambu_ctx_t *ctx)
{
    switch (ctx->state) {
    case BAMBU_STATE_RUNNING:
    case BAMBU_STATE_FINISH:
        return lv_color_hex(APP_UI_COLOR_OK);
    case BAMBU_STATE_PAUSE:
    case BAMBU_STATE_PREPARE:
    case BAMBU_STATE_SLICING:
    case BAMBU_STATE_INIT:
        return lv_color_hex(0xF2C14E);
    case BAMBU_STATE_FAILED:
        return lv_color_hex(APP_UI_COLOR_ERROR);
    case BAMBU_STATE_IDLE:
        return lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY);
    default:
        return lv_color_hex(APP_UI_COLOR_TEXT_MUTED);
    }
}

/* ---- model access -------------------------------------------------------------------------------------------- */

static const char *attr_unit(const ha_state_t *state, char *out, size_t out_len)
{
    out[0] = '\0';
    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (attrs != NULL) {
        const cJSON *unit = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
        if (cJSON_IsString(unit) && unit->valuestring != NULL) {
            snprintf(out, out_len, "%s", unit->valuestring);
        }
        cJSON_Delete(attrs);
    }
    return out;
}

static void bambu_clear_pending(w_bambu_ctx_t *ctx)
{
    ctx->pending = -1;
    ctx->pending_deadline = 0;
}

static void bambu_set_notice(w_bambu_ctx_t *ctx, const char *text)
{
    snprintf(ctx->notice, sizeof(ctx->notice), "%s", text);
    ctx->notice_deadline = lv_tick_get() + BAMBU_NOTICE_MS;
    if (ctx->notice_deadline == 0U) {
        ctx->notice_deadline = 1U;
    }
}

/* Stores one Home Assistant state in the cache slot(s) it belongs to. Returns true when the printer status changed. */
static bool bambu_store(w_bambu_ctx_t *ctx, const ha_state_t *state)
{
    bool status_changed = false;
    const char *id = state->entity_id;
#define IS(slot) (ctx->entity[slot][0] != '\0' && strncmp(id, ctx->entity[slot], APP_MAX_ENTITY_ID_LEN) == 0)
    if (IS(ENT_STATE)) {
        const bambu_state_t next = bambu_state_parse(state->state);
        status_changed = next != ctx->state;
        ctx->state = next;
        strlcpy(ctx->raw_state, state->state, sizeof(ctx->raw_state));
    }
    if (IS(ENT_NOZZLE)) {
        strlcpy(ctx->nozzle, state->state, sizeof(ctx->nozzle));
        attr_unit(state, ctx->nozzle_unit, sizeof(ctx->nozzle_unit));
    }
    if (IS(ENT_BED)) {
        strlcpy(ctx->bed, state->state, sizeof(ctx->bed));
        attr_unit(state, ctx->bed_unit, sizeof(ctx->bed_unit));
    }
    if (IS(ENT_PROGRESS)) {
        strlcpy(ctx->progress, state->state, sizeof(ctx->progress));
    }
    if (IS(ENT_REMAINING)) {
        strlcpy(ctx->remaining, state->state, sizeof(ctx->remaining));
        attr_unit(state, ctx->remaining_unit, sizeof(ctx->remaining_unit));
    }
    if (IS(ENT_STAGE)) {
        strlcpy(ctx->stage, state->state, sizeof(ctx->stage));
    }
    if (IS(ENT_JOB)) {
        strlcpy(ctx->job, state->state, sizeof(ctx->job));
    }
    for (int i = 0; i < 3; i++) {
        if (IS(ENT_PAUSE + i)) {
            strlcpy(ctx->button_state[i], state->state, sizeof(ctx->button_state[i]));
        }
    }
#undef IS
    return status_changed;
}

/* Re-reads every mapped entity from the in-memory model, so nothing cached across a reconnect enables a command. */
static void bambu_refresh_from_model(w_bambu_ctx_t *ctx)
{
    if (s_scratch == NULL) {
        s_scratch = ui_calloc_prefer_psram(1, sizeof(*s_scratch));
        if (s_scratch == NULL) {
            return;
        }
    }
    for (int i = 0; i < ENT_COUNT; i++) {
        if (ctx->entity[i][0] == '\0') {
            continue;
        }
        memset(s_scratch, 0, sizeof(*s_scratch));
        if (ha_model_get_state(ctx->entity[i], s_scratch)) {
            (void)bambu_store(ctx, s_scratch);
        } else if (i == ENT_STATE) {
            ctx->state = BAMBU_STATE_MISSING;
            ctx->raw_state[0] = '\0';
        } else if (i >= ENT_PAUSE && i <= ENT_STOP) {
            ctx->button_state[i - ENT_PAUSE][0] = '\0';
        }
    }
}

static bool bambu_online(void)
{
    return ha_client_is_connected() && ha_client_is_initial_sync_done();
}

static bambu_actions_t bambu_current_actions(const w_bambu_ctx_t *ctx)
{
    return bambu_actions(ctx->state, ctx->online, ctx->pending >= 0, bambu_button_usable(ctx->button_state[0]),
                         bambu_button_usable(ctx->button_state[1]), bambu_button_usable(ctx->button_state[2]));
}

/* ---- commands ------------------------------------------------------------------------------------------------ */

static void bambu_send(w_bambu_ctx_t *ctx, bambu_command_t command)
{
    ctx->online = bambu_online();
    bambu_refresh_from_model(ctx); /* the freshest state and button availability, not what was cached */
    const bambu_actions_t actions = bambu_current_actions(ctx);
    bool allowed = false;
    int entity = ENT_PAUSE;
    if (command == BAMBU_CMD_STOP) {
        allowed = actions.stop_enabled;
        entity = ENT_STOP;
    } else if (command == BAMBU_CMD_RESUME) {
        allowed = actions.primary_enabled && actions.primary_resume;
        entity = ENT_RESUME;
    } else {
        allowed = actions.primary_enabled && !actions.primary_resume;
        entity = ENT_PAUSE;
    }
    if (!allowed || ctx->entity[entity][0] == '\0') {
        bambu_render(ctx);
        return;
    }
    char payload[APP_MAX_ENTITY_ID_LEN + 24];
    snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"}", ctx->entity[entity]);
    const esp_err_t err = ha_client_call_service("button", "press", payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "button.press failed (%s): %s", ctx->entity[entity], esp_err_to_name(err));
        bambu_set_notice(ctx, tr("bambu.send_failed", "Could not send command"));
    } else {
        /* Only the local send is known here, not the printer's answer: wait for the expected status. */
        ctx->pending = (int)command;
        ctx->pending_deadline = lv_tick_get() + BAMBU_PENDING_MS;
        ctx->notice[0] = '\0';
        ctx->notice_deadline = 0;
    }
    bambu_render(ctx);
}

static void bambu_primary_clicked_cb(lv_event_t *event)
{
    w_bambu_ctx_t *ctx = (w_bambu_ctx_t *)lv_event_get_user_data(event);
    ctx->online = bambu_online();
    const bambu_actions_t actions = bambu_current_actions(ctx);
    if (!actions.primary_enabled) {
        return;
    }
    bambu_send(ctx, actions.primary_resume ? BAMBU_CMD_RESUME : BAMBU_CMD_PAUSE);
}

static void stop_label_cb(void *user, size_t index, char *out, size_t out_len)
{
    (void)user;
    snprintf(out, out_len, "%s", index == 0 ? tr("bambu.cancel", "Cancel") : tr("bambu.stop", "Stop"));
}

static void stop_chosen_cb(void *user, size_t index)
{
    w_bambu_ctx_t *ctx = (w_bambu_ctx_t *)user;
    if (index == 1) {
        bambu_send(ctx, BAMBU_CMD_STOP);
    }
}

static void stop_closed_cb(void *user)
{
    ((w_bambu_ctx_t *)user)->picker = NULL;
}

static void bambu_close_popups(w_bambu_ctx_t *ctx)
{
    if (ctx->camera) lv_obj_delete(ctx->camera);
    if (ctx->picker != NULL) {
        ui_option_picker_close(ctx->picker); /* its closed callback clears the handle */
    }
    if (ctx->detail != NULL) {
        lv_obj_delete(ctx->detail);
    }
}

static void bambu_stop_clicked(lv_event_t *event)
{
    w_bambu_ctx_t *ctx = (w_bambu_ctx_t *)lv_event_get_user_data(event);
    ctx->online = bambu_online();
    if (!bambu_current_actions(ctx).stop_enabled || ctx->picker != NULL) {
        return;
    }
    if (ctx->detail != NULL) {
        lv_obj_delete(ctx->detail); /* one popup at a time: details make way for the confirmation */
    }
    snprintf(ctx->stop_title, sizeof(ctx->stop_title), "%s", tr("bambu.stop_title", "Stop this print?"));
    const ui_option_picker_cfg_t cfg = {
        .title = ctx->stop_title,
        .count = 2,
        .current = 0,
        .label = stop_label_cb,
        .chosen = stop_chosen_cb,
        .closed = stop_closed_cb,
        .user = ctx,
    };
    ctx->picker = ui_option_picker_open(&cfg);
}

/* ---- detail popup -------------------------------------------------------------------------------------------- */

static void detail_fill(w_bambu_ctx_t *ctx)
{
    if (ctx->detail == NULL) {
        return;
    }
    char text[96];
    char stage[48];
    format_stage(ctx, stage, sizeof(stage));
    const bool readings = job_readings_visible(ctx);
    const char *dash = "\xE2\x80\x94";

    snprintf(text, sizeof(text), "%s", ctx->job[0] != '\0' && strcmp(ctx->job, "unavailable") != 0 &&
                                              strcmp(ctx->job, "unknown") != 0 && readings ? ctx->job : dash);
    lv_label_set_text(ctx->detail_value[ROW_JOB], text);
    lv_label_set_text(ctx->detail_value[ROW_STATUS], status_text(ctx));
    lv_label_set_text(ctx->detail_value[ROW_STAGE], stage[0] != '\0' ? stage : dash);
    lv_obj_set_style_text_color(ctx->detail_value[ROW_STATUS], status_color(ctx), LV_PART_MAIN);
    format_temp(ctx->nozzle, ctx->nozzle_unit, text, sizeof(text));
    lv_label_set_text(ctx->detail_value[ROW_NOZZLE], text);
    format_temp(ctx->bed, ctx->bed_unit, text, sizeof(text));
    lv_label_set_text(ctx->detail_value[ROW_BED], text);
    if (readings) {
        format_progress(ctx, text, sizeof(text));
        lv_label_set_text(ctx->detail_value[ROW_PROGRESS], text);
        format_remaining(ctx, text, sizeof(text));
        lv_label_set_text(ctx->detail_value[ROW_REMAINING], text);
    } else {
        lv_label_set_text(ctx->detail_value[ROW_PROGRESS], dash);
        lv_label_set_text(ctx->detail_value[ROW_REMAINING], dash);
    }
}

static void detail_deleted_cb(lv_event_t *event)
{
    w_bambu_ctx_t *ctx = (w_bambu_ctx_t *)lv_event_get_user_data(event);
    ctx->detail = NULL;
    memset(ctx->detail_value, 0, sizeof(ctx->detail_value));
}

static void detail_close_cb(lv_event_t *event)
{
    w_bambu_ctx_t *ctx = (w_bambu_ctx_t *)lv_event_get_user_data(event);
    if (ctx->detail != NULL) {
        lv_obj_delete_async(ctx->detail);
    }
}

static void detail_backdrop_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) == lv_event_get_current_target(event)) {
        detail_close_cb(event);
    }
}

static void camera_closed(void *user) { ((w_bambu_ctx_t *)user)->camera = NULL; }

static void camera_clicked(lv_event_t *event)
{
    w_bambu_ctx_t *ctx = lv_event_get_user_data(event);
    if (!ctx->camera) ctx->camera = ui_camera_view_open(ctx->camera_entity, ctx->title, camera_closed, ctx);
}

static void bambu_status_clicked(lv_event_t *event)
{
    w_bambu_ctx_t *ctx = (w_bambu_ctx_t *)lv_event_get_user_data(event);
    if (ctx->detail != NULL || ctx->picker != NULL) {
        return;
    }
    ctx->online = bambu_online();
    bambu_refresh_from_model(ctx);
    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    const int screen_w = lv_obj_get_width(screen) > 0 ? lv_obj_get_width(screen) : 720;
    const int screen_h = lv_obj_get_height(screen) > 0 ? lv_obj_get_height(screen) : 720;

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    ctx->detail = overlay;
    lv_obj_set_size(overlay, screen_w, screen_h);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(overlay, detail_backdrop_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(overlay, detail_deleted_cb, LV_EVENT_DELETE, ctx);

    int card_w = screen_w - 48;
    card_w = card_w > 520 ? 520 : (card_w < 280 ? 280 : card_w);
    lv_obj_t *card = lv_obj_create(overlay);
    lv_obj_set_width(card, card_w);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(card, screen_h - 48, LV_PART_MAIN);
    lv_obj_center(card);
    lv_obj_set_event_bubble(card, false);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CONTENT_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    ui_popup_style_frame(card);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *head = lv_obj_create(card);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), UI_POPUP_CLOSE_SIZE);
    lv_obj_set_scrollable(head, false);
    lv_obj_t *title = lv_label_create(head);
    lv_label_set_text(title, ctx->title[0] != '\0' ? ctx->title : tr("bambu.details", "Printer status"));
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, card_w - 28 - UI_POPUP_CLOSE_SIZE - 12);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *close = ui_popup_close_button(head, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), detail_close_cb, ctx);
    lv_obj_align(close, LV_ALIGN_RIGHT_MID, 0, 0);

    static const struct {
        const char *key;
        const char *fallback;
    } captions[ROW_COUNT] = {
        [ROW_JOB] = {"bambu.job", "Job"},
        [ROW_STATUS] = {"bambu.status", "Status"},
        [ROW_STAGE] = {"bambu.stage", "Step"},
        [ROW_NOZZLE] = {"bambu.nozzle", "Nozzle"},
        [ROW_BED] = {"bambu.bed", "Bed"},
        [ROW_PROGRESS] = {"bambu.progress", "Progress"},
        [ROW_REMAINING] = {"bambu.remaining", "Remaining"},
    };
    for (int i = 0; i < ROW_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(row, 12, LV_PART_MAIN);
        lv_obj_t *caption = lv_label_create(row);
        lv_label_set_text(caption, tr(captions[i].key, captions[i].fallback));
        lv_obj_set_style_text_font(caption, ui_font_role(UI_TEXT_LABEL), LV_PART_MAIN);
        lv_obj_set_style_text_color(caption, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
        lv_obj_t *value = lv_label_create(row);
        ctx->detail_value[i] = value;
        lv_obj_set_style_text_font(value, ui_font_role(UI_TEXT_LABEL), LV_PART_MAIN);
        lv_obj_set_style_text_color(value, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(value, (card_w - 28) * 6 / 10);
    }
    if (ctx->camera_entity[0]) {
        lv_obj_t *button = lv_btn_create(card);
        lv_obj_set_size(button, LV_PCT(100), 56);
        lv_obj_add_event_cb(button, camera_clicked, LV_EVENT_CLICKED, ctx);
        lv_obj_t *label = lv_label_create(button);
        ui_text_set(label, UI_TEXT_LABEL, true);
        lv_label_set_text(label, tr("camera.live", "Live camera"));
        lv_obj_center(label);
    }
    detail_fill(ctx);
}

/* ---- layout and render --------------------------------------------------------------------------------------- */

static lv_coord_t text_width(const char *text, const lv_font_t *font)
{
    lv_point_t size = {0, 0};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

/* Readings have their own space outside the illustration, even when their text changes. */
static void bambu_render_temperatures(w_bambu_ctx_t *ctx)
{
    char nozzle[24], bed[24], text[2][48];
    format_temp(ctx->nozzle, ctx->nozzle_unit, nozzle, sizeof(nozzle));
    format_temp(ctx->bed, ctx->bed_unit, bed, sizeof(bed));
    snprintf(text[0], sizeof(text[0]), "%s %s", tr("bambu.nozzle", "Nozzle"), nozzle);
    snprintf(text[1], sizeof(text[1]), "%s %s", tr("bambu.bed", "Bed"), bed);
    lv_obj_t *labels[] = {ctx->nozzle_label, ctx->bed_label};
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
        const lv_font_t *font = ui_text_fit(text[i], ui_font_role(UI_TEXT_META),
                                           lv_obj_get_width(labels[i]), lv_obj_get_height(labels[i]));
        lv_obj_set_style_text_font(labels[i], font, LV_PART_MAIN);
        lv_obj_set_style_text_color(labels[i], lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_label_set_text(labels[i], text[i]);
    }
}

static void bambu_layout(w_bambu_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    const lv_coord_t inner_w = w - 2 * BAMBU_PAD;
    const bool roomy = w >= 420;
    const lv_font_t *name_font = ui_font_role(roomy ? UI_TEXT_LABEL : UI_TEXT_META);
    const lv_font_t *meta_font = ui_font_role(UI_TEXT_META);
    const lv_font_t *value_font = ui_font_role(UI_TEXT_VALUE);
    const lv_font_t *label_font = ui_font_role(UI_TEXT_LABEL);
    const lv_font_t *key_font = ui_font_role(UI_TEXT_KEY);
    ctx->stage_row = h >= BAMBU_STAGE_ROW_MIN_H || ctx->entity[ENT_STAGE][0] != '\0';

    /* Footer first: the three buttons keep a finger-sized height. */
    lv_coord_t btn_h = lv_font_get_line_height(label_font) + 18;
    if (btn_h < BAMBU_BTN_MIN_H) btn_h = BAMBU_BTN_MIN_H;
    const lv_coord_t btn_y = h - BAMBU_PAD - btn_h;
    const lv_coord_t btn_w = (inner_w - 2 * BAMBU_GAP) / 3;
    for (int i = 0; i < BTN_COUNT; i++) {
        lv_obj_set_size(ctx->btn[i], btn_w, btn_h);
        lv_obj_set_pos(ctx->btn[i], BAMBU_PAD + i * (btn_w + BAMBU_GAP), btn_y);
        lv_obj_set_width(ctx->btn_label[i], btn_w - 8);
        lv_obj_center(ctx->btn_label[i]);
    }
    const lv_coord_t bar_y = btn_y - BAMBU_GAP - BAMBU_BAR_H;
    lv_obj_set_size(ctx->bar, inner_w, BAMBU_BAR_H);
    lv_obj_set_pos(ctx->bar, BAMBU_PAD, bar_y);

    /* Header: the name on the left, the status on the right. */
    const lv_coord_t name_h = ui_widget_name_height(ctx->name_label, name_font);
    const lv_coord_t status_h = lv_font_get_line_height(value_font);
    const lv_coord_t head_h = name_h > status_h ? name_h : status_h;
    lv_coord_t status_w = inner_w;
    if (!ctx->hide_name) {
        status_w = inner_w * 62 / 100;
    }
    lv_obj_set_size(ctx->status_label, status_w, status_h);
    lv_obj_set_pos(ctx->status_label, ctx->hide_name ? BAMBU_PAD : w - BAMBU_PAD - status_w, BAMBU_PAD + (head_h - status_h) / 2);
    lv_obj_set_style_text_align(ctx->status_label, ctx->hide_name ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->name_label, name_font, LV_PART_MAIN);
    lv_obj_set_size(ctx->name_label, inner_w - status_w - BAMBU_GAP > 30 ? inner_w - status_w - BAMBU_GAP : 30, name_h);
    lv_obj_set_pos(ctx->name_label, BAMBU_PAD, BAMBU_PAD + (head_h - name_h) / 2);
    lv_coord_t top = BAMBU_PAD + head_h;
    if (ctx->stage_row) {
        const lv_coord_t stage_h = lv_font_get_line_height(meta_font);
        lv_obj_set_size(ctx->stage_label, inner_w, stage_h);
        lv_obj_set_pos(ctx->stage_label, BAMBU_PAD, top + 2);
        top += stage_h + 4;
    }
    lv_obj_set_hidden(ctx->stage_label, !ctx->stage_row);

    const lv_coord_t area_x = BAMBU_PAD;
    const lv_coord_t area_w = inner_w;
    const bool wide = w >= BAMBU_WIDE_MIN_W && (bar_y - BAMBU_GAP - top) * 3 < area_w * 2;
    lv_coord_t art_x = area_x;
    lv_coord_t art_y = top + 2;
    lv_coord_t art_w = area_w;
    lv_coord_t art_h = 0;
    const lv_coord_t reading_h = lv_font_get_line_height(key_font);
    const lv_coord_t temp_h = lv_font_get_line_height(meta_font);
    if (wide) {
        art_h = bar_y - BAMBU_GAP - art_y;
        art_w = area_w * 56 / 100;
        const lv_coord_t col_x = area_x + art_w + BAMBU_GAP;
        const lv_coord_t col_w = area_x + area_w - col_x;
        const lv_coord_t remaining_h = lv_font_get_line_height(value_font);
        const lv_coord_t total = reading_h + 4 + remaining_h + BAMBU_GAP + 2 * temp_h + 4;
        const lv_coord_t col_y = art_y + (art_h - total) / 2;
        lv_obj_set_size(ctx->progress_label, col_w, reading_h);
        lv_obj_set_pos(ctx->progress_label, col_x, col_y);
        lv_obj_set_size(ctx->remaining_label, col_w, lv_font_get_line_height(value_font));
        lv_obj_set_pos(ctx->remaining_label, col_x, col_y + reading_h + 4);
        lv_obj_set_style_text_align(ctx->progress_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(ctx->remaining_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_size(ctx->nozzle_label, col_w, temp_h);
        lv_obj_set_pos(ctx->nozzle_label, col_x, col_y + reading_h + 4 + remaining_h + BAMBU_GAP);
        lv_obj_set_size(ctx->bed_label, col_w, temp_h);
        lv_obj_set_pos(ctx->bed_label, col_x, col_y + total - temp_h);
        lv_obj_set_style_text_align(ctx->nozzle_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(ctx->bed_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    } else {
        const lv_coord_t row_y = bar_y - 6 - reading_h;
        const lv_coord_t temp_y = row_y - BAMBU_GAP - temp_h;
        art_h = temp_y - BAMBU_GAP - art_y;
        lv_obj_set_size(ctx->nozzle_label, inner_w / 2 - 4, temp_h);
        lv_obj_set_pos(ctx->nozzle_label, area_x, temp_y);
        lv_obj_set_size(ctx->bed_label, inner_w / 2 - 4, temp_h);
        lv_obj_set_pos(ctx->bed_label, area_x + inner_w - (inner_w / 2 - 4), temp_y);
        lv_obj_set_style_text_align(ctx->nozzle_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_align(ctx->bed_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_size(ctx->progress_label, inner_w / 2 - 4, reading_h);
        lv_obj_set_pos(ctx->progress_label, area_x, row_y);
        lv_obj_set_size(ctx->remaining_label, inner_w / 2 - 4, reading_h);
        lv_obj_set_pos(ctx->remaining_label, area_x + inner_w - (inner_w / 2 - 4), row_y);
        lv_obj_set_style_text_align(ctx->progress_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_align(ctx->remaining_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
    if (art_h < 40) art_h = 40;

    bambu_illustration_layout(ctx->art, art_x, art_y, art_w, art_h, NULL);
    bambu_render(ctx);
}

static void set_label(lv_obj_t *label, const char *text, ui_text_role_t role, lv_color_t color)
{
    ui_text_set(label, role, true);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_label_set_text(label, text);
}

/* One text size for all three buttons: the smallest that fits the longest of Pause, Resume, Status and Stop. */
static const lv_font_t *button_font(const w_bambu_ctx_t *ctx)
{
    const char *texts[] = {tr("bambu.pause", "Pause"), tr("bambu.resume", "Resume"), tr("bambu.status", "Status"),
                           tr("bambu.stop", "Stop")};
    const lv_coord_t btn_w = lv_obj_get_width(ctx->btn[0]);
    const lv_coord_t btn_h = lv_obj_get_height(ctx->btn[0]);
    const lv_font_t *best = NULL;
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
        const lv_font_t *font = ui_text_fit(texts[i], ui_font_role(UI_TEXT_LABEL), btn_w > 10 ? btn_w - 10 : btn_w, btn_h);
        if (best == NULL || ui_font_text_px_of(font) < ui_font_text_px_of(best)) {
            best = font;
        }
    }
    return best;
}

static void style_button(w_bambu_ctx_t *ctx, int index, const char *text, bool enabled, bool danger)
{
    lv_obj_t *btn = ctx->btn[index];
    lv_color_t bg = lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE);
    if (danger && enabled) {
        bg = lv_color_mix(lv_color_hex(APP_UI_COLOR_ERROR), bg, 70);
    }
    ui_style_button_auto(btn, bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER));
    lv_obj_set_style_opa(btn, enabled ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
    lv_label_set_text(ctx->btn_label[index], text);
    lv_obj_set_style_text_color(ctx->btn_label[index], ui_style_button_icon_color(bg), LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->btn_label[index], button_font(ctx), LV_PART_MAIN);
}

static void bambu_render(w_bambu_ctx_t *ctx)
{
    const bambu_actions_t actions = bambu_current_actions(ctx);
    const bool readings = job_readings_visible(ctx);
    const bool notice = ctx->notice[0] != '\0';

    /* Status line; on a card too small for a stage row a notice temporarily takes its place. */
    const char *status = notice && !ctx->stage_row ? ctx->notice : status_text(ctx);
    ui_text_set(ctx->status_label, UI_TEXT_VALUE, true);
    lv_obj_set_style_text_color(ctx->status_label, notice && !ctx->stage_row ? lv_color_hex(APP_UI_COLOR_ERROR)
                                                                              : status_color(ctx), LV_PART_MAIN);
    lv_label_set_text(ctx->status_label, status);
    lv_obj_set_style_text_font(ctx->status_label,
                               ui_text_fit(status, ui_font_role(UI_TEXT_VALUE), lv_obj_get_width(ctx->status_label),
                                           lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE))),
                               LV_PART_MAIN);

    if (!ctx->hide_name) {
        /* The name gets whatever the status text leaves; a short status ("Idle") no longer clips the name. */
        const lv_coord_t inner_w = lv_obj_get_width(ctx->card) - 2 * BAMBU_PAD;
        const lv_coord_t used = text_width(status, lv_obj_get_style_text_font(ctx->status_label, LV_PART_MAIN));
        lv_coord_t name_w = inner_w - used - BAMBU_GAP;
        lv_obj_set_width(ctx->name_label, name_w < 30 ? 30 : name_w);
    }

    char stage[64];
    format_stage(ctx, stage, sizeof(stage));
    if (ctx->pending >= 0) {
        snprintf(stage, sizeof(stage), "%s", tr("bambu.sending", "Sending..."));
    }
    if (notice && ctx->stage_row) {
        lv_label_set_text(ctx->stage_label, ctx->notice);
        lv_obj_set_style_text_color(ctx->stage_label, lv_color_hex(APP_UI_COLOR_ERROR), LV_PART_MAIN);
    } else {
        lv_label_set_text(ctx->stage_label, stage);
        lv_obj_set_style_text_color(ctx->stage_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    }

    char text[48];
    int percent = 0;
    const bool have_progress = readings && bambu_progress_percent(ctx->progress, &percent);
    lv_obj_set_hidden(ctx->bar, !readings);
    lv_bar_set_value(ctx->bar, have_progress ? percent : 0, LV_ANIM_OFF);
    if (readings) {
        format_progress(ctx, text, sizeof(text));
        set_label(ctx->progress_label, text, UI_TEXT_KEY, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY));
        if (bambu_state_job_last(ctx->state)) {
            set_label(ctx->remaining_label, tr("bambu.last_job", "Last job"), UI_TEXT_META,
                      lv_color_hex(APP_UI_COLOR_TEXT_MUTED));
        } else {
            format_remaining(ctx, text, sizeof(text));
            set_label(ctx->remaining_label, text, UI_TEXT_VALUE, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY));
        }
    } else {
        lv_label_set_text(ctx->progress_label, "");
        lv_label_set_text(ctx->remaining_label, "");
    }

    bambu_render_temperatures(ctx);

    float nozzle = 0.0f;
    float bed = 0.0f;
    const bool known_nozzle = bambu_parse_number(ctx->nozzle, &nozzle);
    const bool known_bed = bambu_parse_number(ctx->bed, &bed);
    const bool data = ctx->state != BAMBU_STATE_MISSING && ctx->state != BAMBU_STATE_UNAVAILABLE &&
                      ctx->state != BAMBU_STATE_OFFLINE;
    bambu_illustration_set_heat(ctx->art, known_nozzle && nozzle >= 50.0f, known_bed && bed >= 40.0f, data);

    style_button(ctx, BTN_PRIMARY, actions.primary_resume ? tr("bambu.resume", "Resume") : tr("bambu.pause", "Pause"),
                 actions.primary_enabled, false);
    style_button(ctx, BTN_STATUS, tr("bambu.status", "Status"), true, false);
    style_button(ctx, BTN_STOP, tr("bambu.stop", "Stop"), actions.stop_enabled, true);
    ui_style_card_set_active(ctx->card, ctx->state == BAMBU_STATE_RUNNING);
    detail_fill(ctx);
}

/* ---- lifetime ------------------------------------------------------------------------------------------------ */

static void bambu_tick_cb(lv_timer_t *timer)
{
    w_bambu_ctx_t *ctx = (w_bambu_ctx_t *)lv_timer_get_user_data(timer);
    bool changed = false;
    const bool online = bambu_online();
    const uint32_t now = lv_tick_get();
    if (online != ctx->online) {
        ctx->online = online;
        if (online) {
            bambu_refresh_from_model(ctx); /* rehydrate: nothing cached across the outage may enable a command */
        }
        changed = true;
    }
    if (ctx->pending >= 0) {
        if (!online || (int32_t)(now - ctx->pending_deadline) >= 0) {
            bambu_clear_pending(ctx);
            bambu_set_notice(ctx, tr("bambu.not_confirmed", "Command not confirmed"));
            changed = true;
        }
    }
    if (ctx->notice[0] != '\0' && ctx->pending < 0 && ctx->notice_deadline != 0U &&
        (int32_t)(now - ctx->notice_deadline) >= 0) {
        ctx->notice[0] = '\0';
        ctx->notice_deadline = 0;
        changed = true;
    }
    if (changed) {
        bambu_render(ctx);
    }
}

static void bambu_card_event_cb(lv_event_t *event)
{
    w_bambu_ctx_t *ctx = (w_bambu_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        if (ctx->timer != NULL) {
            lv_timer_delete(ctx->timer);
            ctx->timer = NULL;
        }
        bambu_close_popups(ctx); /* their callbacks only clear handles in ctx, which is still valid here */
        free(ctx);
    } else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED) {
        bambu_layout(ctx);
    }
}

static lv_obj_t *make_button(w_bambu_ctx_t *ctx, lv_obj_t *parent, int index, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_style_radius(btn, ui_style_radius(12), LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn, 4);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *label = lv_label_create(btn);
    ui_text_set(label, UI_TEXT_LABEL, true);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    ctx->btn[index] = btn;
    ctx->btn_label[index] = label;
    return btn;
}

esp_err_t w_bambu_printer_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_bambu_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(ctx->entity[ENT_STATE], APP_MAX_ENTITY_ID_LEN, "%s", def->entity_id);
    snprintf(ctx->entity[ENT_NOZZLE], APP_MAX_ENTITY_ID_LEN, "%s", def->secondary_entity_id);
    snprintf(ctx->entity[ENT_BED], APP_MAX_ENTITY_ID_LEN, "%s", def->tertiary_entity_id);
    snprintf(ctx->entity[ENT_PROGRESS], APP_MAX_ENTITY_ID_LEN, "%s", def->extra_entity_id[UI_BAMBU_EXTRA_PROGRESS]);
    snprintf(ctx->entity[ENT_REMAINING], APP_MAX_ENTITY_ID_LEN, "%s", def->extra_entity_id[UI_BAMBU_EXTRA_REMAINING]);
    snprintf(ctx->entity[ENT_PAUSE], APP_MAX_ENTITY_ID_LEN, "%s", def->extra_entity_id[UI_BAMBU_EXTRA_PAUSE]);
    snprintf(ctx->entity[ENT_RESUME], APP_MAX_ENTITY_ID_LEN, "%s", def->extra_entity_id[UI_BAMBU_EXTRA_RESUME]);
    snprintf(ctx->entity[ENT_STOP], APP_MAX_ENTITY_ID_LEN, "%s", def->extra_entity_id[UI_BAMBU_EXTRA_STOP]);
    snprintf(ctx->entity[ENT_STAGE], APP_MAX_ENTITY_ID_LEN, "%s", def->extra_entity_id[UI_BAMBU_EXTRA_STAGE]);
    snprintf(ctx->entity[ENT_JOB], APP_MAX_ENTITY_ID_LEN, "%s", def->extra_entity_id[UI_BAMBU_EXTRA_JOB]);
    snprintf(ctx->camera_entity, sizeof(ctx->camera_entity), "%s", def->extra_entity_id[UI_BAMBU_EXTRA_CAMERA]);
    snprintf(ctx->title, sizeof(ctx->title), "%s", def->title[0] != '\0' ? def->title : "");
    ctx->hide_name = def->hide_name;
    ctx->state = BAMBU_STATE_MISSING;
    ctx->pending = -1;

    lv_obj_t *card = lv_obj_create(parent);
    ctx->card = card;
    out_instance->obj = card;
    out_instance->ctx = ctx;
    out_instance->visible = true;
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);

    ctx->name_label = ui_widget_name_create(card, !def->hide_name);
    ui_style_title_fit(ctx->name_label, ctx->title);
    ui_text_set(ctx->name_label, UI_TEXT_META, false);
    lv_obj_set_style_text_color(ctx->name_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->name_label, LV_LABEL_LONG_DOT);

    ctx->status_label = lv_label_create(card);
    lv_label_set_long_mode(ctx->status_label, LV_LABEL_LONG_DOT);
    ctx->stage_label = lv_label_create(card);
    ui_text_set(ctx->stage_label, UI_TEXT_META, false);
    lv_label_set_long_mode(ctx->stage_label, LV_LABEL_LONG_DOT);

    ctx->art = bambu_illustration_create(card);
    ctx->bar = lv_bar_create(card);
    lv_bar_set_range(ctx->bar, 0, 100);
    ui_style_inset_track(ctx->bar, lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 26));
    lv_obj_set_style_bg_color(ctx->bar, lv_color_hex(APP_UI_COLOR_OK), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(ctx->bar, LV_GRAD_DIR_NONE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ctx->bar, ui_style_track_radius(), LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->bar, ui_style_track_radius(), LV_PART_INDICATOR);

    ctx->progress_label = lv_label_create(card);
    lv_label_set_long_mode(ctx->progress_label, LV_LABEL_LONG_DOT);
    ctx->remaining_label = lv_label_create(card);
    lv_label_set_long_mode(ctx->remaining_label, LV_LABEL_LONG_DOT);
    ctx->nozzle_label = lv_label_create(card);
    ctx->bed_label = lv_label_create(card);
    lv_label_set_long_mode(ctx->nozzle_label, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(ctx->bed_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(ctx->nozzle_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->bed_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    ui_text_record(ctx->nozzle_label, UI_TEXT_META, true);
    ui_text_record(ctx->bed_label, UI_TEXT_META, true);

    make_button(ctx, card, BTN_PRIMARY, bambu_primary_clicked_cb);
    make_button(ctx, card, BTN_STATUS, bambu_status_clicked);
    make_button(ctx, card, BTN_STOP, bambu_stop_clicked);

    lv_obj_add_event_cb(card, bambu_card_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, bambu_card_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    ctx->online = bambu_online();
    ctx->timer = lv_timer_create(bambu_tick_cb, BAMBU_TICK_MS, ctx);
    bambu_layout(ctx);
    return ESP_OK;
}

void w_bambu_printer_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_bambu_ctx_t *ctx = (w_bambu_ctx_t *)instance->ctx;
    const bool status_changed = bambu_store(ctx, state);
    ctx->online = bambu_online();
    if (ctx->pending >= 0 && status_changed && bambu_command_confirmed((bambu_command_t)ctx->pending, ctx->state)) {
        bambu_clear_pending(ctx); /* the expected status followed; unrelated updates leave pending alone */
        ctx->notice[0] = '\0';
        ctx->notice_deadline = 0;
    }
    bambu_render(ctx); /* labels and bar only: progress events never rebuild the layout */
}

void w_bambu_printer_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_bambu_ctx_t *ctx = (w_bambu_ctx_t *)instance->ctx;
    ctx->state = BAMBU_STATE_MISSING;
    ctx->raw_state[0] = '\0';
    bambu_clear_pending(ctx);
    bambu_render(ctx);
}

void w_bambu_printer_set_visible(ui_widget_instance_t *instance, bool visible)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_bambu_ctx_t *ctx = (w_bambu_ctx_t *)instance->ctx;
    if (!visible) {
        bambu_close_popups(ctx); /* a popup on the top layer must not outlive its page */
    }
}
