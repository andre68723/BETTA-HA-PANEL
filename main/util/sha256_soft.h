/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * SHA-256 in plain C, without the SHA engine of the chip. The engine is shared by every task that does TLS or hashing, and a
 * long running streaming hash (the firmware download, 4.8 MB over 40 s) has been seen to come out wrong while other tasks used it.
 * Slower than the engine but independent of what else runs. Pure (host tested against the FIPS 180-4 vectors).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t length; /* bytes hashed so far */
    uint8_t block[64];
    size_t block_len;
} sha256_soft_t;

void sha256_soft_init(sha256_soft_t *ctx);
void sha256_soft_update(sha256_soft_t *ctx, const uint8_t *data, size_t len);
void sha256_soft_final(sha256_soft_t *ctx, uint8_t digest[32]);
