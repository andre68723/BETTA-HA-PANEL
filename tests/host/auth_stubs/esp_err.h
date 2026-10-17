/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host stub for tests/host/test_admin_auth.c: just enough of ESP-IDF for main/settings/admin_auth.c.
 */
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NVS_INVALID_STATE 0x1101
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x1106
static inline const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK: return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    case ESP_ERR_NVS_NOT_FOUND: return "NVS_NOT_FOUND";
    case ESP_ERR_NVS_INVALID_LENGTH: return "NVS_INVALID_LENGTH";
    case ESP_ERR_NVS_INVALID_STATE: return "NVS_INVALID_STATE";
    default: return "ERR";
    }
}
