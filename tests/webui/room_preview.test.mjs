// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// The room card preview summarises its entities like room_evaluate() in w_room.c, and drops chips like room_layout():
// on a narrow card presence (then lights) gives way while more than two chips would get less than 100 px each.
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

function setup() {
  const ctx = loadEditor();
  ctx.run(`editor.i18nMap=WEB_I18N_BUILTIN.en;
    editor.entities=[{id:"sensor.t",unit:"°C",device_class:"temperature"},{id:"binary_sensor.d",device_class:"door"},{id:"binary_sensor.m",device_class:"motion"}];
    editor.states.set("sensor.t","21.5"); editor.states.set("light.a","on"); editor.states.set("binary_sensor.d","on"); editor.states.set("binary_sensor.m","off");`);
  return ctx;
}

test("the summary reads temperature, lights, contacts and presence from the states", () => {
  const { fn } = setup();
  const s = fn("roomPreviewSummary")("sensor.t", ["light.a", "binary_sensor.d", "binary_sensor.m"]);
  assert.equal(s.temp, "21.5°C");
  assert.equal(s.lightsOn, 1);
  assert.equal(s.open, 1);
  assert.equal(s.present, 0);
  assert.equal(s.presence, 1);
});

test("a climate entity gives the temperature when no sensor does", () => {
  const { fn, run } = setup();
  run(`editor.stateMeta.set("climate.c",{device_class:"",unit:"",current_temperature:24.5}); editor.states.set("climate.c","off");`);
  assert.equal(fn("roomPreviewSummary")("climate.c", []).temp, "24.5°C");
  assert.equal(fn("roomPreviewSummary")("sensor.none", []).temp, null);
});

test("chips give way on a narrow card, presence first", () => {
  const { fn } = setup();
  const s = fn("roomPreviewSummary")("sensor.t", ["light.a", "binary_sensor.d", "binary_sensor.m"]);
  const count = (html) => (html.match(/<i /g) || []).length;
  assert.equal(count(fn("roomPreviewChips")(s, 226)), 2, "226 px: contact and lights");
  assert.ok(!fn("roomPreviewChips")(s, 226).includes("Empty"), "presence is the one that gives way");
  assert.equal(count(fn("roomPreviewChips")(s, 520)), 3, "520 px: all three");
});
