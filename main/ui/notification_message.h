/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#pragma once
#include "lvgl.h"

/* The ordinary label retains the exact plain-text rendering path. A span
 * group is created beside it only for emphasis and owned by the same parent.
 * text is already sanitized; both objects copy their text. */
void notification_message_set(lv_obj_t *label, lv_obj_t **spans, const char *text);
