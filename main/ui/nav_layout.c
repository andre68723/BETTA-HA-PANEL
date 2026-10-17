/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/nav_layout.h"

#include <string.h>

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int full_tab_w(int title_w, int screen_w)
{
    return clampi(NAV_TAB_OVERHEAD + title_w, NAV_HOME_MIN_W, screen_w / 3);
}

void nav_layout_compute(nav_layout_t *out, int screen_w, int page_count, int selected, const int *title_w)
{
    memset(out, 0, sizeof(*out));
    const int n = clampi(page_count, 0, NAV_MAX_TABS);
    out->count = (uint16_t)n;
    if (n == 0) return;
    const int avail = screen_w - 2 * NAV_MARGIN;

    if (n == 1) { /* a single page stays centered */
        const int w = full_tab_w(title_w != NULL ? title_w[0] : 0, screen_w);
        out->tab[0] = (nav_tab_t){.x = (int16_t)((screen_w - w) / 2), .w = (int16_t)w, .show_title = true};
        return;
    }

    /* full mode, equal tabs: when every tab of an equal split is wide enough for the first title, they are all the same */
    const int home_w = full_tab_w(title_w != NULL ? title_w[0] : 0, screen_w);
    const int equal_w = (avail - (n - 1) * NAV_GAP) / n;
    if (equal_w >= NAV_FULL_TAB_MIN_W && equal_w >= home_w) {
        const int used = n * equal_w + (n - 1) * NAV_GAP;
        const int x0 = NAV_MARGIN + (avail - used) / 2; /* the few pixels the division leaves are split on both sides */
        for (int i = 0; i < n; i++) {
            out->tab[i] = (nav_tab_t){.x = (int16_t)(x0 + i * (equal_w + NAV_GAP)), .w = (int16_t)equal_w, .show_title = true};
        }
        return;
    }

    /* full mode, first tab wider: it grows with its title, the others share the rest equally */
    const int row_w = avail - home_w - NAV_HOME_GAP;
    const int others = n - 1;
    const int each = (row_w - (others - 1) * NAV_GAP) / others;
    if (each >= NAV_FULL_TAB_MIN_W) {
        out->tab[0] = (nav_tab_t){.x = NAV_MARGIN, .w = (int16_t)home_w, .show_title = true};
        for (int i = 1; i < n; i++) {
            out->tab[i] = (nav_tab_t){.x = (int16_t)(NAV_MARGIN + home_w + NAV_HOME_GAP + (i - 1) * (each + NAV_GAP)),
                                      .w = (int16_t)each, .show_title = true};
        }
        return;
    }

    /* compact mode: the shown page keeps its title, the rest are icons */
    out->compact = true;
    const int sel = (selected >= 0 && selected < n) ? selected : -1;
    const int wide = sel >= 0 ? clampi(full_tab_w(title_w != NULL ? title_w[sel] : 0, screen_w), NAV_FULL_TAB_MIN_W, screen_w / 4) : 0;
    const int small_count = sel >= 0 ? n - 1 : n;
    int small_w = small_count > 0 ? (avail - wide - (n - 1) * NAV_GAP) / small_count : NAV_COMPACT_MAX_W;
    small_w = clampi(small_w, NAV_COMPACT_MIN_W, NAV_COMPACT_MAX_W);
    const int total = wide + small_count * small_w + (n - 1) * NAV_GAP;
    int x = (screen_w - (total < avail ? total : avail)) / 2;
    if (x < NAV_MARGIN) x = NAV_MARGIN;
    for (int i = 0; i < n; i++) {
        const bool shown = (i == sel);
        const int w = shown ? wide : small_w;
        out->tab[i] = (nav_tab_t){.x = (int16_t)x, .w = (int16_t)w, .show_title = shown};
        x += w + NAV_GAP;
    }
}

int nav_layout_max_pages(int screen_w)
{
    const int avail = screen_w - 2 * NAV_MARGIN;
    const int wide = screen_w / 4; /* the shown tab in compact mode is at most a quarter of the bar */
    int n = 1;
    while (n + 1 <= NAV_MAX_TABS && wide + n * NAV_COMPACT_MIN_W + n * NAV_GAP <= avail) n++;
    return n;
}

int nav_layout_full_pages(int screen_w, int title_w_typical)
{
    int n = 1;
    nav_layout_t layout;
    int widths[NAV_MAX_TABS];
    for (int i = 0; i < NAV_MAX_TABS; i++) widths[i] = title_w_typical;
    while (n + 1 <= NAV_MAX_TABS) {
        nav_layout_compute(&layout, screen_w, n + 1, 0, widths);
        if (layout.compact) break;
        n++;
    }
    return n;
}
