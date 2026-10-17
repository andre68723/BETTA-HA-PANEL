/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * OV02C10 register definitions.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define OV02C10_REG_END      0xffff
#define OV02C10_REG_DELAY    0xfffe

/* ov02c10 registers */
#define OV02C10_REG_SENSOR_ID_H             0x300a
#define OV02C10_REG_SENSOR_ID_L             0x300b

#define OV02C10_REG_GROUP_HOLD              0x3208
#define OV02C10_REG_GROUP_HOLD_DELAY        0x3800

#define OV02C10_REG_DIG_COARSE_GAIN         0x3508
#define OV02C10_REG_DIG_FINE_GAIN           0x3509
#define OV02C10_REG_ANG_COARSE_GAIN         0x350a
#define OV02C10_REG_ANG_FINE_GAIN           0x350b

#define OV02C10_REG_AEC_AGC_CTL    0x3503

// #define OV02C10_REG_SHUTTER_TIME_H          0x3e00

#define OV02C10_REG_SHUTTER_TIME_M          0x3501
#define OV02C10_REG_SHUTTER_TIME_L          0x3502


#define OV02C10_REG_TOTAL_WIDTH_H           0x380c // HTS,line width
#define OV02C10_REG_TOTAL_WIDTH_L           0x380d
#define OV02C10_REG_TOTAL_HEIGHT_H          0x380e // VTS,frame height
#define OV02C10_REG_TOTAL_HEIGHT_L          0x380f

#define OV02C10_REG_SLEEP_MODE              0x0100

#ifdef __cplusplus
}
#endif
