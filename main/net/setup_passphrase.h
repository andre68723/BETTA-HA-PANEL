/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The first-run setup access point's WPA2 password: two words and two digits ("maple-river-42"),
 * easier to read off the display and type on a phone than twelve random characters.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define SETUP_PASSPHRASE_WORD_COUNT 1024U
/* Longest possible result is 16 characters; with the terminator and room to spare, WPA2 wants 8..63 characters. */
#define SETUP_PASSPHRASE_MAX_LEN 20U

/* Writes "word-word-NN" built from three random numbers (any value; they are reduced here, so
 * the caller passes raw esp_random() output). Returns the length, 0 if the buffer is too small. */
size_t setup_passphrase_format(char *out, size_t out_len, uint32_t r1, uint32_t r2, uint32_t r3);

/* The word list, for the host test. */
const char *setup_passphrase_word(size_t index);
