// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork
import test from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

test("the 10-inch boards preview with fixed rows, from the firmware flag or the screen size", () => {
  const { run, fn } = loadEditor();
  const adaptive = fn("previewTextAdaptive");
  run("editor.appText = null; editor.appScreenW = 1280; editor.appScreenH = 800;");
  assert.equal(adaptive(), false);
  run("editor.appScreenH = 720;");
  assert.equal(adaptive(), true);
  run("editor.appText = { adaptive: false };");
  assert.equal(adaptive(), false);
});

test("the car preview shows the range in whole kilometres like the panel", () => {
  const { run, fn } = loadEditor();
  run(`editor.entities = [{ id: "sensor.id4_range", unit: "m" }, { id: "sensor.ev_range", unit: "mi" }];
    editor.states.set("sensor.id4_range", "199000"); editor.states.set("sensor.ev_range", "100");
    editor.states.set("sensor.km_range", "291.4");`);
  const range = fn("carPreviewRangeText");
  assert.equal(range("sensor.id4_range"), "199 km");
  assert.equal(range("sensor.ev_range"), "161 km");
  assert.equal(range("sensor.km_range"), "291 km");
  assert.equal(range(""), "-- km");
});

test("the car preview places the ring like car_layout(): the card minus 24 px, less the extras", () => {
  const { run, fn } = loadEditor();
  run("editor.appText = { adaptive: false };");
  const markup = fn("carPreviewMarkup");
  assert.match(markup({ rect: { w: 220, h: 260 }, title: "Car" }), /car-preview-gauge" style="width:196px;top:12px;left:12px"/);
  assert.match(markup({ rect: { w: 260, h: 360 }, lock_entity_id: "lock.car" }), /width:236px/);
  // The status line (25 px) alone, then with the 44 px pre-conditioning button 6 px below it, plus the 12 px margin.
  assert.match(markup({ rect: { w: 400, h: 360 }, lock_entity_id: "lock.car" }), /width:299px/);
  const full = markup({ rect: { w: 400, h: 360 }, lock_entity_id: "lock.car", interior_temp_entity_id: "sensor.t", precond_entity_id: "climate.c" });
  assert.match(full, /width:249px/);
  assert.match(full, /car-x-status"><span class="car-x-ok">[^<]+<\/span><span>21.5°C<\/span><\/div><span class="car-x-btn control-disabled">/);
});

test("overlapping tiles are found, tiles that only touch are not", () => {
  const { fn } = loadEditor();
  const ids = fn("overlappingWidgetIds")([
    { id: "weather", rect: { x: 0, y: 0, w: 630, h: 360 } },
    { id: "car", rect: { x: 500, y: 310, w: 240, h: 370 } },
    { id: "solar", rect: { x: 0, y: 370, w: 280, h: 310 } },
    { id: "beside", rect: { x: 280, y: 370, w: 100, h: 100 } },
  ]);
  assert.deepEqual([...ids].sort(), ["car", "weather"]);
});
