#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Re-applies the small edits this project needs in components fetched into managed_components/.

The component manager replaces managed_components/ whenever a manifest changes, which used to wipe these
edits by hand. The root CMakeLists.txt now runs this on every configure (right after project()), so there
is nothing to remember to run by hand any more; this file is for checking or applying it outside of a
build. It is safe to run any time: an edit that is already in place is reported and left alone, one whose
place cannot be found (a new component version) is reported as a warning and the exit code is 1.

  tools/apply_component_patches.py            apply
  tools/apply_component_patches.py --check    only report, change nothing
"""
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MC = Path(os.environ.get("COMPONENTS_DIR", ROOT / "managed_components"))


def replace(path, old, new):
    """Text edit: old -> new. Returns 'applied' / 'present' / 'missing'."""
    text = path.read_text(encoding="utf-8")
    if new in text:
        return "present", None
    if old not in text:
        return "missing", None
    return "applied", text.replace(old, new, 1)


def replace_ppa_legacy(path, old, new):
    """The bounded-wait patch may already have the newer diagnostic hook signature."""
    text = path.read_text(encoding="utf-8")
    upgraded = new.replace(
        "__attribute__((weak)) void lvgl_port_ppa_timeout_hook(void) {}",
        "__attribute__((weak)) void lvgl_port_ppa_timeout_hook(uint32_t width, uint32_t height, uint32_t x, uint32_t y,\n"
        "    uint32_t rotation, uintptr_t input, uintptr_t output, uint32_t capacity) {}",
    ).replace(
        "        lvgl_port_ppa_timeout_hook();",
        "        lvgl_port_ppa_timeout_hook(w, h, input_x, input_y, (uint32_t)rotate_cfg->rotation * 90U,\n"
        "            (uintptr_t)rotate_cfg->in_buff, (uintptr_t)ppa_ctx->buffer, ppa_ctx->buffer_size);",
    )
    if upgraded in text:
        return "present", None
    return replace(path, old, new)


def ensure_requires(path, keyword, tokens):
    """Make sure `tokens` are in the `keyword` list (REQUIRES / PRIV_REQUIRES) of idf_component_register()."""
    text = path.read_text(encoding="utf-8")
    m = re.search(r"^(\s*%s\s+)([^\n]*)$" % re.escape(keyword), text, re.M)
    if not m:
        return "missing", None
    have = m.group(2).split()
    add = [t for t in tokens if t not in have]
    if not add:
        return "present", None
    return "applied", text[: m.start(2)] + " ".join(have + add) + text[m.end(2):]


# (component directory, file inside it, what, edit function, its arguments[, optional: the place may not exist])
PATCHES = [
    ("espressif__bmi270", "src/bmi270.c",
     "bmi270: bounded I2C reads on the shared Tab5 touch/backlight bus",
     replace, ("i2c_master_transmit_receive(dev_handle->i2c_handle, &reg, 1, data, data_len, -1)",
               "i2c_master_transmit_receive(dev_handle->i2c_handle, &reg, 1, data, data_len, 100) /* BETTA: 100 ms timeout */")),
    ("espressif__bmi270", "src/bmi270.c",
     "bmi270: bounded I2C writes on the shared Tab5 touch/backlight bus",
     replace, ("i2c_master_transmit(dev_handle->i2c_handle, packet, packet_len, -1)",
               "i2c_master_transmit(dev_handle->i2c_handle, packet, packet_len, 100) /* BETTA: 100 ms timeout */")),
    ("espressif__esp_hosted", "host/port/esp/freertos/include/port_esp_hosted_host_config.h",
     "esp_hosted: SDIO byte mode TX (workaround for esp-hosted-mcu#167), all P4 variants",
     replace, ("#define H_SDIO_TX_BLOCK_ONLY_XFER (1)", "#define H_SDIO_TX_BLOCK_ONLY_XFER (0)")),
    ("espressif__esp_hosted", "host/drivers/transport/sdio/sdio_drv.c",
     "esp_hosted: bounded esp_hosted_tx() queue wait instead of an infinite one, all P4 variants",
     replace, (
        "\tg_h.funcs->_h_queue_item(to_slave_queue[pkt_prio], &buf_handle, HOSTED_BLOCK_MAX);\n"
        "\tg_h.funcs->_h_post_semaphore(sem_to_slave_queue);\n",
        "\t/* andre68723/BETTA-HA-PANEL fork patch (tools/apply_component_patches.py), see docs/release-notes.md\n"
        "\t * \"ESP-IDF 6.1 and the ESP32-C6 link\": HOSTED_BLOCK_MAX is portMAX_DELAY, an infinite wait --\n"
        "\t * if to_slave_queue[pkt_prio] stays full (a burst of concurrent outbound traffic, e.g. several\n"
        "\t * HTTP responses going out to a browser at once, faster than the SDIO write task drains it)\n"
        "\t * this call used to block forever, and since it runs on lwIP's own task for an outbound Wi-Fi\n"
        "\t * packet, that froze the entire network stack -- no more HTTP, no more ICMP ping, nothing --\n"
        "\t * until a hardware reset. A bounded wait turns that into a dropped packet (TCP retransmits),\n"
        "\t * the same graceful degradation already used a few lines up when mempool_alloc() itself fails.\n"
        "\t * 300 ticks assumes CONFIG_FREERTOS_HZ=1000 (1 tick = 1 ms), true for every variant in this\n"
        "\t * fork; pdMS_TO_TICKS is not used here to avoid adding a FreeRTOS include to this file just\n"
        "\t * for one constant. */\n"
        "\tint queue_err = g_h.funcs->_h_queue_item(to_slave_queue[pkt_prio], &buf_handle, 300);\n"
        "\tif (queue_err != 0) {\n"
        "\t\tESP_LOGW(TAG, \"TX: to_slave_queue[%u] full for 300 ms, dropping pkt (len=%u) instead of blocking forever\",\n"
        "\t\t\t\tpkt_prio, payload_len);\n"
        "\t\tH_FREE_PTR_WITH_FUNC(free_func, buffer_to_free);\n"
        "\t\treturn ESP_FAIL;\n"
        "\t}\n"
        "\tg_h.funcs->_h_post_semaphore(sem_to_slave_queue);\n")),
    ("waveshare__esp_lcd_hx8394", "esp_lcd_hx8394.c",
     "panel10 under IDF 6: colour order field (build check only)",
     replace, ("panel_dev_config->color_space", "panel_dev_config->rgb_ele_order")),
    ("waveshare__esp32_p4_nano", "CMakeLists.txt",
     "panel10 BSP under IDF 6: requirements (build check only)",
     ensure_requires, ("REQUIRES", ["esp_driver_ledc", "esp_driver_gpio", "esp_driver_i2c", "esp_driver_i2s", "esp_driver_sdmmc"])),
    ("waveshare__esp32_p4_nano", "include/bsp/display.h",
     "panel10 BSP under IDF 6: colour order names (build check only)",
     replace, ("ESP_LCD_COLOR_SPACE_RGB", "LCD_RGB_ELEMENT_ORDER_RGB")),
    ("waveshare__esp32_p4_nano", "include/bsp/display.h",
     "panel10 BSP under IDF 6: colour order names (build check only)",
     replace, ("ESP_LCD_COLOR_SPACE_BGR", "LCD_RGB_ELEMENT_ORDER_BGR"), True),
    ("espressif__m5stack_tab5", "src/bsp_display.c",
     "tab5 BSP: bsp_display_new_with_handles() accepts a NULL config (first boot on a Tab5 crashed with a load access fault)",
     replace, ("        .phy_clk_src = config->dsi_bus.phy_clk_src,\n"
               "        .lane_bit_rate_mbps = config->dsi_bus.lane_bit_rate_mbps,\n",
               "        /* andre68723/BETTA-HA-PANEL fork patch (tools/apply_component_patches.py): main/drivers/display_init_tab5.c passes NULL;\n"
               "         * the BSP's own start path picks the same defaults (965 Mbps on the ST7121 board, 1000 Mbps otherwise). */\n"
               "        .phy_clk_src = config != NULL ? config->dsi_bus.phy_clk_src : 0,\n"
               "        .lane_bit_rate_mbps = config != NULL ? config->dsi_bus.lane_bit_rate_mbps\n"
               "            : (board_version == BSP_TAB5_BOARD_VERSION_ST7121 ? BSP_LCD_ST712X_MIPI_DSI_LANE_BITRATE_MBPS\n"
               "                                                               : BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS),\n")),
    ("espressif__esp_lvgl_port", "src/common/ppa/lcd_ppa.c",
     "esp_lvgl_port: PPA rotation waits at most 1 s for the hardware (a missing done interrupt hung the LVGL task, paneljc 2026-10-05), part 1",
     replace_ppa_legacy, ("#include \"lcd_ppa.h\"\n",
               "#include \"lcd_ppa.h\"\n"
               "#include \"freertos/FreeRTOS.h\"\n"
               "#include \"freertos/semphr.h\"\n"
               "#include \"esp_log.h\"\n"
               "\n"
               "/* andre68723/BETTA-HA-PANEL fork patch (tools/apply_component_patches.py): the blocking PPA call waits with\n"
               " * portMAX_DELAY for a done interrupt. When it never comes the LVGL task, and with it the display lock, hangs for\n"
               " * good. The rotation now runs non-blocking and waits 1 s for the done semaphore; on a timeout the application's\n"
               " * hook (main/util/ui_watchdog_esp.c) writes a core dump and restarts. */\n"
               "__attribute__((weak)) void lvgl_port_ppa_timeout_hook(void) {}\n")),
    ("espressif__esp_lvgl_port", "src/common/ppa/lcd_ppa.c",
     "esp_lvgl_port: bounded PPA wait, part 2 (context)",
     replace, ("    ppa_srm_color_mode_t color_mode;\n};",
               "    ppa_srm_color_mode_t color_mode;\n    SemaphoreHandle_t    done_sem;\n};")),
    ("espressif__esp_lvgl_port", "src/common/ppa/lcd_ppa.c",
     "esp_lvgl_port: bounded PPA wait, part 3 (done callback)",
     replace, ("#define PPA_LCD_ENABLE_CB   0", "#define PPA_LCD_ENABLE_CB   1")),
    ("espressif__esp_lvgl_port", "src/common/ppa/lcd_ppa.c",
     "esp_lvgl_port: bounded PPA wait, part 4 (callback gives the semaphore)",
     replace, ("{\n    return false;\n}\n#endif",
               "{\n    BaseType_t woken = pdFALSE;\n    lvgl_port_ppa_t *ctx = (lvgl_port_ppa_t *)user_data;\n"
               "    if (ctx && ctx->done_sem) xSemaphoreGiveFromISR(ctx->done_sem, &woken);\n    return woken == pdTRUE;\n}\n#endif")),
    ("espressif__esp_lvgl_port", "src/common/ppa/lcd_ppa.c",
     "esp_lvgl_port: bounded PPA wait, part 5 (create the semaphore)",
     replace, ("    ppa_ctx->color_mode = cfg->color_mode;\n",
               "    ppa_ctx->done_sem = xSemaphoreCreateBinary();\n    ESP_GOTO_ON_FALSE(ppa_ctx->done_sem, ESP_ERR_NO_MEM, err, TAG, \"PPA semaphore\");\n"
               "    ppa_ctx->color_mode = cfg->color_mode;\n")),
    ("espressif__esp_lvgl_port", "src/common/ppa/lcd_ppa.c",
     "esp_lvgl_port: bounded PPA wait, part 6 (non-blocking operation, timed wait)",
     replace_ppa_legacy, ("        .mode = rotate_cfg->ppa_mode,\n        .user_data = rotate_cfg->user_data,\n    };\n\n    return ppa_do_scale_rotate_mirror(ppa_ctx->srm_handle, &srm_oper_config);",
               "        .mode = PPA_TRANS_MODE_NON_BLOCKING,\n        .user_data = ppa_ctx,\n    };\n\n"
               "    (void)xSemaphoreTake(ppa_ctx->done_sem, 0); /* drop a stale completion */\n"
               "    esp_err_t err = ppa_do_scale_rotate_mirror(ppa_ctx->srm_handle, &srm_oper_config);\n"
               "    if (err != ESP_OK) return err;\n"
               "    if (xSemaphoreTake(ppa_ctx->done_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {\n"
               "        ESP_LOGE(TAG, \"PPA rotation did not finish within 1 s\");\n"
               "        lvgl_port_ppa_timeout_hook();\n"
               "        return ESP_ERR_TIMEOUT;\n"
               "    }\n    return ESP_OK;")),

    ("espressif__esp_lvgl_port", "src/common/ppa/lcd_ppa.c",
     "esp_lvgl_port: preserve PPA input position for crash evidence",
     replace, ("    const int h = rotate_cfg->area.y2 - rotate_cfg->area.y1 + 1;",
               "    const int h = rotate_cfg->area.y2 - rotate_cfg->area.y1 + 1;\n"
               "    const uint32_t input_x = rotate_cfg->area.x1;\n"
               "    const uint32_t input_y = rotate_cfg->area.y1;")),
    ("espressif__esp_lvgl_port", "src/common/ppa/lcd_ppa.c",
     "esp_lvgl_port: timeout hook receives the failed PPA operation",
     replace, ("__attribute__((weak)) void lvgl_port_ppa_timeout_hook(void) {}",
               "__attribute__((weak)) void lvgl_port_ppa_timeout_hook(uint32_t width, uint32_t height, uint32_t x, uint32_t y,\n"
               "    uint32_t rotation, uintptr_t input, uintptr_t output, uint32_t capacity) {}")),
    ("espressif__esp_lvgl_port", "src/common/ppa/lcd_ppa.c",
     "esp_lvgl_port: pass PPA geometry, rotation and buffer data on timeout",
     replace, ("        lvgl_port_ppa_timeout_hook();",
               "        lvgl_port_ppa_timeout_hook(w, h, input_x, input_y, (uint32_t)rotate_cfg->rotation * 90U,\n"
               "            (uintptr_t)rotate_cfg->in_buff, (uintptr_t)ppa_ctx->buffer, ppa_ctx->buffer_size);")),
]


def main():
    check_only = "--check" in sys.argv[1:]
    problems = 0
    for component, rel, what, fn, args, *optional in PATCHES:
        path = MC / component / rel
        if not path.exists():
            print(f"skip     {what}: {component} is not fetched")
            continue
        state, new_text = fn(path, *args)
        if state == "missing" and optional and optional[0]:
            print(f"n/a      {what} (name not used in this version)")
            continue
        if state == "applied" and not check_only:
            path.write_text(new_text, encoding="utf-8")
        label = {"applied": "would apply" if check_only else "applied", "present": "present", "missing": "MISSING"}[state]
        print(f"{label:9s}{what}")
        if state == "missing" or (state == "applied" and check_only):
            problems += 1
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
