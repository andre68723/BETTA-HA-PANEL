#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Re-vendor components/esp_cam_sensor at another registry version.

The repository carries the OV02C10 driver for the JC8012P4A1 and the SC202CS/SC2356 driver for Tab5.
This takes the core of a registry version, preserves both sensor directories and our project_include.cmake,
and cuts CMakeLists.txt and Kconfig down to those sensors and the DW9714 motor:

    tools/vendor_cam_sensor.py 2.3.0

esp_video pins the matching cam_sensor line (esp_video 2.3.x wants 2.3.*); see docs/development/components.md, "Components with a
vendored copy". Rebuild afterwards (the preserved drivers may need adjusting to the newer core).
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "components", "esp_cam_sensor")
REGISTRY = "https://components.espressif.com/api/components/espressif/esp_cam_sensor"
PRESERVED_SENSORS = ("ov02c10", "sc202cs")
MODIFICATION_NOTICE = (
    "# SPDX-License-Identifier: Apache-2.0\n"
    "# Modified in 2026 by andre68723 for BETTA-HA-PANEL.\n"
    "# Espressif's esp_cam_sensor component is reduced to OV02C10, SC202CS and DW9714.\n"
    "# Original Apache-2.0 license: see LICENSE in this directory.\n\n"
)


def mark_modified(text):
    return text if text.startswith(MODIFICATION_NOTICE) else MODIFICATION_NOTICE + text


def fetch(version, workdir):
    with urllib.request.urlopen(REGISTRY, timeout=30) as r:
        data = json.load(r)
    url = next((v["url"] for v in data["versions"] if v["version"] == version), None)
    if not url:
        sys.exit("esp_cam_sensor %s is not in the registry" % version)
    archive = os.path.join(workdir, "pkg.zip")
    urllib.request.urlretrieve(url, archive)
    out = os.path.join(workdir, "pkg")
    with zipfile.ZipFile(archive) as z:
        z.extractall(out)
    return out


def reduce_cmake(text):
    lines, out, i = text.split("\n"), [], 0
    keep = {"CAMERA_SENSOR_MOTOR_DETECT_METHOD_DYNAMIC_LINK"}
    while i < len(lines):
        m = re.match(r"^if\(CONFIG_(CAMERA_\w+)\)\s*$", lines[i])
        if m and m.group(1) not in keep:  # a sensor block: drop it with the blank lines after it
            while not re.match(r"^endif\(\)\s*$", lines[i]):
                i += 1
            i += 1
            while i < len(lines) and not lines[i].strip():
                i += 1
            continue
        out.append(lines[i])
        i += 1
    text = "\n".join(out)
    block = "".join(
        f'if(CONFIG_CAMERA_{sensor.upper()})\n'
        f'    list(APPEND srcs "sensors/{sensor}/{sensor}.c")\n'
        f'    list(APPEND include_dirs "sensors/{sensor}/include")\n'
        f'    list(APPEND priv_include_dirs "sensors/{sensor}/private_include")\n'
        'endif()\n\n'
        for sensor in PRESERVED_SENSORS
    )
    if "if(CONFIG_CAM_MOTOR_DW9714)" not in text:
        sys.exit("unexpected CMakeLists.txt layout: no DW9714 block to anchor the sensor blocks")
    text = text.replace("if(CONFIG_CAM_MOTOR_DW9714)", block + "if(CONFIG_CAM_MOTOR_DW9714)", 1)
    marker = "if(CONFIG_CAMERA_SENSOR_MOTOR_DETECT_METHOD_DYNAMIC_LINK)\n    if("
    if marker not in text:
        sys.exit("unexpected CMakeLists.txt layout: no detect-link block")
    head = text[: text.index(marker)]
    reduced = head + ('if(CONFIG_CAMERA_SENSOR_MOTOR_DETECT_METHOD_DYNAMIC_LINK)\n'
                   '    if(CONFIG_CAMERA_OV02C10_AUTO_DETECT_MIPI_INTERFACE_SENSOR OR CONFIG_CAMERA_OV02C10_AUTO_DETECT_DVP_INTERFACE_SENSOR)\n'
                   '        target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ov02c10_detect")\n    endif()\n\n'
                   '    if(CONFIG_CAMERA_SC202CS_AUTO_DETECT_MIPI_INTERFACE_SENSOR)\n'
                   '        target_link_libraries(${COMPONENT_LIB} INTERFACE "-u sc202cs_detect")\n    endif()\n\n'
                   '    if(CONFIG_CAM_MOTOR_DW9714_AUTO_DETECT)\n'
                   '        target_link_libraries(${COMPONENT_LIB} INTERFACE "-u dw9714_detect")\n    endif()\nendif()\n\n'
                   'include(package_manager)\ncu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})\n')
    return mark_modified(reduced)


def reduce_kconfig(text):
    text = re.sub(r'^\s*rsource "sensors/[^\"]+"\n', "", text, flags=re.M)
    anchor = 'menu "Select and Set Camera Sensor"\n'
    if anchor not in text:
        sys.exit("unexpected Kconfig layout: no sensor menu")
    sources = "".join(f'            rsource "sensors/{sensor}/Kconfig.{sensor}"\n' for sensor in PRESERVED_SENSORS)
    text = text.replace(anchor, anchor + sources, 1)
    return mark_modified(text)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    version = sys.argv[1]
    with tempfile.TemporaryDirectory(dir=os.path.dirname(DEST)) as work:
        pkg = fetch(version, work)
        stage = os.path.join(work, "staged_component")
        shutil.copytree(pkg, stage, ignore=shutil.ignore_patterns("sensors", "test_apps", "SUPPORT_POLICY*", ".DS_Store", "CHECKSUMS.json"))
        os.makedirs(os.path.join(stage, "sensors"))
        for sensor in PRESERVED_SENSORS:
            shutil.copytree(os.path.join(DEST, "sensors", sensor), os.path.join(stage, "sensors", sensor))
        shutil.copy(os.path.join(DEST, "project_include.cmake"), os.path.join(stage, "project_include.cmake"))
        for name, fn in (("CMakeLists.txt", reduce_cmake), ("Kconfig", reduce_kconfig)):
            path = os.path.join(stage, name)
            with open(path, encoding="utf-8") as f:
                new = fn(f.read())
            with open(path, "w", encoding="utf-8") as f:
                f.write(new)
        old = os.path.join(work, "previous_component")
        os.replace(DEST, old)
        try:
            os.replace(stage, DEST)
        except OSError:
            os.replace(old, DEST)
            raise
    print("components/esp_cam_sensor is now %s core + OV02C10 and SC202CS drivers; update the relevant lock files and rebuild" % version)


if __name__ == "__main__":
    main()
