// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

test("loaded portrait layout keeps its geometry through version refreshes", () => {
  const { run } = loadEditor({ width: 1280, height: 600 });
  run('editor.layout = {orientation:"portrait",canvas_w:720,canvas_h:2000,pages:[]}; applyCanvasGeometry(editor.layout,true)');
  assert.equal(run("CANVAS_WIDTH"), 720);
  assert.equal(run("CANVAS_HEIGHT"), 2000);
  run('applyCanvasGeometry({canvas_w:1280,canvas_h:600})');
  assert.equal(run("CANVAS_WIDTH"), 720);
  assert.equal(run("CANVAS_HEIGHT"), 2000);
  run('editor.layout = {orientation:"landscape",canvas_w:1280,canvas_h:600}; applyCanvasGeometry(editor.layout,true)');
  assert.equal(run("CANVAS_WIDTH"), 1280);
  assert.equal(run("CANVAS_HEIGHT"), 600);
});

test("portrait normalization preserves both positions and bounds card height", () => {
  const { run } = loadEditor({ width: 720, height: 2400 });
  run('editor.layout = {orientation:"portrait",pages:[{widgets:[{type:"sensor",rect:{x:0,y:2000,w:300,h:200},landscape_rect:{x:950,y:0,w:300,h:200},portrait_rect:{x:0,y:2000,w:300,h:200}}]}]}; normalizeLayoutWidgets(editor.layout)');
  assert.equal(run("editor.layout.pages[0].widgets[0].rect.y"), 2000);
  assert.equal(run("editor.layout.pages[0].widgets[0].landscape_rect.x"), 950);
  assert.equal(run('widgetSizeLimits("sensor").maxH'), 1160);
  assert.equal(run('widgetSizeLimits("solar_forecast").maxW'), 720);
  run('editor.layout.orientation = "landscape"');
  assert.equal(run('widgetSizeLimits("sensor").maxH'), 2400);
  assert.equal(run('widgetSizeLimits("solar_forecast").maxW'), 640);
});
