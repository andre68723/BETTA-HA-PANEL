// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// "+ Widget" dialog (js/pickers.js): the widgets offered for a Home Assistant entity.
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

test("each entity domain offers the widgets that can show it, best first", () => {
  const { fn, run } = loadEditor();
  const suggest = fn("entityFirstSuggestions");
  // Arrays from the vm context have their own prototype; copy them before a deep comparison.
  const types = (id, dc, meta) => Array.from(suggest(id, dc, meta), (choice) => choice.type);
  assert.deepEqual(types("light.lamp", "", {capabilities:{dimming:true}}), ["light_tile", "slider"]);
  assert.deepEqual(types("light.relay", "", {capabilities:{dimming:false}}), ["light_tile"]);
  assert.deepEqual(types("image.map"), ["camera_tile"]);
  assert.deepEqual(types("climate.ac"), ["heating_tile", "climate_tile"]);
  assert.deepEqual(types("sensor.power", "power", {unit:"W"}), ["sensor", "graph"]);
  assert.deepEqual(types("sensor.status", "enum"), ["sensor"]);
  assert.deepEqual(types("cover.relay", "", {supported_features:3}), ["cover_tile"]);
  assert.deepEqual(types("cover.position", "", {supported_features:7}), ["cover_tile", "slider"]);
  assert.deepEqual(types("unknown_domain.x"), []);
  const door = suggest("binary_sensor.front", "door");
  assert.equal(door[0].type, "binary_tile");
  assert.equal(door[0].displayMode, "door", "a door or window contact gets the door look first");
  assert.equal(suggest("binary_sensor.motion", "motion")[0].displayMode, undefined);

  // Every offered widget must be a type the layout knows (a size table entry, i.e. not the fallback).
  const known = new Set(run("Object.keys(WIDGET_TYPE_LABEL_KEYS)"));
  for (const id of ["light.a", "switch.a", "sensor.a", "binary_sensor.a", "event.a", "lock.a", "scene.a", "script.a",
    "button.a", "climate.a", "cover.a", "fan.a", "camera.a", "vacuum.a", "media_player.a", "weather.a", "todo.a"]) {
    for (const choice of suggest(id, "")) {
      assert.ok(known.has(choice.type), `${id} offers ${choice.type}, which has no label/size entry`);
      assert.ok(run(`typeof t("wizard.entity.t.${choice.key}") === "string" && !t("wizard.entity.t.${choice.key}").startsWith("wizard.")`),
        `chip label for ${choice.key} is translated`);
    }
  }
});

test("the energy timeline is a known widget with its three counter roles", () => {
  const { fn, run } = loadEditor();
  assert.ok(run('Object.keys(WIDGET_TYPE_LABEL_KEYS).includes("energy_timeline")'), "has an Add-menu label");
  assert.deepEqual(Array.from(fn("allowedEntityDomainsForWidgetType")("energy_timeline")), ["sensor"]);
  const limits = fn("widgetSizeLimits")("energy_timeline");
  assert.equal(limits.minW, 300);
  assert.equal(limits.minH, 200);
  assert.ok(limits.maxW >= 480 && limits.maxH >= 360, "it may take a whole page");
  const second = fn("secondaryEntityConfigForWidgetType")("energy_timeline");
  const third = fn("tertiaryEntityConfigForWidgetType")("energy_timeline");
  assert.ok(second.enabled && second.optional && second.domain === "sensor", "grid export counter");
  assert.ok(third.enabled && third.optional && third.domain === "sensor", "solar production counter");
  for (const key of ["layout.widgets.add_energy_timeline", "layout.inspector.entity_energy_timeline",
    "layout.inspector.secondary_entity_energy_timeline", "layout.inspector.tertiary_entity_energy_timeline",
    "entity_picker.title_energy_timeline"]) {
    assert.ok(run(`typeof t("${key}") === "string" && t("${key}") !== "${key}"`), `${key} is translated`);
  }
});

test("loading a layout keeps the solar counter of an energy timeline", () => {
  const { fn } = loadEditor();
  const layout = { version: 1, pages: [{ id: "p", title: "P", widgets: [{
    id: "e1", type: "energy_timeline", title: "Energy", entity_id: "sensor.in", secondary_entity_id: "sensor.out",
    tertiary_entity_id: "sensor.solar", rect: { x: 0, y: 0, w: 400, h: 300 },
  }] }] };
  fn("normalizeLayoutWidgets")(layout);
  assert.equal(layout.pages[0].widgets[0].tertiary_entity_id, "sensor.solar");
});

test("the room card's extra field is limited to entities the card already has and rewrites cleanly", () => {
  const { fn } = loadEditor();
  const candidates = fn("roomExtraCandidates");
  const apply = fn("applyRoomExtra");
  const normalize = fn("normalizeRoomExtra");
  const list = (mode, primary, extras) => Array.from(candidates(mode, primary, extras));
  assert.deepEqual(list("cover", "sensor.t", ["cover.a", "light.l", "cover.b"]), ["cover.a", "cover.b"]);
  assert.deepEqual(list("humidity", "sensor.t", ["sensor.h", "cover.a"]), ["sensor.t", "sensor.h"], "any sensor when the class is unknown");
  assert.deepEqual(list("media_player", "sensor.t", ["light.l"]), []);
  assert.deepEqual(list("default", "sensor.t", ["cover.a"]), []);
  assert.deepEqual(list("cover", "cover.own", []), ["cover.own"], "the room's own entity counts");

  const widget = {};
  assert.equal(apply(widget, "cover", "cover.a", "sensor.t", ["cover.a"]), false);
  assert.deepEqual({ ...widget }, { room_extra_mode: "cover", room_extra_entity_id: "cover.a" });
  assert.equal(apply(widget, "cover", "cover.gone", "sensor.t", ["cover.a"]), true, "an entity outside the card is dropped");
  assert.equal(widget.room_extra_mode, undefined);
  assert.equal(widget.room_extra_entity_id, undefined);
  assert.equal(apply(widget, "default", "cover.a", "sensor.t", ["cover.a"]), false, "default writes nothing and is not an error");
  assert.deepEqual({ ...widget }, {});
  assert.equal(apply(widget, "lights", "light.l", "sensor.t", ["light.l"]), false, "an unknown mode writes nothing");
  assert.deepEqual({ ...widget }, {});

  const saved = { room_extra_mode: "media_player", room_extra_entity_id: "media_player.tv" };
  assert.equal(normalize(saved, "sensor.t", ["media_player.tv"]), false);
  assert.equal(saved.room_extra_mode, "media_player");
  assert.equal(normalize(saved, "sensor.t", ["light.l"]), true, "the entity left the room: cleared");
  assert.equal(saved.room_extra_mode, undefined);
  assert.equal(normalize({}, "sensor.t", []), false, "an old layout has nothing to clear");
});

test("a new vacuum card gets the battery sensor of the same robot, never a foreign one", () => {
  const { fn, run } = loadEditor();
  run(`editor.entities = [
    { id: "vacuum.robo", name: "Robo", device_class: "" },
    { id: "sensor.garden_battery", name: "Garden battery", device_class: "battery" },
    { id: "sensor.robo_battery_level", name: "Robo battery level", device_class: "battery" },
    { id: "sensor.robo_battery", name: "Robo battery", device_class: "battery" },
    { id: "sensor.robo_filter", name: "Robo filter", device_class: "" },
  ]`);
  const suggest = fn("suggestVacuumBatterySensor");
  assert.equal(suggest("vacuum.robo"), "sensor.robo_battery", "shortest id of the matching sensors");
  assert.equal(suggest("vacuum.other"), "", "no sensor of another device is guessed");
  assert.equal(suggest("light.robo"), "");
  run(`editor.entities = [
    { id: "vacuum.r1", name: "Kitchen robot", device_class: "" },
    { id: "sensor.dock_charge", name: "Kitchen robot battery", device_class: "battery" },
  ]`);
  assert.equal(suggest("vacuum.r1"), "sensor.dock_charge", "falls back to the friendly name");
});
