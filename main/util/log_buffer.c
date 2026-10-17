/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/log_buffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define LOG_BUFFER_BYTES (32 * 1024)
#define LOG_LINE_MAX 256

static char *s_buf = NULL;
static size_t s_head = 0; /* next write position */
static size_t s_len = 0;  /* valid bytes, at most LOG_BUFFER_BYTES */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_prev_vprintf = NULL;

static void append(const char *data, size_t n)
{
    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < n; i++) {
        s_buf[s_head] = data[i];
        s_head = (s_head + 1) % LOG_BUFFER_BYTES;
    }
    s_len += n;
    if (s_len > LOG_BUFFER_BYTES) {
        s_len = LOG_BUFFER_BYTES;
    }
    portEXIT_CRITICAL(&s_lock);
}

static int log_vprintf(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    char line[LOG_LINE_MAX];
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (n > 0 && s_buf != NULL) {
        size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
        /* Drop the ANSI colour escapes (ESC [ ... m) the IDF adds to every line. */
        char clean[LOG_LINE_MAX];
        size_t o = 0;
        for (size_t i = 0; i < len; i++) {
            if (line[i] == '\033') {
                while (i < len && line[i] != 'm') {
                    i++;
                }
                continue;
            }
            clean[o++] = line[i];
        }
        append(clean, o);
    }
    return s_prev_vprintf != NULL ? s_prev_vprintf(fmt, args) : vprintf(fmt, args);
}

void log_buffer_init(void)
{
    if (s_buf != NULL) {
        return;
    }
    s_buf = heap_caps_malloc(LOG_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_buf == NULL) {
        return;
    }
    s_prev_vprintf = esp_log_set_vprintf(log_vprintf);
}

size_t log_buffer_size(void)
{
    portENTER_CRITICAL(&s_lock);
    size_t n = s_len;
    portEXIT_CRITICAL(&s_lock);
    return n;
}

size_t log_buffer_read(size_t offset, char *out, size_t len)
{
    if (s_buf == NULL) {
        return 0;
    }
    size_t copied = 0;
    portENTER_CRITICAL(&s_lock);
    if (offset < s_len) {
        size_t avail = s_len - offset;
        copied = avail < len ? avail : len;
        size_t start = (s_head + LOG_BUFFER_BYTES - s_len + offset) % LOG_BUFFER_BYTES;
        for (size_t i = 0; i < copied; i++) {
            out[i] = s_buf[(start + i) % LOG_BUFFER_BYTES];
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return copied;
}
