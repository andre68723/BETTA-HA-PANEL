/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>

#include "app_events.h"

/* Show (or update in place) the notification overlay on lv_layer_top().
 * Must be called with the display (LVGL) lock held. Takes ownership of
 * `notification` and frees it before returning — for both the create and
 * the update-in-place path. Wakes the display. */
void ui_notification_popup_show(app_notification_t *notification);

/* Dismiss the overlay if present. Must be called with the display lock held. */
void ui_notification_popup_dismiss(void);

bool ui_notification_popup_is_active(void);
