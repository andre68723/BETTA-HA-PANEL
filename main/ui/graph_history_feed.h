/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Gives graph widgets the Recorder history of their sensor. The History request (ha/ha_history.h) holds one waiting
 * request and a popup can take its place, so this keeps one request outstanding for all widgets, fetches only widgets
 * on a shown page, refreshes every 15 minutes and asks again when an answer is lost (util/history_feed_policy.h).
 * Everything runs on the LVGL task.
 */
#pragma once

#include "lvgl.h"

#include "ha/ha_history_reduce.h"

/* Called with the widget's newest history; the snapshot is only valid during the call. */
typedef void (*graph_history_feed_cb_t)(void *user, const ha_history_snapshot_t *snapshot);

/* Registers a widget. `obj` is its card: the history is only fetched while it is visible. Returns a handle, or NULL when
 * all slots are taken. */
void *graph_history_feed_register(const char *entity_id, lv_obj_t *obj, graph_history_feed_cb_t cb, void *user);
void graph_history_feed_unregister(void *handle);
