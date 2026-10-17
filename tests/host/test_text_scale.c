/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host tests of the text size scale: steps, roles, board profiles, user step, floors and metrics.
 */
#include <stdio.h>

#include "ui/text_scale.h"

static int g_checks, g_failed;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; long va = (long)(a), vb = (long)(b); if (va != vb) { g_failed++; printf("FAIL %s:%d %s == %s (%ld vs %ld)\n", __FILE__, __LINE__, #a, #b, va, vb); } } while (0)

static void test_steps(void)
{
    const int expect[TEXT_STEP_COUNT] = {16, 20, 24, 28, 32, 36, 42, 48, 56, 64};
    for (int i = 0; i < TEXT_STEP_COUNT; i++) CHECK_EQ(text_scale_step_px(i), expect[i]);
    CHECK_EQ(text_scale_step_px(-5), 16);
    CHECK_EQ(text_scale_step_px(99), 64);
    CHECK_EQ(text_scale_max_px(), 64);
    CHECK_EQ(text_scale_clamp_index(-1), 0);
    CHECK_EQ(text_scale_clamp_index(10), 9);
    CHECK_EQ(text_scale_clamp_user_step(-1), 0);
    CHECK_EQ(text_scale_clamp_user_step(3), 2);
    CHECK_EQ(text_scale_clamp_user_step(1), 1);
}

static void test_roles_and_profiles(void)
{
    CHECK_EQ(text_scale_role_index(UI_TEXT_UNASSIGNED), -1);
    CHECK_EQ(text_scale_role_index(UI_TEXT_META), 1);
    CHECK_EQ(text_scale_role_index(UI_TEXT_HERO), 6);
    CHECK_EQ(text_scale_board_offset(TEXT_BOARD_PANEL4), 0);
    CHECK_EQ(text_scale_board_offset(TEXT_BOARD_TAB5), 1);
    CHECK_EQ(text_scale_board_offset(TEXT_BOARD_WIDE10), 0);
    CHECK_EQ(text_scale_floor_px(TEXT_BOARD_PANEL4), 20);
    CHECK_EQ(text_scale_floor_px(TEXT_BOARD_TAB5), 20);
    CHECK_EQ(text_scale_floor_px(TEXT_BOARD_WIDE10), 16);
    CHECK_EQ(text_scale_px(TEXT_BOARD_PANEL4, UI_TEXT_UNASSIGNED, 0), 0);
    CHECK_EQ(text_scale_index(TEXT_BOARD_PANEL4, UI_TEXT_UNASSIGNED, 0), -1);
}

/* the tables of the plan (docs/appearance.md): role x user step per board */
static void test_exact_tables(void)
{
    static const int panel4[5][3] = {{20, 24, 28}, {24, 28, 32}, {28, 32, 36}, {36, 42, 48}, {42, 48, 56}};
    static const int tab5[5][3] = {{24, 28, 32}, {28, 32, 36}, {32, 36, 42}, {42, 48, 56}, {48, 56, 64}};
    static const int wide10[5][3] = {{20, 24, 28}, {24, 28, 32}, {28, 32, 36}, {36, 42, 48}, {42, 48, 56}}; /* the Panel4's sizes */
    for (int r = 0; r < 5; r++) {
        const ui_text_role_t role = (ui_text_role_t)(UI_TEXT_META + r);
        for (int u = 0; u < 3; u++) {
            CHECK_EQ(text_scale_px(TEXT_BOARD_PANEL4, role, u), panel4[r][u]);
            CHECK_EQ(text_scale_px(TEXT_BOARD_TAB5, role, u), tab5[r][u]);
            CHECK_EQ(text_scale_px(TEXT_BOARD_WIDE10, role, u), wide10[r][u]);
        }
    }
}

/* every role keeps distinct sizes on every board and step, grows monotonically with the step and never drops below the floor */
static void test_invariants(void)
{
    for (int b = 0; b < TEXT_BOARD_COUNT; b++) {
        const text_board_t board = (text_board_t)b;
        for (int u = 0; u <= TEXT_USER_STEP_MAX; u++) {
            int prev = 0;
            for (int r = UI_TEXT_META; r <= UI_TEXT_HERO; r++) {
                const int px = text_scale_px(board, (ui_text_role_t)r, u);
                CHECK(px >= text_scale_floor_px(board));
                CHECK(px <= text_scale_max_px());
                CHECK(px > prev); /* the hierarchy is preserved */
                prev = px;
                if (u > 0) CHECK(px > text_scale_px(board, (ui_text_role_t)r, u - 1)); /* +1 never disappears */
            }
        }
    }
    /* a role below the board floor is raised to it, and the user step still applies after the floor */
    CHECK_EQ(text_scale_px(TEXT_BOARD_WIDE10, UI_TEXT_META, 0), 20);
    CHECK_EQ(text_scale_px(TEXT_BOARD_WIDE10, UI_TEXT_META, 1), 24);
    CHECK_EQ(text_scale_px(TEXT_BOARD_PANEL4, UI_TEXT_META, 0), 20);
    /* the user step is clamped */
    CHECK_EQ(text_scale_px(TEXT_BOARD_PANEL4, UI_TEXT_LABEL, 7), text_scale_px(TEXT_BOARD_PANEL4, UI_TEXT_LABEL, 2));
    CHECK_EQ(text_scale_px(TEXT_BOARD_PANEL4, UI_TEXT_LABEL, -3), text_scale_px(TEXT_BOARD_PANEL4, UI_TEXT_LABEL, 0));
    /* the top of the table is reached on Tab5 hero +2 and no role saturates before user +2 */
    CHECK_EQ(text_scale_px(TEXT_BOARD_TAB5, UI_TEXT_HERO, 2), 64);
    for (int b = 0; b < TEXT_BOARD_COUNT; b++)
        for (int r = UI_TEXT_META; r <= UI_TEXT_HERO; r++)
            CHECK(text_scale_index((text_board_t)b, (ui_text_role_t)r, 2) > text_scale_index((text_board_t)b, (ui_text_role_t)r, 0));
}

static void test_text_metrics(void)
{
    static const int table[][3] = {{16, 21, 5}, {20, 25, 6}, {24, 32, 7}, {28, 35, 7}, {32, 39, 8},
                                   {36, 44, 9}, {42, 53, 11}, {48, 60, 13}, {56, 69, 15}, {64, 80, 18}};
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        int lh = 0, bl = 0;
        text_scale_text_metrics(table[i][0], &lh, &bl);
        CHECK_EQ(lh, table[i][1]);
        CHECK_EQ(bl, table[i][2]);
    }
    /* between the steps the metrics lie between their neighbours; outside they extrapolate and stay positive */
    int lh = 0, bl = 0, lh_a = 0, lh_b = 0;
    for (int px = 12; px <= 80; px++) {
        text_scale_text_metrics(px, &lh, &bl);
        CHECK(lh >= px && lh <= px * 2);
        CHECK(bl >= 0 && bl < lh);
        text_scale_text_metrics(px + 1, &lh_a, NULL);
        CHECK(lh_a >= lh); /* monotonic */
        (void)lh_b;
    }
    text_scale_text_metrics(38, &lh, &bl);
    CHECK(lh > 44 && lh < 53);
    text_scale_text_metrics(30, NULL, NULL); /* NULL outputs are allowed */
}

static void test_icon_metrics(void)
{
    icon_metrics_t m = text_scale_icon_metrics(ICON_SYMBOL_SMALL);
    CHECK_EQ(m.px, 18); CHECK_EQ(m.line_height, 18); CHECK_EQ(m.base_line, 2);
    m = text_scale_icon_metrics(ICON_TILE_72);
    CHECK_EQ(m.px, 72); CHECK_EQ(m.line_height, 73); CHECK_EQ(m.base_line, 10);
    m = text_scale_icon_metrics(ICON_ARROWS_20);
    CHECK_EQ(m.line_height, 15); CHECK_EQ(m.base_line, 0);
    m = text_scale_icon_metrics(ICON_ENERGY_42);
    CHECK_EQ(m.line_height, 36);
    m = text_scale_icon_metrics((ui_icon_font_t)99); /* out of range falls back to a valid entry */
    CHECK_EQ(m.px, 18);
    for (int i = 0; i < ICON_FONT_COUNT; i++) {
        m = text_scale_icon_metrics((ui_icon_font_t)i);
        CHECK(m.px > 0 && m.line_height > 0 && m.base_line >= 0);
    }
}

static void test_emoji_metrics(void)
{
    icon_metrics_t m = text_scale_emoji_metrics(16);
    CHECK_EQ(m.px, 16); CHECK_EQ(m.line_height, 17); CHECK_EQ(m.base_line, 3);
    m = text_scale_emoji_metrics(20);
    CHECK_EQ(m.px, 20); CHECK_EQ(m.line_height, 22); CHECK_EQ(m.base_line, 4);
    CHECK_EQ(text_scale_emoji_metrics(38).px, 36);
    CHECK_EQ(text_scale_emoji_metrics(64).px, 64);
    CHECK_EQ(text_scale_emoji_metrics(100).px, 64);
    CHECK_EQ(text_scale_emoji_metrics(5).px, 16);
}

static void test_capped_growth(void)
{
    /* cap 0: the reference size of the role on every board that grows; boards below the reference are not raised */
    CHECK_EQ(text_scale_px_capped(TEXT_BOARD_TAB5, UI_TEXT_META, 0, 0), 20);
    CHECK_EQ(text_scale_px_capped(TEXT_BOARD_TAB5, UI_TEXT_META, 2, 0), 20);
    CHECK_EQ(text_scale_px_capped(TEXT_BOARD_PANEL4, UI_TEXT_VALUE, 2, 0), 28);
    CHECK_EQ(text_scale_px_capped(TEXT_BOARD_WIDE10, UI_TEXT_META, 0, 0), 20);
    CHECK_EQ(text_scale_px_capped(TEXT_BOARD_WIDE10, UI_TEXT_LABEL, 2, 0), 24);
    /* cap 1 allows one step: the Tab5 offset, or one user step on the Panel4 */
    CHECK_EQ(text_scale_px_capped(TEXT_BOARD_TAB5, UI_TEXT_LABEL, 0, 1), 28);
    CHECK_EQ(text_scale_px_capped(TEXT_BOARD_TAB5, UI_TEXT_LABEL, 2, 1), 28);
    CHECK_EQ(text_scale_px_capped(TEXT_BOARD_PANEL4, UI_TEXT_LABEL, 2, 1), 28);
    /* an unlimited cap equals the uncapped scale for every board, role and step */
    for (int b = 0; b < TEXT_BOARD_COUNT; b++)
        for (int r = UI_TEXT_META; r <= UI_TEXT_HERO; r++)
            for (int u = 0; u <= 2; u++) {
                CHECK_EQ(text_scale_px_capped((text_board_t)b, (ui_text_role_t)r, u, TEXT_TILE_CAP_NONE),
                         text_scale_px((text_board_t)b, (ui_text_role_t)r, u));
                CHECK(text_scale_px_capped((text_board_t)b, (ui_text_role_t)r, u, 0) <= text_scale_px((text_board_t)b, (ui_text_role_t)r, u));
            }
    CHECK_EQ(text_scale_px_capped(TEXT_BOARD_PANEL4, UI_TEXT_UNASSIGNED, 0, 0), 0);
    /* cap -1: one role step below the reference (floored at the board minimum) at any user step */
    for (int u = 0; u <= 2; u++) {
        CHECK_EQ(text_scale_px_capped(TEXT_BOARD_WIDE10, UI_TEXT_LABEL, u, -1), 20);
        CHECK_EQ(text_scale_px_capped(TEXT_BOARD_WIDE10, UI_TEXT_META, u, -1), 16);
        CHECK_EQ(text_scale_px_capped(TEXT_BOARD_PANEL4, UI_TEXT_META, u, -1), 20);
        CHECK_EQ(text_scale_px_capped(TEXT_BOARD_TAB5, UI_TEXT_VALUE, u, -1), 24);
    }
    /* the energy page: one step on a wide screen, none on a narrow one */
    CHECK_EQ(text_scale_tile_cap(TEXT_BOARD_WIDE10, "energy_page", 1280, 800), 1);
    CHECK_EQ(text_scale_tile_cap(TEXT_BOARD_TAB5, "energy_page", 1280, 720), 1);
    CHECK_EQ(text_scale_tile_cap(TEXT_BOARD_PANEL4, "energy_page", 720, 720), 0);
    /* unknown types and unset sizes are not capped */
    for (int b = 0; b < TEXT_BOARD_COUNT; b++) {
        CHECK_EQ(text_scale_tile_cap((text_board_t)b, "no_such_widget", 100, 100), TEXT_TILE_CAP_NONE);
        CHECK_EQ(text_scale_tile_cap((text_board_t)b, NULL, 100, 100), TEXT_TILE_CAP_NONE);
        CHECK_EQ(text_scale_tile_cap((text_board_t)b, "sensor", 0, 0), TEXT_TILE_CAP_NONE);
    }
    /* growth never decreases with the tile size */
    for (int b = 0; b < TEXT_BOARD_COUNT; b++)
        for (int w = 60; w <= 1280; w += 20)
            for (int h = 60; h <= 720; h += 20) {
                CHECK(text_scale_tile_cap((text_board_t)b, "weather_3day", w + 20, h) >= text_scale_tile_cap((text_board_t)b, "weather_3day", w, h));
                CHECK(text_scale_tile_cap((text_board_t)b, "weather_3day", w, h + 20) >= text_scale_tile_cap((text_board_t)b, "weather_3day", w, h));
            }
}

int main(void)
{
    test_steps();
    test_roles_and_profiles();
    test_exact_tables();
    test_invariants();
    test_text_metrics();
    test_icon_metrics();
    test_emoji_metrics();
    test_capped_growth();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    if (g_failed == 0) printf("PASS: text scale steps, roles, board profiles, user step and metrics\n");
    return g_failed == 0 ? 0 : 1;
}
