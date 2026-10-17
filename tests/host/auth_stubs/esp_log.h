/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host stub for tests/host/test_admin_auth.c: just enough of ESP-IDF for main/settings/admin_auth.c.
 */
#pragma once
void host_log(const char *level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#define ESP_LOGI(tag, fmt, ...) host_log("I", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) host_log("W", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) host_log("E", tag, fmt, ##__VA_ARGS__)
