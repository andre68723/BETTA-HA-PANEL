/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <sdkconfig.h>
#include "ov02c10_regs.h"
#include "ov02c10_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_SOC_MIPI_CSI_SUPPORTED

#include "ov02c10_mipi_1lane_24Minput_1920x1080_raw10_30fps.h"

#include "ov02c10_mipi_1lane_24Minput_1288x728_raw10_30fps.h"

#include "ov02c10_mipi_2lane_24Minput_1920x1080_raw10_30fps.h"

#endif


#ifdef __cplusplus
}
#endif
