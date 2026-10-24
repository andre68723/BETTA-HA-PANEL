# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 Cpt_Kirk
# Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
cmake_minimum_required(VERSION 3.16)

set(BETTAOS_RELEASE_VERSION "v0.9.1-rc1")
# A test build can report another version without editing this file: BETTA_VERSION=v0.9.1-rc2 tools/build.sh <variant> reconfigure build
if(DEFINED ENV{BETTA_VERSION} AND NOT "$ENV{BETTA_VERSION}" STREQUAL "")
    set(BETTAOS_RELEASE_VERSION "$ENV{BETTA_VERSION}")
endif()

# sdkconfig.h is not yet generated here (top-level CMake runs before the
# application's kconfig). We infer the panel variant from the SDKCONFIG_DEFAULTS
# list so PROJECT_VER / project name reflect the target hardware.
set(_PV_SUFFIX "")
set(_PROJECT_NAME "betta-ha-panel")
set(_PANEL_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.panel4.yml")
foreach(_sdk ${SDKCONFIG_DEFAULTS})
    if(_sdk MATCHES "panel10")
        set(_PV_SUFFIX "-10.1")
        set(_PROJECT_NAME "betta-ha-panel-10.1")
        set(_PANEL_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.panel10.yml")
    elseif(_sdk MATCHES "tab5")
        set(_PV_SUFFIX "-tab5")
        set(_PROJECT_NAME "betta-ha-panel-tab5")
        set(_PANEL_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.tab5.yml")
    elseif(_sdk MATCHES "paneljc\\.v2")
        # Must be checked before the plain "paneljc" branch below: both
        # sdkconfig.defaults.paneljc and sdkconfig.defaults.paneljc.v2 are
        # listed in SDKCONFIG_DEFAULTS for a V2 build (the .v2 overlay is
        # layered on top of the base paneljc one), and ".v2" also matches "paneljc" as a
        # substring, so this branch must win on that later iteration.
        set(_PV_SUFFIX "-jc-v2")
        set(_PROJECT_NAME "betta-ha-panel-jc-v2")
        set(_PANEL_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.paneljc.yml")
    elseif(_sdk MATCHES "paneljc")
        set(_PV_SUFFIX "-jc")
        set(_PROJECT_NAME "betta-ha-panel-jc")
        set(_PANEL_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.paneljc.yml")
    endif()
endforeach()
# Every variant reports the same version; the variant is in the project name
# (and the board name), which is also what the OTA check compares.
set(PROJECT_VER "${BETTAOS_RELEASE_VERSION}")

# The IDF component manager does not support Kconfig-based rules in
# idf_component.yml, so we materialise the variant-specific manifest here,
# BEFORE project() runs. The generated main/idf_component.yml is a build
# artifact and is listed in .gitignore.
configure_file("${_PANEL_MANIFEST}"
               "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.yml"
               COPYONLY)
message(STATUS "betta-ha-panel variant manifest: ${_PANEL_MANIFEST}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# LVGL's compile-time default font is the small built-in unscii 8 (CONFIG_LV_USE_CUSTOM_FONT_DEFAULT): every text of the panel
# is an lv_tiny_ttf font made at run time (ui/text_font.c), which cannot be named in a constant expression. The UI sets the
# manager's meta font as the inherited text font of the screen, so a label without a font of its own is still readable.
# Kconfig cannot carry these C expressions, and LVGL itself needs them, so they are build-wide definitions.
set(_DEFAULT_FONT lv_font_unscii_8)
idf_build_set_property(COMPILE_DEFINITIONS "LV_FONT_DEFAULT=&${_DEFAULT_FONT}" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "LV_FONT_CUSTOM_DECLARE=LV_FONT_DECLARE(${_DEFAULT_FONT})" APPEND)
if(_PV_SUFFIX STREQUAL "-jc" OR _PV_SUFFIX STREQUAL "-jc-v2")
    # V1 and V2 share the same manifest/dependency set - only the display
    # driver's init table differs, gated at compile time via Kconfig.
    idf_build_set_property(DEPENDENCIES_LOCK dependencies.lock.paneljc)
elseif(_PV_SUFFIX STREQUAL "-tab5")
    idf_build_set_property(DEPENDENCIES_LOCK dependencies.lock.tab5)
elseif(_PV_SUFFIX STREQUAL "-10.1")
    idf_build_set_property(DEPENDENCIES_LOCK dependencies.lock.panel10)
endif()
project(${_PROJECT_NAME})

# tools/apply_component_patches.py re-applies the small edits this project needs in components fetched
# into managed_components/ (a manifest change, or a build at a commit whose manifest differs from the
# lock, refetches and wipes them). This used to be a "run it by hand after a refetch" step; it now runs
# on every configure instead, right after project() has resolved this variant's manifest and the
# component manager has fetched accordingly. Idempotent (an edit already in place is left alone) and
# safe on a component this variant never fetches (skipped, not an error) -- see the script's own doc
# comment. A PATCHES entry whose place cannot be found any more (a component version moved it) is a
# real problem worth stopping the build for, not a warning to scroll past.
execute_process(
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/tools/apply_component_patches.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE _patch_result
)
if(NOT _patch_result EQUAL 0)
    message(FATAL_ERROR "tools/apply_component_patches.py reported a problem (see output above): "
                         "a component version likely moved the place of an edit -- adjust the script.")
endif()

# The component manager fetches BSPs inside project(). Their CMake requirements have already been
# expanded by the time source patches run above. Add the IDF 6 split drivers to the actual targets as
# well, so a first build from a clean checkout gets their include paths without a second configure.
if(TARGET __idf_waveshare__esp32_p4_nano)
    target_link_libraries(__idf_waveshare__esp32_p4_nano PUBLIC
        idf::esp_driver_ledc idf::esp_driver_gpio idf::esp_driver_i2c idf::esp_driver_i2s idf::esp_driver_sdmmc)
endif()
if(TARGET __idf_waveshare__esp32_p4_wifi6_touch_lcd_4b)
    target_link_libraries(__idf_waveshare__esp32_p4_wifi6_touch_lcd_4b PUBLIC
        idf::esp_driver_i2s idf::esp_driver_sdmmc)
    target_link_libraries(__idf_waveshare__esp32_p4_wifi6_touch_lcd_4b PRIVATE idf::esp_driver_ledc)
endif()
