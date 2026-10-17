/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host stub for the worker test (tests/host/test_history_week.c): pthread-backed FreeRTOS primitives, a counting heap and
 * a synchronous LVGL async call, so the real main/ha/ha_history_week.c runs on the host.
 */
#pragma once
void host_log(const char *level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#define ESP_LOGI(tag, fmt, ...) host_log("I", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) host_log("W", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) host_log("E", tag, fmt, ##__VA_ARGS__)
