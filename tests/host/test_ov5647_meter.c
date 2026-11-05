/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 */
#include <assert.h>
#include <stdio.h>
#include "ov5647_meter.h"
#include "drivers/ambient_limits.h"

int main(void)
{
    uint32_t exposure = 77;
    float gain = 88;
    uint8_t sample[5] = {0, 0x3d, 0x80, 0, 16};
    assert(ov5647_meter_decode(sample, &exposure, &gain));
    assert(exposure == 984 && gain == 1.0f);
    sample[0] = 0xf0; sample[3] = 0xfc; sample[4] = 0xf8;
    assert(ov5647_meter_decode(sample, &exposure, &gain));
    assert(exposure == 984 && gain == 15.5f); /* reserved upper bits ignored */
    for (unsigned raw_gain = 16; raw_gain <= 1023; ++raw_gain) {
        sample[3] = raw_gain / 256; sample[4] = raw_gain % 256;
        assert(ov5647_meter_decode(sample, &exposure, &gain));
        assert(gain == raw_gain / 16.0f);
    }
    sample[3] = 0; sample[4] = 0;
    exposure = 77; gain = 88;
    assert(!ov5647_meter_decode(sample, &exposure, &gain));
    assert(exposure == 77 && gain == 88); /* invalid measurements publish nothing */
    sample[4] = 16; sample[0] = 0; sample[1] = 0; sample[2] = 15;
    assert(!ov5647_meter_decode(sample, &exposure, &gain));
    sample[0] = 15; sample[1] = 255; sample[2] = 255;
    assert(ov5647_meter_decode(sample, &exposure, &gain) && exposure == 65535);

    const int64_t now = INT64_C(6000000000);
    int64_t end = ambient_test_deadline(0, now, 30000);
    assert(end == now + 30000);
    assert(ambient_test_deadline(end, now + 10000, 30000) == end);
    assert(ambient_test_deadline(end, now + 30000, 90000) == now + 60000);
    assert(ambient_test_deadline(0, now, 0) == now);
    assert(ambient_capture_deadline(now, end, 1) == now + 2000);
    assert(ambient_capture_deadline(end - 250, end, 1) == end);
    assert(ambient_capture_deadline(end - 250, end, 0) == end + 1750);
    puts("OV5647 metering: register units, reserved bits, gain range, invalid samples and capture/test deadlines passed");
    return 0;
}
