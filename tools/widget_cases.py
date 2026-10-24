#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
"""Shared widget cases, role substitution and isolated synthetic control states."""
import copy
import json
import sys
from pathlib import Path

EVCC = {"entity_id": "evcc_mode", "evcc_loadpoint_id": "evcc_loadpoint", "evcc_vehicle_entity_id": "evcc_vehicle",
        "evcc_target_soc_entity_id": "evcc_target", "evcc_power_entity_id": "evcc_power", "evcc_soc_entity_id": "evcc_soc",
        "evcc_connected_entity_id": "evcc_connected", "evcc_charging_entity_id": "evcc_charging",
        "evcc_disabled_entity_id": "evcc_disabled", "evcc_effective_soc_entity_id": "evcc_effective",
        "evcc_energy_entity_id": "evcc_energy", "evcc_remaining_entity_id": "evcc_remaining"}
EVCC_SITE = {"evcc_pv_power_entity_id": "evcc_pv", "evcc_battery_power_entity_id": "evcc_battery",
             "evcc_grid_power_entity_id": "evcc_grid", "evcc_home_power_entity_id": "evcc_home"}
PORTFOLIO = {"entity_id": "portfolio_value", "secondary_entity_id": "portfolio_gain", "tertiary_entity_id": "portfolio_xirr",
             "dividends_entity_id": "portfolio_dividends"}
MACROS = {"evcc": EVCC, "evcc_site": EVCC_SITE, "portfolio": PORTFOLIO}


def substitute(value, entities):
    if isinstance(value, str) and value.startswith("{") and value.endswith("}"):
        role = value[1:-1]
        if role not in entities:
            sys.exit(f"entity map has no role '{role}'")
        return copy.deepcopy(entities[role])
    if isinstance(value, list):
        return [substitute(item, entities) for item in value]
    if isinstance(value, dict):
        return {key: substitute(item, entities) for key, item in value.items()}
    return value


def expand(case):
    """A case's widget fields, macros such as "evcc": true replaced by their role fields."""
    fields = {}
    for key, value in case.items():
        if key in MACROS and value is True:
            fields.update({field: "{" + role + "}" for field, role in MACROS[key].items()})
        elif key not in ("w", "h", "expect", "sim", "sim_state", "gallery", "bindings"):
            fields[key] = value
    return fields


REPO = Path(__file__).resolve().parent.parent
CASES = REPO / "tools/test_layout_cases.json"


def catalogue():
    return json.loads(CASES.read_text(encoding="utf-8"))


def widget_fields(case, entities):
    """Project actual widget fields, excluding test/gallery metadata and expanding optional entity groups."""
    bindings = substitute(case.get("bindings", {}), entities)
    return dict(bindings, **substitute(expand(case), entities))


CONTROL_STATES = {
    "button": ("on", "off", "unavailable"),
    "light_tile": ("off", "unavailable"),
    "heating_tile": ("off", "unavailable"),
    "media_player": ("paused", "idle", "unavailable"),
    "vacuum_tile": ("docked", "paused", "returning", "charging", "unavailable"),
    "bambu_printer": ("pause", "finish", "offline", "unavailable"),
    "wallbox_evcc": ("off", "now", "unavailable"),
    "fan_tile": ("off", "unavailable"),
    "cover_tile": ("opening", "closing", "unavailable"),
    "climate_tile": ("off", "unavailable"),
    "lock_tile": ("unlocked", "jammed", "unknown", "unavailable"),
    "alarm_tile": ("disarmed", "triggered", "pending", "unknown", "unavailable"),
    "scene_tile": ("unknown", "unavailable"),
    "select_tile": ("unknown", "unavailable"),
    "datetime_tile": ("unavailable",),
}


def state_cases(suites):
    originals = [case for suite in suites.values() for case in suite["cases"] if "type" in case]
    result = []
    for kind, states in CONTROL_STATES.items():
        base = next(case for case in originals if case["type"] == kind)
        for state in states:
            case = {key: copy.deepcopy(value) for key, value in base.items() if key not in ("expect", "sim_state")}
            case.update(title=f"{base['title']}: {state}", sim_state={"entity_id": state})
            result.append(case)
    car = next(case for case in originals if case["type"] == "car_widget" and case.get("precond_entity_id"))
    for state in ("heat", "off", "unavailable"):
        result.append(dict(car, title=f"Vorklimatisierung: {state}", expect=[], sim_state={"precond_entity_id": state}))
    room = next(case for case in originals if case["type"] == "room_tile" and case.get("room_extra_mode") == "media_player")
    for state in ("playing", "paused", "unavailable"):
        result.append(dict(room, title=f"Raum-Musik: {state}", expect=[], sim_state={"room_extra_entity_id": state}))
    vacuum = next(case for case in originals if case["type"] == "vacuum_tile")
    result.append(dict(vacuum, type="roborock_tile", title="Roborock (legacy)", expect=[], sim_state={"entity_id": "docked"}))
    return result


def state_fixture(pages, states, overrides):
    """Clone only overridden bindings, retaining their attributes. Input layouts and states stay intact."""
    pages = copy.deepcopy(pages)
    states = copy.deepcopy(states)
    by_id = {state["entity_id"]: state for state in states}
    for page in pages:
        for widget in page["widgets"]:
            for field, value in overrides.get(widget["id"], {}).items():
                source_id = widget[field]
                clone_id = f"{source_id.split('.')[0]}.betta_state_{widget['id']}_{field}"
                state = copy.deepcopy(by_id.get(source_id, {"attributes": {}}))
                state.update(entity_id=clone_id, state=value)
                states.append(state)
                for key, item in widget.items():
                    if key.endswith("entity_id") and item == source_id:
                        widget[key] = clone_id
                    elif key == "room_entities":
                        widget[key] = [clone_id if entity == source_id else entity for entity in item]
    return pages, states
