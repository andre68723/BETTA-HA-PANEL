/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/atomic_file.h"
#include "util/flash_gate.h"
#include "util/flash_trace.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ATOMIC_FILE_PATH_MAX 160
#define ATOMIC_FILE_SUFFIX ".tmp"

esp_err_t atomic_file_write_locked(const char *path, const void *data, size_t len)
{
    if (path == NULL || path[0] == '\0' || (data == NULL && len > 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    char tmp[ATOMIC_FILE_PATH_MAX + sizeof(ATOMIC_FILE_SUFFIX)];
    if (strlen(path) > ATOMIC_FILE_PATH_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(tmp, sizeof(tmp), "%s%s", path, ATOMIC_FILE_SUFFIX);

    FLASH_TRACE_BEGIN(trace_start);
    FILE *f = fopen(tmp, "wb");
    if (f == NULL) {
        return ESP_FAIL;
    }
    bool ok = len == 0U || fwrite(data, 1U, len, f) == len;
    ok = ok && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        remove(tmp);
        return ESP_FAIL;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return ESP_FAIL;
    }
    FLASH_TRACE_END(trace_start, path, len);
    return ESP_OK;
}

esp_err_t atomic_file_write(const char *path, const void *data, size_t len)
{
    if (path == NULL || path[0] == '\0' || strlen(path) > ATOMIC_FILE_PATH_MAX || (data == NULL && len > 0U))
        return ESP_ERR_INVALID_ARG;
    if (!flash_gate_enter(2000)) return ESP_ERR_TIMEOUT;
    esp_err_t err = atomic_file_write_locked(path, data, len);
    flash_gate_leave();
    return err;
}
