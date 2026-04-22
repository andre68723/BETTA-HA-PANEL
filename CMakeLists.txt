# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 Cpt_Kirk
cmake_minimum_required(VERSION 3.16)

set(BETTAOS_RELEASE_VERSION "v0.8.0")

# sdkconfig.h is not yet generated here (top-level CMake runs before the
# application's kconfig). We infer the panel variant from the SDKCONFIG_DEFAULTS
# list so PROJECT_VER / project name reflect the target hardware.
set(_PV_SUFFIX "")
set(_PROJECT_NAME "betta-ha-panel")
foreach(_sdk ${SDKCONFIG_DEFAULTS})
    if(_sdk MATCHES "panel10")
        set(_PV_SUFFIX "-10.1")
        set(_PROJECT_NAME "betta-ha-panel-10.1")
    endif()
endforeach()
set(PROJECT_VER "${BETTAOS_RELEASE_VERSION}${_PV_SUFFIX}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${_PROJECT_NAME})
