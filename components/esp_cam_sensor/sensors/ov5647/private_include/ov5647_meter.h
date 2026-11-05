/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 andre68723
 * OV5647 register-format conversion for ambient-light metering.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* AEC registers 0x3500..0x3502: twenty bits, sixteen units per line.
 * Analogue gain 0x350a..0x350b: ten bits, sixteen units per 1x gain.
 * Input order: exposure high/middle/low, gain high/low. */
static inline bool ov5647_meter_decode(const uint8_t registers[5], uint32_t *lines, float *gain)
{
    uint32_t shutter = ((uint32_t)(registers[0] & 15) * 65536u +
                        (uint32_t)registers[1] * 256u + registers[2]) / 16u;
    unsigned amplification = (unsigned)(registers[3] & 3) * 256u + registers[4];
    if (shutter == 0 || amplification < 16) return false;
    *lines = shutter;
    *gain = (float)amplification / 16.0f;
    return true;
}
