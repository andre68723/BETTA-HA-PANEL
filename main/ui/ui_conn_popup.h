/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The connectivity popups of the top bar: the Wi-Fi chip opens the Wi-Fi card, the Home Assistant chip opens the Home
 * Assistant card (status, host, firmware version) and the MQTT chip the MQTT card (status, broker, device id). Both refresh themselves every two seconds while open.
 * Call from the LVGL task.
 */
#pragma once

#include <stdbool.h>

void ui_conn_popup_show_wifi(void);
void ui_conn_popup_show_ha(void);
void ui_conn_popup_show_mqtt(void);
/* Home Assistant state shown in the services card; ui_pages_set_topbar_status() passes it on. */
void ui_conn_popup_set_api_status(bool connected, bool initial_sync_done);
