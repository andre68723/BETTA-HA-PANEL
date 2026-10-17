/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Font manager failure acceptance (ui/text_font.c): a creation failure and the instance budget never give a caller a NULL font, keep the
 * instances bounded and keep every font that was created before. Deliberate synthetic requests: ordinary role and step cycling cannot
 * reach the budget (46 retained keys at most), so every integer size and every icon font is requested here.
 */
#include <stdio.h>
#include <stdlib.h>

#include "lvgl.h"
#include "ui/text_font.h"

static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Font budget regression line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

int sim_font_budget_regression(void)
{
    const lv_font_t *kept = ui_font_text_px(20);
    CHECK(kept != NULL && ui_font_text_px_of(kept) == 20);
    const lv_font_t *strong = ui_font_emphasis(kept);
    CHECK(strong != kept && strong == ui_font_emphasis(kept));
    CHECK(strong->line_height == kept->line_height && strong->base_line == kept->base_line);
    const ui_fonts_stats_t before = ui_fonts_stats();

    /* A creation failure below the budget: a nearest existing size or LVGL's default, never NULL; the failure is counted. */
    ui_fonts_test_fail_creates(1000);
    for (int px = 9; px < 64; px += 6) {
        const lv_font_t *font = ui_font_text_px(px);
        CHECK(font != NULL);
        CHECK(font->line_height > 0);
    }
    for (int kind = 0; kind < ICON_FONT_COUNT; kind++) CHECK(ui_font_icon((ui_icon_font_t)kind) != NULL);
    CHECK(ui_font_probe(33) != NULL);
    const lv_font_t *failure_base = ui_font_text_px(25);
    CHECK(ui_font_emphasis(failure_base) == failure_base);
    ui_fonts_test_fail_creates(0);
    const ui_fonts_stats_t failed = ui_fonts_stats();
    CHECK(failed.create_failures > before.create_failures);
    CHECK(ui_font_text_px(20) == kept); /* a key created before the failure is untouched */

    /* The budget: every integer size the manager accepts, then every icon font. */
    for (int px = 8; px <= 64; px++) {
        const lv_font_t *font = ui_font_text_px(px);
        CHECK(font != NULL);
        CHECK(font->line_height > 0);
        CHECK(ui_fonts_stats().instances <= UI_FONT_MAX_INSTANCES);
    }
    for (int kind = 0; kind < ICON_FONT_COUNT; kind++) {
        CHECK(ui_font_icon((ui_icon_font_t)kind) != NULL);
        CHECK(ui_fonts_stats().instances <= UI_FONT_MAX_INSTANCES);
    }
    const ui_fonts_stats_t full = ui_fonts_stats();
    CHECK(full.instances <= UI_FONT_MAX_INSTANCES);
    CHECK(full.budget_hits > 0); /* 57 sizes + 11 icon fonts + the emoji sizes do not fit */

    /* A substitute is never larger than the request while a smaller text font exists, and a created key keeps its instance. */
    int substituted = 0;
    for (int px = 16; px <= 64; px++) {
        const lv_font_t *font = ui_font_text_px(px);
        const int got = ui_font_text_px_of(font);
        CHECK(got > 0);
        if (got != px) {
            substituted++;
            CHECK(got < px);
        }
    }
    CHECK(substituted > 0);
    CHECK(ui_font_text_px(20) == kept);
    CHECK(ui_font_emphasis(kept) == strong);
    CHECK(ui_font_emphasis(ui_font_text_px(64)) != NULL);
    CHECK(ui_font_at_floor(ui_font_role(UI_TEXT_META)) || ui_fonts_role_px(UI_TEXT_META) > 0);

    printf("Font budget regression: %u checks passed (%d instances, %d budget hits, %d create failures, %d substituted sizes)\n", checks,
           full.instances, full.budget_hits, ui_fonts_stats().create_failures, substituted);
    return 0;
}
