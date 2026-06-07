/* Vendored third-party code — NOT covered by this project's LicenseRef-FNCL-1.1.
 *
 * Header for gsl_point_id.c: Silead touch-point tracking algorithm from the
 * Linux kernel driver drivers/input/touchscreen/mediatek/gslX680/,
 * Copyright 2010-2016 Silead Inc., GPL-2.0-or-later (see gsl_point_id.c).
 * Vendored via sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6.
 */
#ifndef _GSL_POINT_ID_H
#define _GSL_POINT_ID_H

struct gsl_touch_info
{
    int x[10];
    int y[10];
    int id[10];
    int finger_num;
};

unsigned int gsl_mask_tiaoping(void);
unsigned int gsl_version_id(void);
void gsl_alg_id_main(struct gsl_touch_info *cinfo);
void gsl_DataInit(unsigned int *conf_in);

#endif
