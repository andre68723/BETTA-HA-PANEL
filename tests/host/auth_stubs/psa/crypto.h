/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host stub for tests/host/test_admin_auth.c: just enough of ESP-IDF for main/settings/admin_auth.c.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int psa_status_t;
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
/* A deterministic stand-in for SHA-256: the test checks the state logic, not the cryptography. */
psa_status_t psa_hash_compute(int alg, const uint8_t *in, size_t in_len, uint8_t *out, size_t out_size, size_t *out_len);
