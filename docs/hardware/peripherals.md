<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->

# Cameras and chimes

These are driver and hardware-validation notes. For the controls themselves, see
[Display and brightness](../display.md) and [notification sounds](../notifications.md#sound).

## Ambient camera (paneljc and Tab5)

On paneljc the built-in OV02C10 is used as a light and presence sensor, never for image recording or streaming:
short bursts of frames (40 for a light reading every 20 s, 8
for movement about every 1.3 s while the display is dimmed), a 16x16 luma grid,
exposure and gain; PSRAM for two 1288x728 RGB565 buffers (about 3.8 MB), a 6 KB task.
The sensor's SCCB shares the touch I2C bus (GPIO 7/8). The camera is only opened while
"automatic brightness" or "wake on approach" is enabled. The calibration is rough (dark
room about -6.3, a phone light in front of the lens about -5.7, daylight in the study about
-4.65); `AMBIENT_LIGHT_DARK/BRIGHT` (-6.3 / -4.6) in `ambient_camera.c` map that to 10..100 %.

Tab5 uses its SC2356 through the SC202CS sensor driver and the M5Stack BSP camera power and I2C setup. It shares the duty-cycled luma and presence logic and supports the same Settings controls, but starts with both camera options off. In one ST7121/ESP32-P4 v1.3 trial on 2026-10-05, the room-light reading changed from about 89% to 27–28% when the lens was covered and returned to about 89% when uncovered. Broader brightness calibration, presence wake and long-term I2C/touch coexistence remain pending.

## Optional chime output

Current builds enable the audio path on panel4 and Tab5. The JC ES8311 path is
present but disabled by `APP_PANELJC_AUDIO=0`; it does not expose Sound settings.
The Waveshare LCD-4B and LCD-X manufacturer guides identify external-speaker
connectors. Guition's JC datasheet likewise labels a **Speaker Interface** on
the internal PCB; it does not establish an integrated loudspeaker. An audio
codec or amplifier alone does not establish that a speaker is fitted. Tab5 has
a built-in 1 W speaker, as specified by [M5Stack](https://docs.m5stack.com/en/core/Tab5).
Tab5 uses the BSP's ES8388 codec and shared I2C setup. A test chime played on 2026-10-05; the default
volume is 60%, with sound off and night mute on by default.

`main/drivers/audio_out.c` uses the existing touch I2C bus (GPIO7 SDA / GPIO8 SCL), never a
second bus driver. Both P4 boards use I2S MCLK13, BCLK12, LRCK10 and DOUT9; DIN11 is unused.
The amplifier enable is GPIO53 on the 4B and GPIO20 on the JC, including JC v2. Amplifier enable
follows codec startup and silent frames; synthesized tones have attack/release ramps. Stop
lowers PA, closes/deletes all codec interfaces and deletes the I2S channel. The codec library
requires stereo I2S slots, so the mono samples are duplicated into both slots.

The 4B BSP asks for `esp_codec_dev` ~1.5 (1.5.11 there); the JC has no BSP and runs 1.6.2. `panel10` has no enabled audio driver.
On 2026-09-28, the 4B completed 50 consecutive API test requests at 10% volume after the
I2S startup fix. The log showed 51 successful codec opens including the initial 40% test,
no audio/I2C errors, and no steady heap decline between requests. Night mute suppressed a
notification while active; disabling it allowed a notification tone (one further codec open).
This checks the driver path, not audible output. Speaker output, touch/camera coexistence and
pop suppression remain unverified; JC hardware tests remain pending. See
[notifications.md](../notifications.md#sound) for controls.
