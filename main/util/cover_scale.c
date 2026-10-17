/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/cover_scale.h"

#include <string.h>

size_t cover_box_sum_bytes(uint32_t out_w)
{
    return (size_t)out_w * 4U * sizeof(uint32_t);
}

size_t cover_box_colcnt_bytes(uint32_t out_w)
{
    return (size_t)out_w * sizeof(uint16_t);
}

bool cover_box_init(cover_box_t *box, uint32_t src_w, uint32_t src_h, uint32_t out_w, uint32_t out_h,
                    uint32_t *sum, uint16_t *colcnt)
{
    if (box == NULL || sum == NULL || colcnt == NULL || src_w == 0U || src_h == 0U || out_w == 0U || out_h == 0U ||
        out_w > src_w || out_h > src_h) {
        return false;
    }
    const uint64_t per_col = ((uint64_t)src_w + out_w - 1U) / out_w;
    const uint64_t per_row = ((uint64_t)src_h + out_h - 1U) / out_h;
    if (per_col * per_row > COVER_BOX_MAX_SAMPLES || per_col > UINT16_MAX) {
        return false;
    }
    memset(box, 0, sizeof(*box));
    box->src_w = src_w;
    box->src_h = src_h;
    box->out_w = out_w;
    box->out_h = out_h;
    box->sum = sum;
    box->colcnt = colcnt;
    memset(sum, 0, cover_box_sum_bytes(out_w));
    memset(colcnt, 0, cover_box_colcnt_bytes(out_w));

    /* dx(sx) = floor(sx * out_w / src_w), stepped without a division per pixel. */
    uint32_t dx = 0;
    uint32_t rem = 0;
    for (uint32_t sx = 0; sx < src_w; sx++) {
        if (sx > 0U) {
            rem += out_w;
            if (rem >= src_w) {
                rem -= src_w;
                dx++;
            }
        }
        colcnt[dx]++;
    }
    return true;
}

/* True when source row `sy` is the last one of its destination row. */
static bool box_row_completes(const cover_box_t *box, uint32_t sy, uint32_t *dy_out)
{
    const uint32_t dy = (uint32_t)(((uint64_t)sy * box->out_h) / box->src_h);
    *dy_out = dy;
    if (sy + 1U >= box->src_h) {
        return true;
    }
    return (uint32_t)(((uint64_t)(sy + 1U) * box->out_h) / box->src_h) != dy;
}

static bool box_finish_add(cover_box_t *box)
{
    uint32_t dy = 0;
    const bool done = box_row_completes(box, box->next_sy, &dy);
    box->next_sy++;
    box->rows++;
    if (done) {
        box->ready_dy = dy;
    }
    return done;
}

/* Walks the source columns; the body (the variadic argument) must set r, g, b and a from `sx`. */
#define BOX_COLUMNS(box, ...)                                                         \
    do {                                                                              \
        uint32_t rem_ = 0;                                                            \
        uint32_t *acc_ = (box)->sum;                                                  \
        for (uint32_t sx = 0; sx < (box)->src_w; sx++) {                              \
            if (sx > 0U) {                                                            \
                rem_ += (box)->out_w;                                                 \
                if (rem_ >= (box)->src_w) {                                           \
                    rem_ -= (box)->src_w;                                             \
                    acc_ += 4;                                                        \
                }                                                                     \
            }                                                                         \
            uint32_t r, g, b, a;                                                      \
            __VA_ARGS__                                                               \
            acc_[0] += r * a;                                                         \
            acc_[1] += g * a;                                                         \
            acc_[2] += b * a;                                                         \
            acc_[3] += a;                                                             \
        }                                                                             \
    } while (0)

bool cover_box_add_rgba_row(cover_box_t *box, const uint8_t *rgba)
{
    if (box == NULL || rgba == NULL || box->next_sy >= box->src_h) {
        return false;
    }
    BOX_COLUMNS(box, {
        const uint8_t *p = rgba + (size_t)sx * 4U;
        r = p[0];
        g = p[1];
        b = p[2];
        a = p[3];
    });
    return box_finish_add(box);
}

bool cover_box_add_rgb24_row(cover_box_t *box, const uint8_t *rgb)
{
    if (box == NULL || rgb == NULL || box->next_sy >= box->src_h) {
        return false;
    }
    BOX_COLUMNS(box, {
        const uint8_t *p = rgb + (size_t)sx * 3U;
        r = p[0];
        g = p[1];
        b = p[2];
        a = 255U;
    });
    return box_finish_add(box);
}

bool cover_box_add_rgb565_row(cover_box_t *box, const uint16_t *src)
{
    if (box == NULL || src == NULL || box->next_sy >= box->src_h) {
        return false;
    }
    BOX_COLUMNS(box, {
        const uint16_t v = src[sx];
        const uint32_t r5 = (v >> 11) & 0x1FU;
        const uint32_t g6 = (v >> 5) & 0x3FU;
        const uint32_t b5 = v & 0x1FU;
        r = (r5 << 3) | (r5 >> 2);
        g = (g6 << 2) | (g6 >> 4);
        b = (b5 << 3) | (b5 >> 2);
        a = 255U;
    });
    return box_finish_add(box);
}

uint32_t cover_box_ready_row(const cover_box_t *box)
{
    return box != NULL ? box->ready_dy : 0U;
}

void cover_box_take_rgb565_row(cover_box_t *box, uint16_t *dst, uint8_t *alpha)
{
    if (box == NULL || dst == NULL || box->rows == 0U) {
        return;
    }
    for (uint32_t dx = 0; dx < box->out_w; dx++) {
        uint32_t *acc = box->sum + (size_t)dx * 4U;
        const uint32_t n = box->rows * (uint32_t)box->colcnt[dx];
        const uint32_t a_sum = acc[3];
        uint32_t r = 0, g = 0, b = 0, a = 0;
        if (a_sum > 0U && n > 0U) {
            r = (acc[0] + a_sum / 2U) / a_sum;
            g = (acc[1] + a_sum / 2U) / a_sum;
            b = (acc[2] + a_sum / 2U) / a_sum;
            a = (a_sum + n / 2U) / n;
            if (r > 255U) r = 255U;
            if (g > 255U) g = 255U;
            if (b > 255U) b = 255U;
            if (a > 255U) a = 255U;
        }
        dst[dx] = (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
        if (alpha != NULL) {
            alpha[dx] = (uint8_t)a;
        }
    }
    memset(box->sum, 0, cover_box_sum_bytes(box->out_w));
    box->rows = 0;
}
