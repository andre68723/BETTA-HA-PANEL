/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "typography_dump.h"
#include "src/widgets/label/lv_label_private.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui/theme/ui_typography.h"

typedef struct text_metadata {
    lv_obj_t *label;
    ui_text_role_t role;
    bool essential, compact;
    struct text_metadata *next;
} text_metadata_t;
static text_metadata_t *s_metadata;

static void metadata_deleted(lv_event_t *event)
{
    text_metadata_t *entry = lv_event_get_user_data(event);
    text_metadata_t **link = &s_metadata;
    while (*link && *link != entry) link = &(*link)->next;
    if (*link) *link = entry->next;
    free(entry);
}
static text_metadata_t *metadata(lv_obj_t *label, bool create)
{
    for (text_metadata_t *entry = s_metadata; entry; entry = entry->next)
        if (entry->label == label) return entry;
    if (!create) return NULL;
    text_metadata_t *entry = calloc(1, sizeof(*entry));
    if (!entry) abort();
    entry->label = label; entry->next = s_metadata; s_metadata = entry;
    lv_obj_add_event_cb(label, metadata_deleted, LV_EVENT_DELETE, entry);
    return entry;
}
void ui_text_record(lv_obj_t *label, ui_text_role_t role, bool essential)
{ text_metadata_t *entry = metadata(label, true); entry->role = role; entry->essential = essential; }
ui_text_role_t ui_text_role(lv_obj_t *label)
{ text_metadata_t *entry = metadata(label, false); return entry ? entry->role : UI_TEXT_UNASSIGNED; }
bool ui_text_is_essential(lv_obj_t *label)
{ text_metadata_t *entry = metadata(label, false); return entry && entry->essential; }
void ui_text_compact(lv_obj_t *label, bool compact) { metadata(label, true)->compact = compact; }
bool ui_text_is_compact(lv_obj_t *label)
{ text_metadata_t *entry = metadata(label, false); return entry && entry->compact; }

void simulator_dump_typography(cJSON *record, lv_obj_t *label)
{
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    const char *roles[] = {"unassigned", "meta", "label", "value", "key", "hero"};
    const ui_text_role_t role = ui_text_role(label);
    cJSON_AddStringToObject(record, "text_role", role <= UI_TEXT_HERO ? roles[role] : "unassigned");
    cJSON_AddBoolToObject(record, "essential", ui_text_is_essential(label));
    cJSON_AddBoolToObject(record, "compact_fallback", ui_text_is_compact(label));
    /* A symbol wrapper can name a text font as its fallback. Measure that text font,
     * while keeping the wrapper's identity distinct from a real text font. */
    int nominal = ui_font_text_px_of(font);
    const lv_font_t *text_font = nominal > 0 ? font : NULL;
    if (text_font == NULL && role != UI_TEXT_UNASSIGNED && font->fallback != NULL) {
        nominal = ui_font_text_px_of(font->fallback);
        text_font = nominal > 0 ? font->fallback : NULL;
    }
    if (text_font == NULL) { cJSON_AddStringToObject(record, "font", "symbol_or_unregistered"); return; }
    char identity[40];
    snprintf(identity, sizeof(identity), "poppins_medium_%d%s", nominal, text_font == font ? "" : "_fallback");
    cJSON_AddStringToObject(record, "font", identity);
    cJSON_AddNumberToObject(record, "font_px", nominal);
    cJSON_AddNumberToObject(record, "line_height", text_font->line_height);
    const uint32_t chars[] = {'H', 'x', '0'};
    const char *keys[] = {"H_ink_px", "x_ink_px", "digit_ink_px"};
    for (unsigned i = 0; i < 3; i++) {
        lv_font_glyph_dsc_t glyph = {0};
        text_font->get_glyph_dsc(text_font, &glyph, chars[i], 0);
        cJSON_AddNumberToObject(record, keys[i], glyph.box_h);
        if (i == 0) {
            double mm = glyph.box_h * 71.93 / 720.0;
            cJSON_AddNumberToObject(record, "H_mm_panel4", mm);
            cJSON_AddNumberToObject(record, "H_arcmin_30cm", 2 * atan(mm / 600) * 180 / 3.141592653589793 * 60);
            cJSON_AddNumberToObject(record, "H_arcmin_40cm", 2 * atan(mm / 800) * 180 / 3.141592653589793 * 60);
        }
    }
    lv_point_t natural;
    lv_text_get_size(&natural, lv_label_get_text(label), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    /* LVGL LONG_DOT mutates its text buffer: natural size alone misses ellipses.
     * Read its private marker only in the simulator; never mutate LVGL state. */
    bool clipped = ((const lv_label_t *)label)->dot_begin != UINT32_MAX ||
                   (lv_label_get_long_mode(label) != LV_LABEL_LONG_WRAP &&
                   (natural.x > lv_obj_get_content_width(label) || natural.y > lv_obj_get_content_height(label)));
    cJSON_AddBoolToObject(record, "text_clipped", clipped);
}
