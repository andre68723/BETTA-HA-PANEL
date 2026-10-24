#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork
"""Check that the simulator reads every widget field the firmware reads.

The simulator parses layouts with its own parse_widget_def() (tests/simulator/simulator.c) instead of the firmware's
ui_runtime_widget_from_json() (main/ui/ui_runtime.c). A field only the firmware reads renders differently in the
simulator, and every check built on it misses that option (sensor_format and sensor_unit did: the sensor formats rendered
raw). Exit status 1 names the missing fields.
"""
from pathlib import Path
import re
import sys

REPO = Path(__file__).resolve().parent.parent
FIRMWARE = REPO / "main/ui/ui_runtime.c"
SIMULATOR = REPO / "tests/simulator/simulator.c"


def function_body(source, signature):
    start = source.index(signature)
    depth, i = 0, source.index("{", start)
    for j in range(i, len(source)):
        depth += {"{": 1, "}": -1}.get(source[j], 0)
        if depth == 0:
            return source[i:j + 1]
    raise ValueError(signature)


def fields(text):
    return set(re.findall(r'"([a-z][a-z0-9_]+)"', text))


def main():
    firmware = function_body(FIRMWARE.read_text(encoding="utf-8"), "static bool ui_runtime_widget_from_json(")
    simulator = SIMULATOR.read_text(encoding="utf-8")
    sim_parse = function_body(simulator, "static void parse_widget_def(")
    # Field names the firmware compares against (types, modes) are values, not keys; only keys read from the widget count.
    keys = {name for name in fields(firmware) if re.search(rf'widget_json,\s*"{name}"', firmware)}
    missing = sorted(name for name in keys if f'"{name}"' not in sim_parse)
    if missing:
        print("simulator does not read: " + ", ".join(missing))
        return 1
    print(f"simulator fields: ok ({len(keys)} keys; parse_widget_def has {len(fields(sim_parse))} names)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
