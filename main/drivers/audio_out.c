/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "drivers/audio_out.h"
#include "util/chime_synth.h"
#include <string.h>
#include "sdkconfig.h"
#include "app_config.h"

#if defined(CONFIG_APP_PANEL_VARIANT_4INCH_720) || (defined(CONFIG_APP_PANEL_VARIANT_10INCH_JC) && APP_PANELJC_AUDIO)
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if defined(CONFIG_APP_PANEL_VARIANT_4INCH_720)
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#define AUDIO_PA_GPIO GPIO_NUM_53
#else
#include "drivers/touch_init.h"
#define AUDIO_PA_GPIO GPIO_NUM_20
#endif

bool audio_out_available(void) { return true; }

esp_err_t audio_out_play(const int16_t *pcm, size_t samples, int volume, bool (*cancel)(void))
{
    if (!pcm || !samples || volume < 0 || volume > 100) return ESP_ERR_INVALID_ARG;
#if defined(CONFIG_APP_PANEL_VARIANT_4INCH_720)
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
#else
    i2c_master_bus_handle_t bus = touch_paneljc_i2c_bus();
#endif
    if (!bus) return ESP_ERR_INVALID_STATE;
    i2s_chan_handle_t tx = NULL;
    const audio_codec_data_if_t *data = NULL;
    const audio_codec_ctrl_if_t *ctrl = NULL;
    const audio_codec_if_t *codec = NULL;
    esp_codec_dev_handle_t device = NULL;
    bool tx_enabled = false;
    bool device_open = false;
    esp_err_t err = ESP_FAIL;
    const gpio_config_t pa = {.pin_bit_mask = 1ULL << AUDIO_PA_GPIO, .mode = GPIO_MODE_OUTPUT};
    if ((err = gpio_config(&pa)) != ESP_OK) return err;
    gpio_set_level(AUDIO_PA_GPIO, 0);
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 4;
    channel.dma_frame_num = 256;
    channel.auto_clear = true;
    if ((err = i2s_new_channel(&channel, &tx, NULL)) != ESP_OK) goto done;
    const i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CHIME_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = GPIO_NUM_13, .bclk = GPIO_NUM_12, .ws = GPIO_NUM_10,
                     .dout = GPIO_NUM_9, .din = I2S_GPIO_UNUSED}
    };
    if ((err = i2s_channel_init_std_mode(tx, &config)) != ESP_OK) goto done;
    /* ES8311 needs MCLK before register initialization. The codec data interface
     * also expects a running channel when it stops TX to set the sample format. */
    if ((err = i2s_channel_enable(tx)) != ESP_OK) goto done;
    tx_enabled = true;
    audio_codec_i2s_cfg_t data_config = {.port = I2S_NUM_0, .tx_handle = tx};
    audio_codec_i2c_cfg_t ctrl_config = {
        .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bus
    };
    data = audio_codec_new_i2s_data(&data_config);
    ctrl = audio_codec_new_i2c_ctrl(&ctrl_config);
    if (!data || !ctrl) { err = ESP_ERR_NO_MEM; goto done; }
    es8311_codec_cfg_t codec_config = {
        .ctrl_if = ctrl, .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1, .use_mclk = true, .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3}
    };
    codec = es8311_codec_new(&codec_config);
    if (!codec) { err = ESP_ERR_NO_MEM; goto done; }
    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data
    };
    device = esp_codec_dev_new(&device_config);
    if (!device) { err = ESP_ERR_NO_MEM; goto done; }
    esp_codec_dev_sample_info_t info = {
        .sample_rate = CHIME_SAMPLE_RATE, .channel = 2, .bits_per_sample = 16
    };
    if (esp_codec_dev_open(device, &info) != ESP_CODEC_DEV_OK) { err = ESP_FAIL; goto done; }
    device_open = true;
    if (esp_codec_dev_set_out_vol(device, 0) != ESP_CODEC_DEV_OK) { err = ESP_FAIL; goto done; }
    /* esp_codec_dev requires two I2S slots; duplicate the mono tone into both. */
    int16_t chunk[512] = {0};
    if (esp_codec_dev_write(device, chunk, sizeof(chunk)) != ESP_CODEC_DEV_OK) { err = ESP_FAIL; goto done; }
    gpio_set_level(AUDIO_PA_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    if (esp_codec_dev_set_out_vol(device, volume) != ESP_CODEC_DEV_OK) { err = ESP_FAIL; goto done; }
    err = ESP_OK;
    for (size_t offset = 0; offset < samples; offset += 256) {
        if (cancel && cancel()) break;
        size_t count = samples - offset < 256 ? samples - offset : 256;
        for (size_t i = 0; i < count; i++) chunk[2 * i] = chunk[2 * i + 1] = pcm[offset + i];
        if (esp_codec_dev_write(device, chunk, count * 2 * sizeof(*pcm)) != ESP_CODEC_DEV_OK) {
            err = ESP_FAIL; break;
        }
    }
    /* Drain queued samples before lowering PA; a restart aborts at most one DMA queue later. */
    memset(chunk, 0, sizeof(chunk));
    (void)esp_codec_dev_write(device, chunk, sizeof(chunk));
    vTaskDelay(pdMS_TO_TICKS(80));
done:
    if (device) (void)esp_codec_dev_set_out_vol(device, 0);
    gpio_set_level(AUDIO_PA_GPIO, 0);
    if (device) { (void)esp_codec_dev_close(device); esp_codec_dev_delete(device); }
    if (tx_enabled && !device_open) (void)i2s_channel_disable(tx);
    if (codec) audio_codec_delete_codec_if(codec);
    if (ctrl) audio_codec_delete_ctrl_if(ctrl);
    if (data) audio_codec_delete_data_if(data);
    if (tx) (void)i2s_del_channel(tx);
    if (err != ESP_OK) ESP_LOGW("audio", "Playback failed: %s", esp_err_to_name(err));
    return err;
}
#elif defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
/* M5Stack Tab5: ES8388 codec and power amplifier through the BSP (espressif/m5stack_tab5). Written from the BSP's
 * source and the other boards' sequence; NOT yet heard on a device (no Tab5 hardware here). */
#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_codec_dev_handle_t s_speaker; /* the BSP never frees its I2S channel, so the codec is created once */

bool audio_out_available(void) { return true; }

esp_err_t audio_out_play(const int16_t *pcm, size_t samples, int volume, bool (*cancel)(void))
{
    if (!pcm || !samples || volume < 0 || volume > 100) return ESP_ERR_INVALID_ARG;
    if (s_speaker == NULL) {
        s_speaker = bsp_audio_codec_speaker_init(); /* I2C, I2S, speaker feature, ES8388 and its amplifier pin */
        if (s_speaker == NULL) return ESP_ERR_NOT_FOUND;
    }
    esp_codec_dev_sample_info_t info = {.sample_rate = CHIME_SAMPLE_RATE, .channel = 2, .bits_per_sample = 16};
    if (esp_codec_dev_open(s_speaker, &info) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    esp_err_t err = ESP_FAIL;
    int16_t chunk[512] = {0};
    /* Open silent, let the amplifier settle, then raise the volume: no pop at the start. */
    if (esp_codec_dev_set_out_vol(s_speaker, 0) != ESP_CODEC_DEV_OK) goto done;
    if (esp_codec_dev_write(s_speaker, chunk, sizeof(chunk)) != ESP_CODEC_DEV_OK) goto done;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (esp_codec_dev_set_out_vol(s_speaker, volume) != ESP_CODEC_DEV_OK) goto done;
    err = ESP_OK;
    for (size_t offset = 0; offset < samples; offset += 256) {
        if (cancel && cancel()) break;
        size_t count = samples - offset < 256 ? samples - offset : 256;
        for (size_t i = 0; i < count; i++) chunk[2 * i] = chunk[2 * i + 1] = pcm[offset + i];
        if (esp_codec_dev_write(s_speaker, chunk, count * 2 * sizeof(*pcm)) != ESP_CODEC_DEV_OK) {
            err = ESP_FAIL;
            break;
        }
    }
    memset(chunk, 0, sizeof(chunk));
    (void)esp_codec_dev_write(s_speaker, chunk, sizeof(chunk)); /* drain before the amplifier goes off */
    vTaskDelay(pdMS_TO_TICKS(80));
done:
    (void)esp_codec_dev_set_out_vol(s_speaker, 0);
    (void)esp_codec_dev_close(s_speaker);
    if (err != ESP_OK) ESP_LOGW("audio", "Playback failed: %s", esp_err_to_name(err));
    return err;
}
#else
bool audio_out_available(void) { return false; }
esp_err_t audio_out_play(const int16_t *pcm, size_t samples, int volume, bool (*cancel)(void))
{
    (void)pcm; (void)samples; (void)volume; (void)cancel;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
