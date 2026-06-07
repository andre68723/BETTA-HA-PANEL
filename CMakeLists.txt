# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 Cpt_Kirk
cmake_minimum_required(VERSION 3.16)

set(BETTAOS_RELEASE_VERSION "v0.8.2")

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
    elseif(_sdk MATCHES "panels3")
        set(_PV_SUFFIX "-s3")
        set(_PROJECT_NAME "betta-ha-panel-s3")
        set(_PANEL_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.panels3.yml")
    elseif(_sdk MATCHES "paneljc")
        set(_PV_SUFFIX "-jc")
        set(_PROJECT_NAME "betta-ha-panel-jc")
        set(_PANEL_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.paneljc.yml")
    endif()
endforeach()
set(PROJECT_VER "${BETTAOS_RELEASE_VERSION}${_PV_SUFFIX}")

# The IDF component manager does not support Kconfig-based rules in
# idf_component.yml, so we materialise the variant-specific manifest here,
# BEFORE project() runs. The generated main/idf_component.yml is a build
# artifact and is listed in .gitignore.
configure_file("${_PANEL_MANIFEST}"
               "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.yml"
               COPYONLY)
message(STATUS "betta-ha-panel variant manifest: ${_PANEL_MANIFEST}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(_PV_SUFFIX STREQUAL "-jc")
    idf_build_set_property(DEPENDENCIES_LOCK dependencies.lock.paneljc)
endif()
project(${_PROJECT_NAME})
