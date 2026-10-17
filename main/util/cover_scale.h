/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Area-average (box filter) downscaler for cover art. Source rows are fed one at a time, so a streamed PNG
 * decoder can use it without holding the whole image; the output is RGB565 plus an optional alpha row.
 *
 * Every source pixel belongs to exactly one destination pixel: destination column dx covers the source columns
 * with floor(sx * out_w / src_w) == dx, and the same rule applies to rows. A destination pixel is the plain mean
 * of its source pixels (colour weighted by alpha, alpha averaged), so thin lines and text keep their weight
 * instead of being skipped the way nearest-neighbour sampling does. Only shrinking (or 1:1) is supported.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Upper bound for source pixels per destination pixel (keeps the 32-bit sums from overflowing). */
#define COVER_BOX_MAX_SAMPLES 60000U

typedef struct {
    uint32_t src_w;
    uint32_t src_h;
    uint32_t out_w;
    uint32_t out_h;
    uint32_t next_sy;   /* next source row to be added */
    uint32_t rows;      /* source rows added to the current destination row */
    uint32_t ready_dy;  /* destination row finished by the last add_*_row() that returned true */
    uint32_t *sum;      /* out_w * 4: alpha-weighted r, g, b and alpha */
    uint16_t *colcnt;   /* source columns per destination column */
} cover_box_t;

size_t cover_box_sum_bytes(uint32_t out_w);
size_t cover_box_colcnt_bytes(uint32_t out_w);

/* `sum` and `colcnt` must hold cover_box_sum_bytes()/cover_box_colcnt_bytes() bytes. False for upscaling,
 * zero sizes or a ratio above COVER_BOX_MAX_SAMPLES. */
bool cover_box_init(cover_box_t *box, uint32_t src_w, uint32_t src_h, uint32_t out_w, uint32_t out_h,
                    uint32_t *sum, uint16_t *colcnt);

/* Add the next source row. They return true when the row completed a destination row: call
 * cover_box_take_rgb565_row() before adding the next one. Rows beyond src_h are ignored (false). */
bool cover_box_add_rgba_row(cover_box_t *box, const uint8_t *rgba);   /* src_w * 4 bytes: r, g, b, a */
bool cover_box_add_rgb24_row(cover_box_t *box, const uint8_t *rgb);   /* src_w * 3 bytes, opaque */
bool cover_box_add_rgb565_row(cover_box_t *box, const uint16_t *src); /* src_w pixels, opaque */

/* Index of the destination row that is ready to be taken. */
uint32_t cover_box_ready_row(const cover_box_t *box);

/* Write the finished destination row (out_w pixels, RGB565 as a native uint16_t; alpha may be NULL) and reset the
 * accumulators for the next destination row. */
void cover_box_take_rgb565_row(cover_box_t *box, uint16_t *dst, uint8_t *alpha);
