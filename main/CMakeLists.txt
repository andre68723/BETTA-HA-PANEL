# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 Cpt_Kirk
# Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md

# ESP-IDF 6.0 removed the built-in "json" (cJSON) component in favor of the
# "espressif/cjson" managed component; the registered component name changed
# accordingly. Variants still building on IDF 5.x keep using "json".
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(JSON_COMPONENT_NAME cjson)
else()
    set(JSON_COMPONENT_NAME json)
endif()

# ---- Panel-variant-specific driver sources --------------------------------
# The 4" (smart86) and 10.1" (nano) panels use different BSPs and different
# touch controllers, so their display_init/touch_init implementations live
# in separate source files.  Exactly one pair is compiled per build.
if(CONFIG_APP_PANEL_VARIANT_10INCH_1280)
    set(PANEL_DRIVER_SRCS
        "drivers/display_init_panel10.c"
        "drivers/touch_init_panel10.c"
    )
elseif(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
    set(PANEL_DRIVER_SRCS
        "drivers/display_init_tab5.c"
        "drivers/touch_init_tab5.c"
    )
elseif(CONFIG_APP_PANEL_VARIANT_10INCH_JC)
    set(PANEL_DRIVER_SRCS
        "drivers/display_init_paneljc.c"
        "drivers/touch_init_paneljc.c"
        # GSL3680 (Silead) touch driver — vendored from Espressif's Apache-2.0 ESP32_Display_Panel
        "drivers/gsl3680/esp_lcd_gsl3680.c"
    )
else()
    set(PANEL_DRIVER_SRCS
        "drivers/display_init_panel4.c"
        "drivers/touch_init_panel4.c"
    )
endif()

# Report exactly which managed-component versions the build system actually
# resolved (not just what the manifest requested) via /api/version - useful
# for support/diagnostics, e.g. confirming an ESP-IDF 6.x migration picked up
# the expected esp_lvgl_port/esp_lcd_jd9365 versions. Regenerated on every
# configure from the same dependencies.lock file the root CMakeLists.txt
# already selects for this variant.
if(CONFIG_APP_PANEL_VARIANT_10INCH_JC)
    set(APP_DEPENDENCIES_LOCK "${CMAKE_SOURCE_DIR}/dependencies.lock.paneljc")
elseif(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
    set(APP_DEPENDENCIES_LOCK "${CMAKE_SOURCE_DIR}/dependencies.lock.tab5")
elseif(CONFIG_APP_PANEL_VARIANT_10INCH_1280)
    set(APP_DEPENDENCIES_LOCK "${CMAKE_SOURCE_DIR}/dependencies.lock.panel10")
else()
    set(APP_DEPENDENCIES_LOCK "${CMAKE_SOURCE_DIR}/dependencies.lock")
endif()
set(APP_COMPONENT_VERSIONS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/component_versions.h")

# Embed the exact source commit this build was compiled from, for the same
# support/diagnostics purpose - falls back to "unknown" outside a git
# checkout (e.g. a release tarball with no .git directory). Captured before
# the script call below so it can be written in the SAME pass as the
# component versions - the script's own "skip rewrite if unchanged"
# comparison only works if the whole file is produced by one process in one
# write; writing this separately via a later file(APPEND ...) would make
# the on-disk file never match the script's freshly-computed content again
# after the first configure, defeating that optimization on every rebuild.
execute_process(
    COMMAND git -C "${CMAKE_SOURCE_DIR}" describe --always --dirty --abbrev=8
    OUTPUT_VARIABLE APP_GIT_COMMIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE APP_GIT_DESCRIBE_RESULT
)
if(NOT APP_GIT_DESCRIBE_RESULT EQUAL 0 OR NOT APP_GIT_COMMIT_SHA)
    set(APP_GIT_COMMIT_SHA "unknown")
endif()

idf_build_get_property(APP_GEN_PYTHON PYTHON)
execute_process(
    COMMAND "${APP_GEN_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/generate_component_versions.py"
        --lock "${APP_DEPENDENCIES_LOCK}"
        --out "${APP_COMPONENT_VERSIONS_HEADER}"
        --package "lvgl/lvgl=LVGL"
        --package "espressif/esp_lvgl_port=ESP_LVGL_PORT"
        --package "espressif/esp_lvgl_adapter=ESP_LVGL_ADAPTER"
        --package "espressif/esp_lcd_jd9365=ESP_LCD_JD9365"
        --package "espressif/esp_lcd_touch=ESP_LCD_TOUCH"
        --package "espressif/esp_hosted=ESP_HOSTED"
        --package "espressif/esp_wifi_remote=ESP_WIFI_REMOTE"
        --package "espressif/esp_websocket_client=ESP_WEBSOCKET_CLIENT"
        --package "joltwallet/littlefs=LITTLEFS"
        --package "espressif/mqtt=MQTT"
        --package "espressif/cjson=CJSON"
        --package "espressif/esp_serial_slave_link=ESP_SERIAL_SLAVE_LINK"
        --package "waveshare/esp32_p4_wifi6_touch_lcd_4b=WAVESHARE_BSP"
        --package "espressif/m5stack_tab5=M5STACK_BSP"
        --package "waveshare/esp_lcd_st7703=ESP_LCD_ST7703"
        --package "espressif/esp_lcd_touch_gt911=ESP_LCD_TOUCH_GT911"
        --package "espressif/esp_video=ESP_VIDEO"
        --package "espressif/esp_ipa=ESP_IPA"
        --package "espressif/esp_sccb_intf=ESP_SCCB_INTF"
        --package "esp_cam_sensor=ESP_CAM_SENSOR"
        --define "APP_GIT_COMMIT_SHA=${APP_GIT_COMMIT_SHA}"
    RESULT_VARIABLE APP_GEN_COMPONENT_VERSIONS_RESULT
)
if(NOT APP_GEN_COMPONENT_VERSIONS_RESULT EQUAL 0)
    # main/api/api_version.c references every APP_COMPONENT_VERSION_* macro
    # (and APP_GIT_COMMIT_SHA) unconditionally with no #ifdef guard, so this
    # fallback must define all of them - a bare "#pragma once" here would
    # turn a script failure into a hard compile error instead of the
    # graceful "/api/version omits these fields" degradation this warning
    # describes.
    message(WARNING "generate_component_versions.py failed (${APP_GEN_COMPONENT_VERSIONS_RESULT}); /api/version will report empty component versions")
    file(WRITE "${APP_COMPONENT_VERSIONS_HEADER}"
"#pragma once
#define APP_COMPONENT_VERSION_LVGL \"\"
#define APP_COMPONENT_VERSION_ESP_LVGL_PORT \"\"
#define APP_COMPONENT_VERSION_ESP_LVGL_ADAPTER \"\"
#define APP_COMPONENT_VERSION_ESP_LCD_JD9365 \"\"
#define APP_COMPONENT_VERSION_ESP_LCD_TOUCH \"\"
#define APP_COMPONENT_VERSION_ESP_HOSTED \"\"
#define APP_COMPONENT_VERSION_ESP_WIFI_REMOTE \"\"
#define APP_COMPONENT_VERSION_ESP_WEBSOCKET_CLIENT \"\"
#define APP_COMPONENT_VERSION_LITTLEFS \"\"
#define APP_COMPONENT_VERSION_MQTT \"\"
#define APP_COMPONENT_VERSION_CJSON \"\"
#define APP_COMPONENT_VERSION_ESP_SERIAL_SLAVE_LINK \"\"
#define APP_COMPONENT_VERSION_WAVESHARE_BSP \"\"
#define APP_COMPONENT_VERSION_M5STACK_BSP \"\"
#define APP_COMPONENT_VERSION_ESP_LCD_ST7703 \"\"
#define APP_COMPONENT_VERSION_ESP_LCD_TOUCH_GT911 \"\"
#define APP_COMPONENT_VERSION_ESP_VIDEO \"\"
#define APP_COMPONENT_VERSION_ESP_IPA \"\"
#define APP_COMPONENT_VERSION_ESP_SCCB_INTF \"\"
#define APP_COMPONENT_VERSION_ESP_CAM_SENSOR \"\"
#define APP_GIT_COMMIT_SHA \"unknown\"
")
endif()

# Recompile build_stamp.c on every build so /api/version reports the real build time.
idf_component_register(
    SRCS
        "app_main.c"
        "build_stamp.c"
        "util/log_buffer.c"
        "app_events.c"
        "api/http_server.c"
        "api/http_guard.c"
        "api/api_routes.c"
        "ui/theme/ui_style.c"
        "ui/theme/ui_style_registry.c"
        "ui/theme/matte_title.c"
        "settings/brightness_model.c"
        "settings/settings_restart.c"
        "settings/night_mode.c"
        "settings/device_identity.c"
        "settings/admin_auth.c"
        "api/api_layout.c"
        "api/api_energy.c"
        "api/api_entities.c"
        "api/api_state.c"
        "api/api_settings.c"
        "api/api_i18n.c"
        "api/api_wifi.c"
        "api/api_version.c"
        "api/api_log.c"
        "api/api_coredump.c"
        "api/api_navigate.c"
        "api/api_ui_tap.c"
        "api/api_reboot_log.c"
        "api/api_camera.c"
        "api/api_display_test.c"
        "api/api_factory_reset.c"
        "api/api_restart.c"
        "api/api_screenshot.c"
        "api/api_ota.c"
        "api/ota_transfer.c"
        "api/api_theme.c"
        "api/api_ha_diagnostics.c"
        "api/api_ha_printers.c"
        "api/api_ha_evcc.c"
        "api/api_notifications.c"
        ${PANEL_DRIVER_SRCS}
        "drivers/ambient_camera.c"
        "drivers/display_orientation.c"
        "drivers/orientation_policy.c"
        "ui/display_geometry.c"
        "layout/layout_orientation.c"
        "ha/ha_client.c"
        "ha/ha_attr_serialize.c"
        "ha/ha_layout_entities.c"
        "ha/ha_display_precision.c"
        "ha/ha_printer_lookup.c"
        "ha/ha_evcc_lookup.c"
        "ha/ha_discovery_domains.c"
        "ha/ha_entity_source.c"
        "ha/ha_registry_discovery.c"
        "ha/ha_auth_state.c"
        "ha/ha_select_options.c"
    "ha/ha_evcc_value.c"
    "ha/ha_evcc_vehicle_names.c"
    "ha/ha_evcc_sessions.c"
        "ha/ha_control_options.c"
        "ha/ha_datetime_value.c"
        "ha/ha_timestamp_format.c"
        "ha/ha_sensor_value.c"
        "ha/ha_history.c"
        "ha/ha_history_reduce.c"
        "ha/ha_energy_prefs.c"
        "ha/ha_history_stats.c"
        "ha/ha_price_series.c"
        "ha/parqet_portfolio.c"
        "ha/parqet_performance.c"
        "ha/parqet_request.c"
        "ha/ha_quote.c"
        "ha/ha_quote_series.c"
        "ha/ha_easy_stock.c"
        "ha/ha_history_week.c"
        "ha/ha_lock_state.c"
        "ha/ha_alarm_state.c"
        "ha/ha_room_registry.c"
        "ha/ha_attrs_compact.c"
        "ha/ha_ws.c"
        "ha/ha_model.c"
        "ha/ha_energy_model.c"
        "ha/ha_light_capabilities.c"
        "ha/ha_cover_fetcher.c"
        "ha/ha_camera_stream.c"
        "ha/mjpeg_parser.c"
        "ha/cover_request_lifetime.c"
        "layout/layout_store.c"
        "layout/layout_validate.c"
        "layout/widget_types.c"
        "layout/bambu_printer_slots.c"
        "layout/evcc_slots.c"
        "settings/runtime_settings.c"
            "settings/runtime_secrets.c"
        "settings/i18n_store.c"
        "net/wifi_mgr.c"
        "net/wifi_diag_store.c"
        "net/c6_memory.c"
        "net/c6_memory_state.c"
        "net/time_sync.c"
        "net/mqtt_mgr.c"
        "net/wifi_signal_level.c"
        "net/setup_passphrase.c"
        "net/update_check.c"
        "net/update_check_parse.c"
        "ui/ui_runtime.c"
        "ui/graph_history_store.c"
        "ui/ui_display_protection.c"
        "ui/ui_boot_splash.c"
        "ui/ui_notification_popup.c"
        "ui/ui_popup.c"
        "ui/ui_camera_view.c"
        "ui/ui_ota_progress.c"
        "ui/ui_pages.c"
        "ui/ui_conn_popup.c"
        "ui/nav_layout.c"
        "ui/ui_energy_page.c"
        "ui/ui_house_page.c"
        "ui/ui_i18n.c"
        "ui/ui_widget_factory.c"
        "ui/ui_bindings.c"
        "ui/widgets/w_sensor.c"
        "ui/widgets/w_button.c"
        "ui/widgets/w_slider.c"
        "ui/widgets/w_gauge.c"
        "ui/widgets/w_graph.c"
        "ui/graph_history_feed.c"
        "util/history_feed_policy.c"
        "ui/widgets/w_empty_tile.c"
        "ui/widgets/w_light_tile.c"
        "ui/widgets/w_heating_tile.c"
        "ui/widgets/w_weather_tile.c"
        "ui/widgets/weather_tile_lottie.c"
        "ui/widgets/weather_tile_text.c"
        "ui/widgets/weather_tile_icon.c"
        "ui/widgets/weather_tile_values.c"
        "ui/widgets/weather_tile_forecast.c"
        "ui/widgets/weather_tile_layout.c"
        "ui/widgets/weather_tile_popup.c"
        "ui/widgets/weather_tile_render.c"
        "ui/widgets/weather_value_parse.c"
        "ui/widgets/weather_condition_icon.c"
        "ui/widgets/weather_now_view.c"
        "ui/widgets/w_solar_forecast.c"
        "ui/widgets/w_energy_price_forecast.c"
        "ui/widgets/w_parqet_portfolio.c"
        "ui/widgets/w_stock_quotes.c"
        "ui/quote_history_feed.c"
        "ui/widgets/w_car_widget.c"
        "ui/widgets/w_bambu_printer.c"
        "ui/widgets/w_wallbox_evcc.c"
        "ui/widgets/bambu_printer_illustration.c"
        "ui/widgets/bambu_printer_policy.c"
        "ui/widgets/w_todo.c"
        "ui/widgets/w_media_player.c"
        "ui/widgets/w_sonos_favorites.c"
        "ui/widgets/w_vacuum.c"
        "ui/widgets/w_cover.c"
        "ui/widgets/w_fan.c"
        "ui/widgets/w_camera.c"
        "ui/widgets/w_contact.c"
        "ui/widgets/w_climate.c"
        "ui/widgets/w_room.c"
        "ui/widgets/w_room_controls.c"
        "ui/widgets/w_scene.c"
        "ui/widgets/w_select.c"
        "ui/widgets/w_datetime.c"
        "ui/ui_history_popup.c"
    "ui/ui_evcc_sessions_view.c"
        "ui/ui_days_view.c"
        "ui/widgets/w_lock.c"
        "ui/widgets/w_alarm.c"
        "ui/ui_pin_pad.c"
        "ui/widgets/w_energy_timeline.c"
        "ui/ui_energy_timeline_popup.c"
        "ui/ui_option_picker.c"
        "ui/widgets/w_binary.c"
        "ui/fonts/mdi_font_registry.c"
        "ui/text_font.c"
        "ui/notification_text.c"
        "ui/notification_markup.c"
        "ui/notification_message.c"
        "ui/notification_emoji.c"
        "ui/fonts/notification_emoji_data.c"
        "ui/text_scale.c"
        "ui/theme/theme_default.c"
        "ui/theme/theme_palette.c"
        "ui/theme/theme_store.c"
        "util/atomic_file.c"
        "util/cover_scale.c"
        "util/cover_guard.c"
        "settings/settings_validate.c"
        "util/json_util.c"
        "util/ringbuf.c"
        "util/boot_guard.c"
        "util/boot_guard_esp.c"
        "util/coredump_report.c"
        "util/coredump_store.c"
        "util/coredump_archive.c"
        "util/coredump_diagnostics.c"
        "util/coredump_diagnostics_esp.c"
        "util/entity_id.c"
        "util/distance_units.c"
        "util/iso_time.c"
        "util/room_extra.c"
        "util/ota_health.c"
        "util/auto_update.c"
        "util/sha256_soft.c"
        "util/auto_update_state.c"
        "net/auto_update_task.c"
        "util/user_activity.c"
        "util/reboot_log.c"
        "util/reboot_log_esp.c"
        "util/flash_gate.c"
        "util/transfer_deadline.c"
        "util/net_watchdog.c"
        "util/net_watchdog_esp.c"
        "util/ui_watchdog.c"
        "util/ota_chip_rev.c"
        "util/ui_watchdog_esp.c"
        "util/chart_scale.c"
        "ui/lvgl_alloc.c"
        "util/ota_validate.c"
        "ha/energy_timeline_model.c"
        "ha/ha_energy_timeline.c"
        "util/url_decode.c"
        "util/notification_parse.c"
        "util/chime_synth.c"
        "drivers/chime.c"
        "drivers/display_brightness.c"
        "drivers/display_port.c"
        "drivers/display_rotation.c"
        "drivers/display_policy.c"
        "drivers/touch_track.c"
        "drivers/touch_latch.c"
        "drivers/audio_out.c"
        "api/api_sound.c"
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_BINARY_DIR}"
        "api"
        "drivers"
        "ha"
        "layout"
        "net"
        "ui"
        "ui/theme"
        "util"
    REQUIRES
        nvs_flash
        bootloader_support
        esp_netif
        esp_event
        esp_wifi
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_ledc
        esp_driver_ppa
        esp_lvgl_adapter
        esp_websocket_client
        mqtt
        esp_http_client
        esp_http_server
        esp_rom
        esp_timer
        app_update
        esp_app_format
        esp_lcd
        lvgl
        ${JSON_COMPONENT_NAME}
        littlefs
        webui
        esp_driver_i2s
        esp_driver_jpeg
        espcoredump
)

# ESP-Hosted (P4 + C6 coprocessor) is used by every variant.
idf_component_optional_requires(PRIVATE esp_wifi_remote esp_hosted)
if(CONFIG_APP_FEATURE_AMBIENT_CAMERA)
    idf_component_optional_requires(PRIVATE espressif__esp_video)
endif()

# paneljc has no upstream ESP-BSP package: provide a minimal local
# shim of `bsp/display.h` so files including it (e.g. api_settings.c,
# api_ota.c) compile. Stub implementations live in display_init_paneljc.c.
if(CONFIG_APP_PANEL_VARIANT_10INCH_JC)
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/drivers/paneljc_bsp_shim"
        # Vendored GSL3680 headers (esp_lcd_gsl3680.h, esp_lcd_gsl3680_fw.h)
        "${CMAKE_CURRENT_SOURCE_DIR}/drivers/gsl3680"
    )
endif()

set(APP_HAVE_HOSTED_C6_FW_IMAGE 0)
set(HOSTED_C6_FW_BIN "")
set(HOSTED_C6_FW_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/hosted_c6_fw")
set(HOSTED_C6_FW_BIN_RELEASE_GENERIC "${CMAKE_SOURCE_DIR}/release/network_adapter_esp32c6.bin")
file(MAKE_DIRECTORY "${HOSTED_C6_FW_EMBED_DIR}")

# esp_hosted 3.x relocated/reduced host/esp_hosted_host_fw_ver.h to a legacy
# compat shim (host/compat/include/) whose ESP_HOSTED_VERSION_* macros are a
# hardcoded stale placeholder, not the real component version -- read the
# authoritative version straight from the resolved component's own manifest
# instead (accurate on both the 2.x and 3.x component layouts).
set(HOSTED_C6_HOST_MANIFEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_hosted/idf_component.yml")
set(HOSTED_C6_EXPECTED_VERSION "unknown")
if(EXISTS "${HOSTED_C6_HOST_MANIFEST_FILE}")
    file(READ "${HOSTED_C6_HOST_MANIFEST_FILE}" HOSTED_C6_HOST_MANIFEST_TEXT)
    string(REGEX MATCH "\nversion:[ \t]*\"?([0-9]+\\.[0-9]+\\.[0-9]+)\"?"
        _HOSTED_C6_VER_MATCH "${HOSTED_C6_HOST_MANIFEST_TEXT}")
    if(NOT CMAKE_MATCH_1 STREQUAL "")
        set(HOSTED_C6_EXPECTED_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

if(NOT HOSTED_C6_EXPECTED_VERSION STREQUAL "unknown")
    set(HOSTED_C6_FW_BIN_RELEASE_VERSIONED
        "${CMAKE_SOURCE_DIR}/release/network_adapter_esp32c6_${HOSTED_C6_EXPECTED_VERSION}.bin")
endif()
if(DEFINED HOSTED_C6_FW_BIN_RELEASE_VERSIONED AND EXISTS "${HOSTED_C6_FW_BIN_RELEASE_VERSIONED}")
    set(HOSTED_C6_FW_BIN "${HOSTED_C6_FW_BIN_RELEASE_VERSIONED}")
    message(STATUS "Using release C6 firmware image: ${HOSTED_C6_FW_BIN_RELEASE_VERSIONED}")
elseif(EXISTS "${HOSTED_C6_FW_BIN_RELEASE_GENERIC}")
    set(HOSTED_C6_FW_BIN "${HOSTED_C6_FW_BIN_RELEASE_GENERIC}")
    message(STATUS "Using release C6 firmware image: ${HOSTED_C6_FW_BIN_RELEASE_GENERIC}")
elseif(NOT HOSTED_C6_EXPECTED_VERSION STREQUAL "unknown")
    message(WARNING
        "Missing release C6 firmware image for host version ${HOSTED_C6_EXPECTED_VERSION}. "
        "Expected: ${CMAKE_SOURCE_DIR}/release/network_adapter_esp32c6_${HOSTED_C6_EXPECTED_VERSION}.bin")
endif()

if(HOSTED_C6_FW_BIN)
    set(HOSTED_C6_FW_EMBED "${HOSTED_C6_FW_EMBED_DIR}/hosted_c6_fw.bin")
    configure_file("${HOSTED_C6_FW_BIN}" "${HOSTED_C6_FW_EMBED}" COPYONLY)
    target_add_binary_data(${COMPONENT_LIB} "${HOSTED_C6_FW_EMBED}" BINARY)
    set(APP_HAVE_HOSTED_C6_FW_IMAGE 1)
    message(STATUS "Embedding ESP-Hosted C6 firmware image: ${HOSTED_C6_FW_BIN}")
else()
    message(WARNING
        "No ESP-Hosted C6 firmware image found in release/. "
        "Runtime C6 auto-update will be skipped until a release image is provided.")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_HAVE_HOSTED_C6_FW_IMAGE=${APP_HAVE_HOSTED_C6_FW_IMAGE})
target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_HOSTED_C6_EXPECTED_VERSION="${HOSTED_C6_EXPECTED_VERSION}")

set(SPLASH_HOUSE_IMAGE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/ui/assets/splash_house_image.c")
if(EXISTS "${SPLASH_HOUSE_IMAGE_SOURCE}")
    target_sources(${COMPONENT_LIB} PRIVATE "${SPLASH_HOUSE_IMAGE_SOURCE}")
endif()

set(SPLASH_SMART86_BETTA_SOURCE "${CMAKE_CURRENT_LIST_DIR}/ui/assets/SMART86OS_Betta.c")
set(SPLASH_SMART86_BETTA_PNG "${CMAKE_CURRENT_LIST_DIR}/ui/assets/SMART86OS_Betta.png")
set(SPLASH_SMART86_BETTA_PNG_SOURCE "${CMAKE_CURRENT_LIST_DIR}/ui/assets/SMART86OS_Betta_png.c")
set(SPLASH_SMART86_BETTA_AVAILABLE 0)
if(EXISTS "${SPLASH_SMART86_BETTA_PNG}" AND EXISTS "${SPLASH_SMART86_BETTA_PNG_SOURCE}" AND CONFIG_LV_USE_LODEPNG)
    # Preferred: the logo as an embedded PNG (about 108 KB instead of 246 KB
    # raw RGB565), decoded by LVGL's PNG decoder when drawn.
    file(SIZE "${SPLASH_SMART86_BETTA_PNG}" SPLASH_SMART86_BETTA_PNG_SIZE)
    target_add_binary_data(${COMPONENT_LIB} "${SPLASH_SMART86_BETTA_PNG}" BINARY)
    target_sources(${COMPONENT_LIB} PRIVATE "${SPLASH_SMART86_BETTA_PNG_SOURCE}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SMART86OS_BETTA_PNG_SIZE=${SPLASH_SMART86_BETTA_PNG_SIZE})
    set(SPLASH_SMART86_BETTA_AVAILABLE 1)
elseif(EXISTS "${SPLASH_SMART86_BETTA_SOURCE}")
    target_sources(${COMPONENT_LIB} PRIVATE "${SPLASH_SMART86_BETTA_SOURCE}")
    set(SPLASH_SMART86_BETTA_AVAILABLE 1)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_HAVE_SMART86OS_BETTA_IMAGE=${SPLASH_SMART86_BETTA_AVAILABLE})

# Colored weather icons (Meteocons, MIT) as embedded PNGs for the weather tile's
# column view. Used only if the whole set is present and PNG decoding is on.
set(WEATHER_PNG_DIR "${CMAKE_CURRENT_LIST_DIR}/ui/assets/weather")
set(WEATHER_PNG_IDS clear_day clear_night partly_cloudy_day partly_cloudy_night overcast fog hail thunderstorms thunderstorms_rain rain extreme_rain snow sleet wind extreme)
set(APP_HAVE_WEATHER_PNG_ICONS 0)
if(CONFIG_LV_USE_LODEPNG)
    set(WEATHER_PNG_ALL_PRESENT TRUE)
    foreach(WEATHER_PNG_ID IN LISTS WEATHER_PNG_IDS)
        foreach(WEATHER_PNG_SIZE 64 112)
            if(NOT EXISTS "${WEATHER_PNG_DIR}/wxicon_${WEATHER_PNG_ID}_${WEATHER_PNG_SIZE}.png")
                set(WEATHER_PNG_ALL_PRESENT FALSE)
            endif()
        endforeach()
    endforeach()
    if(WEATHER_PNG_ALL_PRESENT AND EXISTS "${WEATHER_PNG_DIR}/weather_icons.c")
        foreach(WEATHER_PNG_ID IN LISTS WEATHER_PNG_IDS)
            foreach(WEATHER_PNG_SIZE 64 112)
                target_add_binary_data(${COMPONENT_LIB}
                    "${WEATHER_PNG_DIR}/wxicon_${WEATHER_PNG_ID}_${WEATHER_PNG_SIZE}.png" BINARY)
            endforeach()
        endforeach()
        target_sources(${COMPONENT_LIB} PRIVATE "${WEATHER_PNG_DIR}/weather_icons.c")
        set(APP_HAVE_WEATHER_PNG_ICONS 1)
    endif()
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_HAVE_WEATHER_PNG_ICONS=${APP_HAVE_WEATHER_PNG_ICONS})

set(LIGHT_TEMP_BUTTON_IMAGE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/ui/assets/Temp.c")
set(LIGHT_RGB_BUTTON_IMAGE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/ui/assets/RGB.c")
set(LIGHT_RGB_SLIDER_IMAGE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/ui/assets/visiblecolor.c")
set(LIGHT_COLOR_BUTTON_IMAGES_AVAILABLE 0)
set(LIGHT_RGB_SLIDER_IMAGE_AVAILABLE 0)
if(EXISTS "${LIGHT_TEMP_BUTTON_IMAGE_SOURCE}" AND EXISTS "${LIGHT_RGB_BUTTON_IMAGE_SOURCE}")
    target_sources(${COMPONENT_LIB} PRIVATE "${LIGHT_TEMP_BUTTON_IMAGE_SOURCE}" "${LIGHT_RGB_BUTTON_IMAGE_SOURCE}")
    set(LIGHT_COLOR_BUTTON_IMAGES_AVAILABLE 1)
endif()
if(EXISTS "${LIGHT_RGB_SLIDER_IMAGE_SOURCE}")
    target_sources(${COMPONENT_LIB} PRIVATE "${LIGHT_RGB_SLIDER_IMAGE_SOURCE}")
    set(LIGHT_RGB_SLIDER_IMAGE_AVAILABLE 1)
endif()
target_compile_definitions(
    ${COMPONENT_LIB}
    PRIVATE
        APP_HAVE_LIGHT_COLOR_BUTTON_IMAGES=${LIGHT_COLOR_BUTTON_IMAGES_AVAILABLE}
        APP_HAVE_LIGHT_RGB_SLIDER_IMAGE=${LIGHT_RGB_SLIDER_IMAGE_AVAILABLE})

# Every text, symbol, icon and emoji is rendered at run time by lv_tiny_ttf from four committed subsets (const arrays in flash),
# see ui/text_font.c and tools/generate_widget_fonts.py. No bitmap font is compiled any more.
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ui/fonts/ttf_poppins_data.c"
    "${CMAKE_CURRENT_LIST_DIR}/ui/fonts/ttf_poppins_bold_data.c"
    "${CMAKE_CURRENT_LIST_DIR}/ui/fonts/ttf_mdi_data.c"
    "${CMAKE_CURRENT_LIST_DIR}/ui/fonts/ttf_emoji_data.c")

set(APP_UI_WEATHER_LOTTIE_ASSETS 0)
set(APP_UI_BETTA_LOTTIE_ASSET 0)
if(CONFIG_LV_USE_LOTTIE)
    set(APP_UI_WEATHER_LOTTIE_ASSETS 1)
    set(WEATHER_LOTTIE_DIR "${CMAKE_CURRENT_LIST_DIR}/ui/weather_icons/fill/lottie")
    set(WEATHER_LOTTIE_EMBEDS
        "clear-day:clear_day"
        "clear-night:clear_night"
        "cloudy:cloudy"
        "fog:fog"
        "hail:hail"
        "partly-cloudy-day:partly_cloudy_day"
        "partly-cloudy-night:partly_cloudy_night"
        "sleet:sleet"
        "wind:wind"
        "overcast-day:overcast_day"
        "extreme:extreme"
        "extreme-rain:extreme_rain"
        "rain:rain"
        "snow:snow"
        "thunderstorms:thunderstorms"
        "thunderstorms-rain:thunderstorms_rain")

    if(NOT IS_DIRECTORY "${WEATHER_LOTTIE_DIR}")
        # The animated icons are not part of this repository (main/ui/weather_icons/UPSTREAM_LINK.txt):
        # expected, the static PNG weather icons are used instead.
        set(WEATHER_LOTTIE_EMBEDS "")
        set(APP_UI_WEATHER_LOTTIE_ASSETS 0)
        message(STATUS "Weather lottie assets not present, using the static PNG weather icons")
    endif()

    foreach(WEATHER_LOTTIE_PAIR IN LISTS WEATHER_LOTTIE_EMBEDS)
        string(REPLACE ":" ";" WEATHER_LOTTIE_PARTS "${WEATHER_LOTTIE_PAIR}")
        list(GET WEATHER_LOTTIE_PARTS 0 WEATHER_LOTTIE_SRC_STEM)
        list(GET WEATHER_LOTTIE_PARTS 1 WEATHER_LOTTIE_DST_STEM)

        set(WEATHER_LOTTIE_SRC_FILE "${WEATHER_LOTTIE_DIR}/${WEATHER_LOTTIE_SRC_STEM}.json")
        set(WEATHER_LOTTIE_EMBED_FILE "${CMAKE_CURRENT_BINARY_DIR}/weather_${WEATHER_LOTTIE_DST_STEM}.json")
        if(EXISTS "${WEATHER_LOTTIE_SRC_FILE}")
            configure_file("${WEATHER_LOTTIE_SRC_FILE}" "${WEATHER_LOTTIE_EMBED_FILE}" COPYONLY)
            target_add_binary_data(${COMPONENT_LIB} "${WEATHER_LOTTIE_EMBED_FILE}" TEXT)
        else()
            set(APP_UI_WEATHER_LOTTIE_ASSETS 0)
            message(WARNING "Missing weather lottie asset: ${WEATHER_LOTTIE_SRC_FILE}")
        endif()
    endforeach()

    # Easter-egg betta fish lottie (shown after 7 taps on the nav-bar home button); opt-in, see Kconfig.
    if(CONFIG_APP_BETTA_EASTER_EGG)
    set(BETTA_LOTTIE_SRC_FILE "${CMAKE_CURRENT_LIST_DIR}/ui/assets/betta.json")
    set(BETTA_LOTTIE_EMBED_FILE "${CMAKE_CURRENT_BINARY_DIR}/betta.json")
    if(EXISTS "${BETTA_LOTTIE_SRC_FILE}")
        configure_file("${BETTA_LOTTIE_SRC_FILE}" "${BETTA_LOTTIE_EMBED_FILE}" COPYONLY)
        target_add_binary_data(${COMPONENT_LIB} "${BETTA_LOTTIE_EMBED_FILE}" TEXT)
        set(APP_UI_BETTA_LOTTIE_ASSET 1)
    else()
        message(WARNING "Missing betta lottie asset: ${BETTA_LOTTIE_SRC_FILE}")
    endif()
    endif()
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_UI_WEATHER_LOTTIE_ASSETS=${APP_UI_WEATHER_LOTTIE_ASSETS})
target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_UI_BETTA_LOTTIE_ASSET=${APP_UI_BETTA_LOTTIE_ASSET})

# Keep strict warnings in project code, but silence an upstream ThorVG warning in lvgl.
if(TARGET __idf_lvgl__lvgl)
    target_compile_options(__idf_lvgl__lvgl PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-type-limits>)
endif()

# Recompile build_stamp.c on every build so /api/version reports the real build time.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_custom_target(app_touch_build_stamp
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_LIST_DIR}/build_stamp.c")
    add_dependencies(${COMPONENT_LIB} app_touch_build_stamp)
endif()

if(CONFIG_APP_PANEL_VARIANT_4INCH_720 OR CONFIG_APP_PANEL_VARIANT_10INCH_JC)
    idf_component_optional_requires(PRIVATE espressif__esp_codec_dev)
endif()

if(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
    idf_component_optional_requires(PRIVATE espressif__bmi270)
endif()
