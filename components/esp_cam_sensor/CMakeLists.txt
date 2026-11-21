# SPDX-License-Identifier: Apache-2.0
# Modified in 2026 by andre68723 for BETTA-HA-PANEL.
# Espressif's esp_cam_sensor component is reduced to OV02C10, SC202CS, OV5647 and DW9714.
# Original Apache-2.0 license: see LICENSE in this directory.

set(srcs "src/esp_cam_sensor.c" "src/esp_cam_sensor_xclk.c" "src/esp_cam_motor.c" "src/esp_cam_sensor_detect.c" "src/esp_cam_motor_detect.c" "src/esp_cam_ircut_detect.c" "src/esp_cam_ircut.c")

if(CONFIG_CAMERA_SENSOR_MOTOR_DETECT_METHOD_DYNAMIC_LINK)
    set(ldfragments "linker.lf")
endif()

if(CONFIG_CAM_CTRL_SPI_ENABLE)
    list(APPEND srcs "src/driver_cam/esp_cam_ctlr_spi_cam.c")

    if(CONFIG_SPIRAM)
        list(APPEND srcs "src/driver_spi/spi_slave.c")
    endif()
endif()

if(CONFIG_CAM_CTRL_DVP_ENABLE)
    list(APPEND srcs "src/driver_dvp/esp_cam_ctlr_dvp_cam.c")
endif()

set(include_dirs "include")
set(requires "esp_sccb_intf" "esp_driver_cam" "esp_driver_gpio" "esp_driver_ledc" "esp_driver_spi" "esp_driver_parlio" "esp_adc")
set(priv_requires "esp_timer")

if(CONFIG_SOC_ISP_SUPPORTED)
    list(APPEND srcs "src/esp_cam_als_detect.c" "src/esp_cam_als.c" "src/esp_cam_led_detect.c" "src/esp_cam_led.c")
endif()

if(CONFIG_CAMERA_OV02C10)
    list(APPEND srcs "sensors/ov02c10/ov02c10.c")
    list(APPEND include_dirs "sensors/ov02c10/include")
    list(APPEND priv_include_dirs "sensors/ov02c10/private_include")
endif()

if(CONFIG_CAMERA_SC202CS)
    list(APPEND srcs "sensors/sc202cs/sc202cs.c")
    list(APPEND include_dirs "sensors/sc202cs/include")
    list(APPEND priv_include_dirs "sensors/sc202cs/private_include")
endif()

if(CONFIG_CAMERA_OV5647)
    list(APPEND srcs "sensors/ov5647/ov5647.c")
    list(APPEND include_dirs "sensors/ov5647/include")
    list(APPEND priv_include_dirs "sensors/ov5647/private_include")
endif()

if(CONFIG_CAM_MOTOR_DW9714)
    list(APPEND srcs "motors/dw9714/dw9714.c")
    list(APPEND include_dirs "motors/dw9714/include")
    list(APPEND priv_include_dirs "motors/dw9714/private_include")
endif()

if(CONFIG_CAM_IRCUT_AP1511B)
    list(APPEND srcs "ircuts/ap1511b/ap1511b.c")
    list(APPEND include_dirs "ircuts/ap1511b/include")
endif()

if(CONFIG_CAM_ALS_PT1411)
    list(APPEND srcs "als/pt1411/pt1411.c")
    list(APPEND include_dirs "als/pt1411/include")
endif()

if(CONFIG_CAM_LED_IR2835)
    list(APPEND srcs "leds/ir2835/ir2835.c")
    list(APPEND include_dirs "leds/ir2835/include")
endif()

if(CONFIG_CAM_LED_TCJ60)
    list(APPEND srcs "leds/tcj60/tcj60.c")
    list(APPEND include_dirs "leds/tcj60/include")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       PRIV_INCLUDE_DIRS ${priv_include_dirs}
                       REQUIRES ${requires}
                       PRIV_REQUIRES ${priv_requires}
                       LDFRAGMENTS ${ldfragments})

if(CONFIG_CAMERA_SENSOR_MOTOR_DETECT_METHOD_DYNAMIC_LINK)
    if(CONFIG_CAMERA_OV02C10_AUTO_DETECT_MIPI_INTERFACE_SENSOR OR CONFIG_CAMERA_OV02C10_AUTO_DETECT_DVP_INTERFACE_SENSOR)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ov02c10_detect")
    endif()

    if(CONFIG_CAMERA_SC202CS_AUTO_DETECT_MIPI_INTERFACE_SENSOR)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-u sc202cs_detect")
    endif()

    if(CONFIG_CAMERA_OV5647_AUTO_DETECT_MIPI_INTERFACE_SENSOR)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ov5647_detect")
    endif()

    if(CONFIG_CAM_MOTOR_DW9714_AUTO_DETECT)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-u dw9714_detect")
    endif()
endif()

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})
