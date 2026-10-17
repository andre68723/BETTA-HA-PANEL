/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/theme/ui_style_registry.h"

#include <string.h>

typedef struct {
    const char *id;
    ui_style_kind_t kind;
} ui_style_entry_t;

/* Supported IDs, in the editor's display order. */
static const ui_style_entry_t s_styles[] = {
    {"flat", UI_STYLE_FLAT},
    {"glow", UI_STYLE_GLOW},
    {"glass", UI_STYLE_GLASS},
    {"matte", UI_STYLE_MATTE},
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

bool ui_style_lookup(const char *name, ui_style_kind_t *out)
{
    for (size_t i = 0; name != NULL && i < COUNT(s_styles); i++) {
        if (strcmp(name, s_styles[i].id) == 0) {
            if (out != NULL) {
                *out = s_styles[i].kind;
            }
            return true;
        }
    }
    if (out != NULL) {
        *out = UI_STYLE_FALLBACK;
    }
    return false;
}

ui_style_kind_t ui_style_from_name(const char *name)
{
    ui_style_kind_t kind;
    (void)ui_style_lookup(name, &kind);
    return kind;
}

const char *ui_style_name(ui_style_kind_t kind)
{
    for (size_t i = 0; i < COUNT(s_styles); i++) {
        if (s_styles[i].kind == kind) {
            return s_styles[i].id;
        }
    }
    return ui_style_name(UI_STYLE_FALLBACK);
}

size_t ui_style_count(void)
{
    return COUNT(s_styles);
}

bool ui_style_at(size_t index, ui_style_kind_t *out)
{
    if (index >= COUNT(s_styles)) {
        return false;
    }
    if (out != NULL) {
        *out = s_styles[index].kind;
    }
    return true;
}
