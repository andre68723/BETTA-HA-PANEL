# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
cmake_minimum_required(VERSION 3.21)
project(betta_widget_simulator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
get_filename_component(REPO_ROOT "${REPO_ROOT}" ABSOLUTE)

set(SIM_VARIANT "panel4" CACHE STRING "Panel geometry: panel4, panel10, paneljc, paneljc-v2, tab5")
set_property(CACHE SIM_VARIANT PROPERTY STRINGS panel4 panel10 paneljc paneljc-v2 tab5)
if(SIM_VARIANT STREQUAL "panel4")
    set(SIM_VARIANT_DEFINE CONFIG_APP_PANEL_VARIANT_4INCH_720=1)
elseif(SIM_VARIANT STREQUAL "panel10")
    set(SIM_VARIANT_DEFINE CONFIG_APP_PANEL_VARIANT_10INCH_1280=1)
elseif(SIM_VARIANT STREQUAL "paneljc" OR SIM_VARIANT STREQUAL "paneljc-v2")
    set(SIM_VARIANT_DEFINE CONFIG_APP_PANEL_VARIANT_10INCH_JC=1)
elseif(SIM_VARIANT STREQUAL "tab5")
    set(SIM_VARIANT_DEFINE CONFIG_APP_PANEL_VARIANT_TAB5_1280=1)
else()
    message(FATAL_ERROR "Unknown SIM_VARIANT: ${SIM_VARIANT}")
endif()

# Prefer the exact LVGL sources resolved by the ESP-IDF component manager.
# A fresh checkout can fetch the corresponding upstream release on first use.
set(SIM_LVGL_DIR "${REPO_ROOT}/managed_components/lvgl__lvgl" CACHE PATH "LVGL 9.6 source tree")
if(NOT EXISTS "${SIM_LVGL_DIR}/CMakeLists.txt")
    include(FetchContent)
    FetchContent_Declare(sim_lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG v9.6.0
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(sim_lvgl)
    if(NOT sim_lvgl_POPULATED)
        FetchContent_Populate(sim_lvgl)
    endif()
    set(SIM_LVGL_DIR "${sim_lvgl_SOURCE_DIR}")
endif()

set(SIM_CJSON_DIR "${REPO_ROOT}/managed_components/espressif__cjson/cJSON" CACHE PATH "cJSON source directory")
if(NOT EXISTS "${SIM_CJSON_DIR}/cJSON.c")
    include(FetchContent)
    FetchContent_Declare(sim_cjson
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
        GIT_TAG v1.7.19
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(sim_cjson)
    if(NOT sim_cjson_POPULATED)
        FetchContent_Populate(sim_cjson)
    endif()
    set(SIM_CJSON_DIR "${sim_cjson_SOURCE_DIR}")
endif()

# The host is an independent CMake project. It does not use esp_lvgl_port,
# ESP-IDF, board display drivers, or a firmware sdkconfig.
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_SDL ON CACHE BOOL "" FORCE)
set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SIM_LVGL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
find_package(SDL2 CONFIG REQUIRED)

add_library(sim_cjson STATIC "${SIM_CJSON_DIR}/cJSON.c")
target_include_directories(sim_cjson PUBLIC "${SIM_CJSON_DIR}")

add_executable(betta-widget-sim
    "${REPO_ROOT}/main/ui/display_geometry.c"
    "${REPO_ROOT}/main/layout/layout_orientation.c"
    simulator.c
    simulator_frame.c
    typography_dump.c
    host_adapters.c
    camera_stream_stub.c
    controls_regression.c
    weather_now_regression.c
    popup_contract_regression.c
    camera_regression.c
    wallbox_regression.c
    bambu_regression.c
    history_regression.c
    energy_regression.c
    font_budget_regression.c
    notification_regression.c
    price_regression.c
    chart_refresh_regression.c
    car_refresh_regression.c
    parqet_regression.c
    pages_stubs.c
    sim_clock.c
    "${REPO_ROOT}/main/build_stamp.c"
    "${REPO_ROOT}/main/net/wifi_signal_level.c"
    "${REPO_ROOT}/main/layout/widget_types.c"
    "${REPO_ROOT}/main/layout/bambu_printer_slots.c"
    "${REPO_ROOT}/main/layout/evcc_slots.c"
    "${REPO_ROOT}/main/ui/widgets/w_empty_tile.c"
    "${REPO_ROOT}/main/ui/widgets/w_sensor.c"
    "${REPO_ROOT}/main/ui/widgets/w_sensor_list.c"
    "${REPO_ROOT}/main/ui/widgets/w_button.c"
    "${REPO_ROOT}/main/ui/widgets/w_slider.c"
    "${REPO_ROOT}/main/ui/widgets/w_gauge.c"
    "${REPO_ROOT}/main/ui/widgets/w_graph.c"
    "${REPO_ROOT}/main/ui/graph_history_feed.c"
    "${REPO_ROOT}/main/util/history_feed_policy.c"
    "${REPO_ROOT}/main/ui/widgets/w_light_tile.c"
    "${REPO_ROOT}/main/ui/widgets/w_heating_tile.c"
    "${REPO_ROOT}/main/ui/widgets/w_cover.c"
    "${REPO_ROOT}/main/ui/widgets/w_fan.c"
    "${REPO_ROOT}/main/ui/widgets/w_contact.c"
    "${REPO_ROOT}/main/ui/widgets/w_scene.c"
    "${REPO_ROOT}/main/ui/widgets/w_select.c"
    "${REPO_ROOT}/main/ui/widgets/w_datetime.c"
    "${REPO_ROOT}/main/ui/ui_history_popup.c"
    "${REPO_ROOT}/main/ui/ui_evcc_sessions_view.c"
    "${REPO_ROOT}/main/ui/ui_days_view.c"
    "${REPO_ROOT}/main/ha/ha_history_reduce.c"
    "${REPO_ROOT}/main/ha/ha_history_stats.c"
    "${REPO_ROOT}/main/ha/ha_price_series.c"
    "${REPO_ROOT}/main/ha/parqet_portfolio.c"
    "${REPO_ROOT}/main/ha/parqet_performance.c"
    "${REPO_ROOT}/tests/simulator/parqet_request_sim.c"
    "${REPO_ROOT}/main/ha/ha_attr_serialize.c"
    "${REPO_ROOT}/main/ui/ui_option_picker.c"
    "${REPO_ROOT}/main/ha/ha_select_options.c"
    "${REPO_ROOT}/main/ha/ha_evcc_value.c"
    "${REPO_ROOT}/main/ha/ha_evcc_vehicle_names.c"
    "${REPO_ROOT}/main/ha/ha_evcc_sessions.c"
    "${REPO_ROOT}/main/ha/ha_control_options.c"
    "${REPO_ROOT}/main/ha/ha_datetime_value.c"
    "${REPO_ROOT}/main/ha/ha_timestamp_format.c"
    "${REPO_ROOT}/main/ha/ha_sensor_value.c"
    "${REPO_ROOT}/main/ui/widgets/w_lock.c"
    "${REPO_ROOT}/main/ui/widgets/w_alarm.c"
    "${REPO_ROOT}/main/ui/ui_pin_pad.c"
    "${REPO_ROOT}/main/ui/widgets/w_energy_timeline.c"
    "${REPO_ROOT}/main/ui/ui_energy_timeline_popup.c"
    "${REPO_ROOT}/main/ha/energy_timeline_model.c"
    "${CMAKE_CURRENT_LIST_DIR}/energy_timeline_stub.c"
    "${REPO_ROOT}/main/ha/ha_lock_state.c"
    "${REPO_ROOT}/main/ha/ha_alarm_state.c"
    "${REPO_ROOT}/main/ui/widgets/w_binary.c"
    "${REPO_ROOT}/main/ui/ui_popup.c"
    "${REPO_ROOT}/main/ui/ui_camera_view.c"
    "${REPO_ROOT}/main/ui/ui_widget_factory.c"
    "${REPO_ROOT}/main/ui/ui_pages.c"
    "${REPO_ROOT}/main/ui/ui_conn_popup.c"
    "${REPO_ROOT}/main/ha/ha_auth_state.c"
    "${REPO_ROOT}/main/ui/nav_layout.c"
    "${REPO_ROOT}/main/ui/ui_house_page.c"
    "${REPO_ROOT}/main/ui/ui_energy_page.c"
    "${REPO_ROOT}/main/ui/ui_notification_popup.c"
    "${REPO_ROOT}/main/ui/ui_boot_splash.c"
    "${REPO_ROOT}/main/ui/assets/SMART86OS_Betta.c"
    "${REPO_ROOT}/main/drivers/chime.c"
    "${REPO_ROOT}/main/util/chime_synth.c"
    "${REPO_ROOT}/main/util/entity_id.c"
    "${REPO_ROOT}/main/util/distance_units.c"
    "${REPO_ROOT}/main/util/iso_time.c"
    "${REPO_ROOT}/main/util/room_extra.c"
    "${REPO_ROOT}/main/util/chart_scale.c"
    "${REPO_ROOT}/main/ui/widgets/w_room.c"
    "${REPO_ROOT}/main/ui/widgets/w_room_controls.c"
    "${REPO_ROOT}/main/ui/widgets/w_climate.c"
    "${REPO_ROOT}/main/ui/widgets/w_sonos_favorites.c"
    "${REPO_ROOT}/main/ui/widgets/w_media_player.c"
    "${REPO_ROOT}/main/ui/widgets/w_camera.c"
    "${REPO_ROOT}/main/ui/widgets/w_vacuum.c"
    "${REPO_ROOT}/main/ui/widgets/w_solar_forecast.c"
    "${REPO_ROOT}/main/ui/widgets/w_energy_price_forecast.c"
    "${REPO_ROOT}/main/ui/widgets/w_parqet_portfolio.c"
    "${REPO_ROOT}/main/ui/widgets/w_stock_quotes.c"
    "${REPO_ROOT}/main/ui/quote_history_feed.c"
    "${REPO_ROOT}/main/ha/ha_quote.c"
    "${REPO_ROOT}/main/ha/ha_quote_series.c"
    "${REPO_ROOT}/main/ui/widgets/w_car_widget.c"
    "${REPO_ROOT}/main/ui/widgets/w_todo.c"
    "${REPO_ROOT}/main/ui/widgets/w_bambu_printer.c"
    "${REPO_ROOT}/main/ui/widgets/w_wallbox_evcc.c"
    "${REPO_ROOT}/main/ui/widgets/bambu_printer_illustration.c"
    "${REPO_ROOT}/main/ui/widgets/bambu_printer_policy.c"
    "${REPO_ROOT}/main/ui/widgets/w_weather_tile.c"
    "${REPO_ROOT}/main/ui/widgets/weather_tile_lottie.c"
    "${REPO_ROOT}/main/ui/widgets/weather_tile_text.c"
    "${REPO_ROOT}/main/ui/widgets/weather_tile_icon.c"
    "${REPO_ROOT}/main/ui/widgets/weather_tile_values.c"
    "${REPO_ROOT}/main/ui/widgets/weather_tile_forecast.c"
    "${REPO_ROOT}/main/ui/widgets/weather_tile_layout.c"
    "${REPO_ROOT}/main/ui/widgets/weather_tile_popup.c"
    "${REPO_ROOT}/main/ui/widgets/weather_tile_render.c"
    "${REPO_ROOT}/main/ui/widgets/weather_value_parse.c"
    "${REPO_ROOT}/main/ui/widgets/weather_condition_icon.c"
    "${REPO_ROOT}/main/ui/widgets/weather_now_view.c"
    "${REPO_ROOT}/main/ui/widgets/weather_hourly.c"
    "${REPO_ROOT}/main/ha/ha_weather_hourly.c"
    "${REPO_ROOT}/main/ui/assets/weather/weather_icons.c"
    "${REPO_ROOT}/main/ui/theme/theme_default.c"
    "${REPO_ROOT}/main/ui/theme/theme_palette.c"
    "${REPO_ROOT}/main/ui/theme/ui_style.c"
    "${REPO_ROOT}/main/ui/theme/ui_style_registry.c"
    "${REPO_ROOT}/main/ui/theme/matte_title.c"
    "${REPO_ROOT}/main/ha/ha_display_precision.c"
    "${REPO_ROOT}/main/ha/ha_light_capabilities.c"
    "${REPO_ROOT}/main/ui/ui_i18n.c"
    "${REPO_ROOT}/main/settings/i18n_store.c"
    "${REPO_ROOT}/main/util/atomic_file.c"
    "${REPO_ROOT}/main/ui/graph_history_store.c"
    "${REPO_ROOT}/main/ui/text_scale.c"
    "${REPO_ROOT}/main/ui/text_font.c"
    "${REPO_ROOT}/main/ui/notification_text.c"
    "${REPO_ROOT}/main/ui/notification_markup.c"
    "${REPO_ROOT}/main/ui/notification_message.c"
    "${REPO_ROOT}/main/ui/notification_emoji.c"
    "${REPO_ROOT}/main/ui/fonts/notification_emoji_data.c"
    "${REPO_ROOT}/main/ui/fonts/mdi_font_registry.c"
    "${REPO_ROOT}/main/ui/fonts/ttf_poppins_data.c"
    "${REPO_ROOT}/main/ui/fonts/ttf_poppins_bold_data.c"
    "${REPO_ROOT}/main/ui/fonts/ttf_mdi_data.c"
    "${REPO_ROOT}/main/ui/fonts/ttf_emoji_data.c")
target_include_directories(betta-widget-sim PRIVATE
    "${REPO_ROOT}/main"
    "${CMAKE_CURRENT_LIST_DIR}/stubs")
target_compile_definitions(betta-widget-sim PRIVATE
    ${SIM_VARIANT_DEFINE}
    APP_HAVE_SMART86OS_BETTA_IMAGE=1
    APP_HAVE_WEATHER_PNG_ICONS=1
    BETTA_SIMULATOR=1
    UI_FONTS_TEST_HOOKS=1
    SIM_WEATHER_ICON_DIR="${REPO_ROOT}/main/ui/assets/weather"
    SIM_DEFAULT_FIXTURE="${CMAKE_CURRENT_LIST_DIR}/demo.json")
target_compile_options(betta-widget-sim PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter
    # time() reads BETTA_SIM_NOW when set (stubs/sim_clock.h)
    -include "${CMAKE_CURRENT_LIST_DIR}/stubs/sim_clock.h")
target_link_libraries(betta-widget-sim PRIVATE lvgl SDL2::SDL2 sim_cjson m)

# the real top bar/navigation compiles with -Werror here; its wide-screen branch leaves a variable unused
set_source_files_properties("${REPO_ROOT}/main/ui/ui_pages.c" PROPERTIES COMPILE_OPTIONS "-Wno-unused-but-set-variable")
