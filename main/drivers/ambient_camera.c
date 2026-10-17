/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Ambient light and presence sensing with the on-board MIPI-CSI camera
 * (OV02C10 on paneljc, SC2356/SC202CS on Tab5).
 * The V4L2 capture flow (esp_video, RGB565 through the ISP, user-pointer
 * buffers) follows the approach of the BETTA-kruzio project's local camera
 * module, reduced to a duty-cycled light sensor.
 */
#include "drivers/ambient_camera.h"

#include <stddef.h>
#include <string.h>

#include "sdkconfig.h"

#if CONFIG_APP_FEATURE_AMBIENT_CAMERA

#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linux/videodev2.h"

#include "drivers/display_init.h"
#include "settings/night_mode.h"

#define TAG_AMBIENT "ambient_cam"

#define AMBIENT_GRID 16
#define AMBIENT_BUFS 2
#define AMBIENT_BURST_FRAMES 40        /* the ISP auto exposure needs about a second to settle */
/* Share of changed grid cells that counts as presence, by sensitivity 1..10. */
static const uint8_t s_presence_pct_by_sensitivity[10] = {20, 16, 13, 10, 8, 6, 5, 4, 3, 2};
#define AMBIENT_CACHE_LINE 64
#define AMBIENT_MOTION_DELTA 12        /* luma change per cell that counts as movement */

#if defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
#include "bsp/m5stack_tab5.h"
#else
extern i2c_master_bus_handle_t touch_paneljc_i2c_bus(void);
#endif

static ambient_camera_reading_t s_reading;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_prev_grid[AMBIENT_GRID * AMBIENT_GRID];
static bool s_have_prev;
static int s_fd = -1;
static uint8_t *s_bufs[AMBIENT_BUFS];
static size_t s_buf_size;
static uint32_t s_width;
static uint32_t s_height;
static int32_t s_base_gain = 1;

static int ambient_ioctl(int cmd, void *arg)
{
    return ioctl(s_fd, cmd, arg);
}

static esp_err_t ambient_open(void)
{
#if defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
    /* The BSP enables camera power through the I/O expander and shares its
     * initialized I2C bus with touch before it registers /dev/video0. */
    esp_err_t err = bsp_camera_start(NULL);
#else
    esp_video_init_csi_config_t csi[] = {{
        .sccb_config = {
            .init_sccb = false,
            .i2c_handle = touch_paneljc_i2c_bus(),
            .freq = 100000,
        },
        .reset_pin = -1,
        .pwdn_pin = -1,
    }};
    if (csi[0].sccb_config.i2c_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_video_init_config_t cfg = {.csi = csi};
    esp_err_t err = esp_video_init(&cfg);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AMBIENT, "camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY | O_NONBLOCK);
    if (s_fd < 0) {
        ESP_LOGE(TAG_AMBIENT, "open %s failed", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        return ESP_FAIL;
    }

    struct v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    if (ambient_ioctl(VIDIOC_G_FMT, &fmt) != 0) {
        return ESP_FAIL;
    }
    s_width = fmt.fmt.pix.width;
    s_height = fmt.fmt.pix.height;
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
        struct v4l2_format req = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .fmt.pix = {.width = s_width, .height = s_height, .pixelformat = V4L2_PIX_FMT_RGB565},
        };
        if (ambient_ioctl(VIDIOC_S_FMT, &req) != 0) {
            ESP_LOGE(TAG_AMBIENT, "S_FMT RGB565 failed");
            return ESP_FAIL;
        }
    }

    s_buf_size = (size_t)s_width * s_height * 2;
    for (int i = 0; i < AMBIENT_BUFS; i++) {
        s_bufs[i] = heap_caps_aligned_calloc(AMBIENT_CACHE_LINE, 1, s_buf_size, MALLOC_CAP_SPIRAM);
        if (s_bufs[i] == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    struct v4l2_requestbuffers rb = {
        .count = AMBIENT_BUFS,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_USERPTR,
    };
    if (ambient_ioctl(VIDIOC_REQBUFS, &rb) != 0) {
        ESP_LOGE(TAG_AMBIENT, "REQBUFS failed");
        return ESP_FAIL;
    }

    struct v4l2_query_ext_ctrl qc = {.id = V4L2_CID_GAIN};
    struct v4l2_querymenu qm = {.id = V4L2_CID_GAIN};
    if (ambient_ioctl(VIDIOC_QUERY_EXT_CTRL, &qc) == 0) {
        qm.index = (uint32_t)qc.minimum;
        if (ambient_ioctl(VIDIOC_QUERYMENU, &qm) == 0 && qm.value > 0) {
            s_base_gain = (int32_t)qm.value;
        }
    }
    ESP_LOGI(TAG_AMBIENT, "camera ready %" PRIu32 "x%" PRIu32 ", base gain %d", s_width, s_height, (int)s_base_gain);
    return ESP_OK;
}

static bool ambient_queue_buffer(int index)
{
    struct v4l2_buffer b = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_USERPTR,
        .index = (uint32_t)index,
    };
    b.m.userptr = (unsigned long)s_bufs[index];
    b.length = s_buf_size;
    return ambient_ioctl(VIDIOC_QBUF, &b) == 0;
}

static float ambient_read_gain(void)
{
    /* The gain control is a menu: the current value is an index into it. */
    struct v4l2_ext_control c = {.id = V4L2_CID_GAIN};
    struct v4l2_ext_controls cs = {.ctrl_class = V4L2_CID_USER_CLASS, .count = 1, .controls = &c};
    if (ambient_ioctl(VIDIOC_G_EXT_CTRLS, &cs) != 0) {
        return 1.0f;
    }
    struct v4l2_querymenu qm = {.id = V4L2_CID_GAIN, .index = (uint32_t)c.value};
    if (ambient_ioctl(VIDIOC_QUERYMENU, &qm) != 0 || qm.value <= 0) {
        return 1.0f;
    }
    return (float)qm.value / (float)s_base_gain;
}

static float ambient_read_exposure(void)
{
    struct v4l2_ext_control c = {.id = V4L2_CID_EXPOSURE};
    struct v4l2_ext_controls cs = {.ctrl_class = V4L2_CID_CAMERA_CLASS, .count = 1, .controls = &c};
    if (ambient_ioctl(VIDIOC_G_EXT_CTRLS, &cs) != 0) {
        return 1.0f;
    }
    return (float)c.value;
}

static inline uint8_t ambient_luma(uint16_t px)
{
    const uint32_t r = (px >> 11) & 0x1F;
    const uint32_t g = (px >> 5) & 0x3F;
    const uint32_t b = px & 0x1F;
    /* Rec.601 weights on 5/6/5-bit channels scaled to 0..255. */
    return (uint8_t)((r * 8 * 77 + g * 4 * 150 + b * 8 * 29) >> 8);
}

/* Reduce the frame to a coarse luma grid (each cell samples a 4x4 pixel patch). */
static void ambient_frame_to_grid(const uint8_t *frame, uint8_t *grid)
{
    const uint16_t *px = (const uint16_t *)frame;
    for (int gy = 0; gy < AMBIENT_GRID; gy++) {
        for (int gx = 0; gx < AMBIENT_GRID; gx++) {
            const uint32_t x0 = (uint32_t)((gx * 2 + 1) * s_width / (AMBIENT_GRID * 2)) - 2;
            const uint32_t y0 = (uint32_t)((gy * 2 + 1) * s_height / (AMBIENT_GRID * 2)) - 2;
            uint32_t sum = 0;
            for (uint32_t y = 0; y < 4; y++) {
                for (uint32_t x = 0; x < 4; x++) {
                    sum += ambient_luma(px[(y0 + y) * s_width + x0 + x]);
                }
            }
            grid[gy * AMBIENT_GRID + gx] = (uint8_t)(sum / 16);
        }
    }
}

/* Runtime configuration (set once at boot from the settings). */
static bool s_auto_brightness;
static bool s_presence_wake;
static bool s_night_wake;
static int s_presence_pct = 8;
static int64_t s_last_light_ms;
/* Set by ambient_camera_start_test(), read by ambient_task(); a coarse timestamp compare
 * tolerates the odd torn read on a 32-bit target the same way s_auto_brightness/s_presence_wake
 * already do, so no lock. 0 means "no test running". */
static int64_t s_test_until_ms;

/* Frame index whose grid is used for movement: the ISP auto exposure restarts
 * the same way on every stream start, so this frame always looks alike. */
#define AMBIENT_MOTION_FRAME 7

/* Light index range that maps to 10 % .. 100 % display brightness. */
#define AMBIENT_LIGHT_DARK (-6.3f)
#define AMBIENT_LIGHT_BRIGHT (-4.6f) /* measured: daylight in the study at 10:30 read -4.65 */
#define AMBIENT_LIGHT_INTERVAL_MS 20000

static int64_t ambient_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int ambient_light_to_percent(float light_index)
{
    float t = (light_index - AMBIENT_LIGHT_DARK) / (AMBIENT_LIGHT_BRIGHT - AMBIENT_LIGHT_DARK);
    t = fminf(fmaxf(t, 0.0f), 1.0f);
    return 10 + (int)(t * 90.0f + 0.5f);
}

/* Movement between this grid and the previous one; false while there is no previous grid. */
static bool ambient_motion(const uint8_t *grid, int *changed_pct, int *level)
{
    bool valid = s_have_prev;
    int changed = 0;
    int same_sign = 0;
    uint32_t diff_sum = 0;
    if (valid) {
        int pos = 0;
        int neg = 0;
        for (int i = 0; i < AMBIENT_GRID * AMBIENT_GRID; i++) {
            const int d = (int)grid[i] - (int)s_prev_grid[i];
            diff_sum += (uint32_t)abs(d);
            if (abs(d) >= AMBIENT_MOTION_DELTA) {
                changed++;
                if (d > 0) {
                    pos++;
                } else {
                    neg++;
                }
            }
        }
        same_sign = pos > neg ? pos : neg;
    }
    memcpy(s_prev_grid, grid, AMBIENT_GRID * AMBIENT_GRID);
    s_have_prev = true;
    *changed_pct = changed * 100 / (AMBIENT_GRID * AMBIENT_GRID);
    *level = (int)(diff_sum / (AMBIENT_GRID * AMBIENT_GRID));
    /* A lighting change moves almost every cell the same way: that is not presence. */
    if (*changed_pct >= 60 && same_sign * 100 >= changed * 90) {
        *changed_pct = 0;
    }
    return valid;
}

static void ambient_sample(int frames, bool want_light)
{
    for (int i = 0; i < AMBIENT_BUFS; i++) {
        if (!ambient_queue_buffer(i)) {
            ESP_LOGW(TAG_AMBIENT, "QBUF %d failed", i);
            return;
        }
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ambient_ioctl(VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGW(TAG_AMBIENT, "STREAMON failed");
        return;
    }

    uint8_t motion_grid[AMBIENT_GRID * AMBIENT_GRID];
    bool have_motion_grid = false;
    int last = -1;
    for (int i = 0; i < frames; i++) {
        struct v4l2_buffer b = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_USERPTR,
        };
        /* Non-blocking descriptor: poll for up to one second per frame. */
        int waited_ms = 0;
        while (ambient_ioctl(VIDIOC_DQBUF, &b) != 0 && waited_ms < 1000) {
            vTaskDelay(pdMS_TO_TICKS(5));
            waited_ms += 5;
        }
        if (waited_ms >= 1000) {
            ESP_LOGW(TAG_AMBIENT, "frame %d timed out", i);
            break;
        }
        last = (int)b.index;
        if (i == AMBIENT_MOTION_FRAME) {
            ambient_frame_to_grid(s_bufs[last], motion_grid);
            have_motion_grid = true;
        }
        if (i < frames - 1 && !ambient_queue_buffer(last)) {
            break;
        }
    }

    const float exposure = ambient_read_exposure();
    const float gain = ambient_read_gain();
    (void)ambient_ioctl(VIDIOC_STREAMOFF, &type);
    if (last < 0 || !have_motion_grid) {
        ESP_LOGW(TAG_AMBIENT, "no usable frame");
        return;
    }

    ambient_camera_reading_t r;
    portENTER_CRITICAL(&s_lock);
    r = s_reading;
    portEXIT_CRITICAL(&s_lock);

    int changed_pct = 0;
    int level = 0;
    const bool motion_valid = ambient_motion(motion_grid, &changed_pct, &level);
    r.motion_pct = motion_valid ? changed_pct : 0;
    r.motion_level = motion_valid ? level : 0;

    if (want_light) {
        uint8_t grid[AMBIENT_GRID * AMBIENT_GRID];
        ambient_frame_to_grid(s_bufs[last], grid);
        uint32_t sum = 0;
        for (int i = 0; i < AMBIENT_GRID * AMBIENT_GRID; i++) {
            sum += grid[i];
        }
        r.luma = (float)sum / (AMBIENT_GRID * AMBIENT_GRID);
        r.exposure = exposure;
        r.gain = gain;
        /* Relative scene brightness: linearised pixel value divided by what
         * the sensor needed (exposure * gain) to get there. */
        const float lin = powf(fmaxf(r.luma, 1.0f) / 255.0f, 2.2f);
        r.light_index = log10f(lin / fmaxf(exposure * gain, 1.0f));
        r.valid = true;
        s_last_light_ms = ambient_now_ms();
        ESP_LOGI(TAG_AMBIENT, "luma %.1f exposure %.0f gain %.2f light %.2f -> %d%%", (double)r.luma,
            (double)exposure, (double)gain, (double)r.light_index, ambient_light_to_percent(r.light_index));
        if (s_auto_brightness) {
            display_set_ambient_limit_percent(ambient_light_to_percent(r.light_index));
        }
    }
    portENTER_CRITICAL(&s_lock);
    s_reading = r;
    portEXIT_CRITICAL(&s_lock);

    /* Someone moved in front of the panel while it is dimmed: wake it up. */
    if (s_presence_wake && motion_valid && r.motion_pct >= s_presence_pct && display_is_dimmed() &&
        (s_night_wake || !night_mode_active())) {
        ESP_LOGI(TAG_AMBIENT, "presence: %d%% of the cells changed, waking the display", r.motion_pct);
        display_note_activity();
    }
}

static void ambient_task(void *arg)
{
    (void)arg;
    /* Let boot finish (Wi-Fi, LVGL) before touching the camera. */
    vTaskDelay(pdMS_TO_TICKS(20000));
    bool opened = false;
    for (;;) {
        const bool test_active = s_test_until_ms > 0 && ambient_now_ms() < s_test_until_ms;
        if (s_test_until_ms > 0 && !test_active) {
            s_test_until_ms = 0; /* expired */
        }
        /* The features can be switched on and off live from the editor: the
         * camera is only opened once one of them (or a live test) is on. */
        if (!s_auto_brightness && !s_presence_wake && !test_active) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        if (!opened) {
            if (ambient_open() != ESP_OK) {
                ESP_LOGE(TAG_AMBIENT, "camera unavailable, ambient sensing disabled");
                vTaskDelete(NULL);
                return;
            }
            opened = true;
        }
        const bool need_light = s_auto_brightness && ambient_now_ms() - s_last_light_ms >= AMBIENT_LIGHT_INTERVAL_MS;
        const bool need_motion = test_active || (s_presence_wake && display_is_dimmed() && (s_night_wake || !night_mode_active()));
        if (need_light || need_motion) {
            ambient_sample(need_light ? AMBIENT_BURST_FRAMES : AMBIENT_MOTION_FRAME + 1, need_light);
        } else {
            s_have_prev = false; /* the next comparison must not span a long pause */
        }
        vTaskDelay(pdMS_TO_TICKS(need_motion ? 1000 : 2000));
    }
}

void ambient_camera_configure(bool auto_brightness, bool presence_wake, int sensitivity, bool night_wake)
{
    s_auto_brightness = auto_brightness;
    s_presence_wake = presence_wake;
    s_night_wake = night_wake;
    s_last_light_ms = 0;
    if (!auto_brightness) {
        display_set_ambient_limit_percent(100); /* no room-light limit any more */
    }
    if (sensitivity < 1) sensitivity = 1;
    if (sensitivity > 10) sensitivity = 10;
    s_presence_pct = s_presence_pct_by_sensitivity[sensitivity - 1];
}

void ambient_camera_start(void)
{
    (void)xTaskCreate(ambient_task, "ambient_cam", 6 * 1024, NULL, 3, NULL);
}

bool ambient_camera_get(ambient_camera_reading_t *out)
{
    if (out == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_reading;
    portEXIT_CRITICAL(&s_lock);
    return out->valid;
}

bool ambient_camera_available(void)
{
    return true;
}

int ambient_camera_get_presence_threshold_pct(void)
{
    return s_presence_pct;
}

void ambient_camera_start_test(uint32_t duration_ms)
{
    s_test_until_ms = ambient_now_ms() + (int64_t)duration_ms;
}

bool ambient_camera_test_active(int *out_remaining_s)
{
    const int64_t remaining_ms = s_test_until_ms - ambient_now_ms();
    if (s_test_until_ms <= 0 || remaining_ms <= 0) {
        if (out_remaining_s != NULL) {
            *out_remaining_s = 0;
        }
        return false;
    }
    if (out_remaining_s != NULL) {
        *out_remaining_s = (int)((remaining_ms + 999) / 1000);
    }
    return true;
}

#else /* !CONFIG_APP_FEATURE_AMBIENT_CAMERA */

void ambient_camera_start(void) {}

void ambient_camera_configure(bool auto_brightness, bool presence_wake, int sensitivity, bool night_wake)
{
    (void)auto_brightness;
    (void)presence_wake;
    (void)sensitivity;
    (void)night_wake;
}

bool ambient_camera_get(ambient_camera_reading_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    return false;
}

bool ambient_camera_available(void)
{
    return false;
}

int ambient_camera_get_presence_threshold_pct(void)
{
    return 0;
}

void ambient_camera_start_test(uint32_t duration_ms)
{
    (void)duration_ms;
}

bool ambient_camera_test_active(int *out_remaining_s)
{
    if (out_remaining_s != NULL) {
        *out_remaining_s = 0;
    }
    return false;
}

#endif
