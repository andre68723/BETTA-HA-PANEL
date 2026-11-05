# OV5647 provenance and local metering

Original sensor source: Espressif `esp_cam_sensor` 2.3.0 registry package,
repository commit `58d4c6eea08f0f78a2beff27aa4e0155efd74b55`.
Archive SHA256: `2d69928503bcc483c374b3b9b36a49f14caa9a068539a2eff360b72ceeb86e87`.
Original Apache-2.0 headers and component LICENSE are retained.
Trailing whitespace in the original ISP JSON is normalized.

Sensor modes, initialization tables, Kconfig and ISP profile come directly from
that archive. BETTA adds a read-only query through Espressif's existing
`ESP_CAM_SENSOR_STATS` interface. It reads actual automatic exposure and analogue
gain; the original exposure setter changes an AE target and cannot supply a
light measurement. Failed reads publish no synthetic values. The local helper
`ov5647_meter.h` converts five register bytes; host tests cover reserved bits,
gain range, invalid samples and the application's fixed test deadline.

Primary references:

- [Waveshare camera example](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-X/tree/f15098fe62072156db1e7baf9a0945801f8a8877/examples/esp-idf/09_video_lcd_display): existing BSP I2C bus, esp_video and RGB565 capture.
- [Espressif sensor API and original OV5647 driver](https://github.com/espressif/esp-video-components/tree/58d4c6eea08f0f78a2beff27aa4e0155efd74b55/esp_cam_sensor): register access and statistical-query interface.
- [Raspberry Pi Linux OV5647 driver](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/media/i2c/ov5647.c) consulted only for register-format facts: AEC 0x3500–0x3502 in sixteenths of a line, analogue gain 0x350a–0x350b with unity 16. No Linux source code was copied.

The panel10 profile selects 800 × 640 RAW8 and converts through the ISP to
RGB565. Physical light calibration, presence wake and audio/touch coexistence
remain unverified in this fork.
