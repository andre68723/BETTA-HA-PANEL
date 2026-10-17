/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Where the tabs of the bottom navigation bar go (ui_pages.c draws them). Two modes, chosen by the width alone:
 *   full     every tab shows icon and title, while each one is at least NAV_FULL_TAB_MIN_W wide. They are all the same width
 *            when the first title fits in that width; otherwise the first tab grows to fit its title and the rest share
 *            the remaining row equally;
 *   compact  from the page that no longer fits like that: the shown page keeps icon and title, every other tab shows
 *            its icon only (a number when it has none) and is NAV_COMPACT_MIN_W..NAV_COMPACT_MAX_W wide; the group is centered.
 * Pure (host tested). The page limit follows from the compact mode: nav_layout_max_pages().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NAV_MAX_TABS 16
#define NAV_MARGIN 14
#define NAV_GAP 8
#define NAV_HOME_GAP 12
#define NAV_FULL_TAB_MIN_W 112
#define NAV_HOME_MIN_W 72
#define NAV_COMPACT_MIN_W 64
#define NAV_COMPACT_MAX_W 96
/* What a full tab spends besides its title: 12 left padding, the 26 px icon, an 8 px gap and 12 right padding. The title
 * label of the first tab is exactly tab width minus this (ui_pages.c), so a tab narrower than title + this ends in dots. */
#define NAV_TAB_OVERHEAD 58

typedef struct {
    int16_t x;
    int16_t w;
    bool show_title; /* the title is drawn (full mode, or the shown page in compact mode) */
} nav_tab_t;

typedef struct {
    bool compact;
    uint16_t count; /* tabs, one per page */
    nav_tab_t tab[NAV_MAX_TABS];
} nav_layout_t;

/* `title_w[i]` is the pixel width of page i's title in the navigation font; `selected` the shown page (any value outside
 * 0..page_count-1 means none). page_count 0..NAV_MAX_TABS (more is clamped). */
void nav_layout_compute(nav_layout_t *out, int screen_w, int page_count, int selected, const int *title_w);

/* The most pages whose tabs still fit at NAV_COMPACT_MIN_W with the widest shown tab (a quarter of the screen): 8 on 720 px. */
int nav_layout_max_pages(int screen_w);

/* The most pages that keep full tabs for titles of `title_w_typical` pixels (for the editor's hint: icons matter beyond this). */
int nav_layout_full_pages(int screen_w, int title_w_typical);
