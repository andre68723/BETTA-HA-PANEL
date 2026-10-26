// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723
import test from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

test("media primary stays accented across playback states while one-shot controls stay neutral", () => {
  const { run, fn } = loadEditor();
  const widget = { type: "media_player", entity_id: "media_player.test", rect: { w: 300, h: 300 } };
  const render = fn("tilePreviewMarkup");
  for (const state of ["playing", "paused", "idle", "off", "unknown", "unavailable"]) {
    run(`editor.states.set("media_player.test", ${JSON.stringify(state)});`);
    const available = !["unknown", "unavailable"].includes(state);
    const html = render(widget);
    assert.match(html, new RegExp(`tp-primary control-${available ? "primary" : "disabled"}`));
    assert.ok(html.includes(state === "playing" ? "\u23F8" : "\u23F5"));
    const stop = render({ ...widget, type: "button", button_mode: "stop" });
    assert.doesNotMatch(stop, /control-active|control-primary/);
  }
});

test("vacuum modes stay neutral and the primary follows cleaning, pause and return", () => {
  const { run, fn } = loadEditor();
  const widget = { type: "vacuum_tile", entity_id: "vacuum.test", tertiary_entity_id: "select.mode", rect: { w: 300, h: 300 } };
  for (const state of ["docked", "cleaning", "paused", "returning", "unavailable"]) {
    run(`editor.states.set("vacuum.test", ${JSON.stringify(state)});`);
    const html = fn("tilePreviewMarkup")(widget);
    const role = ["cleaning", "returning"].includes(state) ? "active" : state === "unavailable" ? "disabled" : "action";
    assert.match(html, new RegExp(`tp-vac-btns"><span class="control-${role}"`));
    assert.doesNotMatch(html, /tp-vac-select[^>]*control-active/);
  }
});

test("wallbox highlights the chosen Off mode independently of charging", () => {
  const { run, fn } = loadEditor();
  run('editor.states.set("select.mode", "off"); editor.states.set("binary_sensor.charging", "on");');
  const html = fn("evccPreviewMarkup")({ entity_id: "select.mode", evcc_charging_entity_id: "binary_sensor.charging" });
  assert.match(html, /evcc-modes"><span class="control-active">Off<\/span>/);
  assert.equal((html.match(/control-active/g) || []).length, 1);
});

test("printer has one reversible primary and an independent destructive abort", () => {
  const { run, fn } = loadEditor();
  run('editor.states.set("button.pause", "unknown"); editor.states.set("button.resume", "unknown"); editor.states.set("button.stop", "unknown");');
  const widget = { entity_id: "sensor.printer", pause_entity_id: "button.pause", resume_entity_id: "button.resume", stop_entity_id: "button.stop" };
  for (const state of ["running", "pause", "idle"]) {
    run(`editor.states.set("sensor.printer", ${JSON.stringify(state)});`);
    const html = fn("bambuPreviewMarkup")(widget);
    const role = state === "running" ? "active" : state === "pause" ? "action" : "disabled";
    assert.match(html, new RegExp(`bambu-preview-buttons">\\s*<span class="control-${role}">${state === "pause" ? "Resume" : "Pause"}`));
    assert.match(html, new RegExp(`control-${state === "idle" ? "disabled" : "danger"}">Abort`));
  }
});

test("car only marks a reported toggle or climate operation active", () => {
  const { run, fn } = loadEditor();
  const active = fn("previewPreconditionActive");
  for (const [id, state, expected] of [["switch.car", "on", true], ["switch.car", "off", false],
    ["switch.car", "unknown", false], ["climate.car", "heat", true], ["climate.car", "off", false],
    ["select.car", "heat", false], ["button.car", "2026-10-24T12:00:00Z", false]]) {
    run(`editor.states.set(${JSON.stringify(id)}, ${JSON.stringify(state)});`);
    assert.equal(active({ precond_entity_id: id }), expected);
  }
});

test("fan distinguishes the Off power control from a retained selected preset", () => {
  const { run, fn } = loadEditor();
  run('editor.states.set("fan.test", "off"); editor.stateMeta.set("fan.test", { percentage: 50, percentage_step: 50, preset_mode: "Auto", preset_modes: ["Auto", "Sleep"] });');
  const html = fn("tilePreviewMarkup")({ type: "fan_tile", entity_id: "fan.test", rect: { w: 300, h: 300 } });
  assert.match(html, /tp-fanbig fan-off/);
  assert.match(html, /tp-fan-power control-action/);
  assert.match(html, /control-active">Auto<\/i>/);
  assert.match(html, /control-action">Sleep<\/i>/);
});

test("unavailable printer command entities cannot appear as active actions", () => {
  const { run, fn } = loadEditor();
  run('editor.states.set("sensor.printer", "running"); editor.states.set("button.pause", "unavailable"); editor.states.set("button.stop", "unavailable");');
  const html = fn("bambuPreviewMarkup")({ entity_id: "sensor.printer", pause_entity_id: "button.pause", stop_entity_id: "button.stop" });
  assert.match(html, /control-disabled">Pause/);
  assert.match(html, /control-disabled">Abort/);
  assert.doesNotMatch(html, /control-active|control-danger/);
});

test("missing bindings never produce an active or destructive control", () => {
  const { fn } = loadEditor();
  for (const type of ["media_player", "vacuum_tile", "fan_tile", "button", "scene_tile", "car_widget",
    "bambu_printer", "cover_tile", "lock_tile", "alarm_tile"]) {
    const render = fn(type === "car_widget" ? "carPreviewMarkup" : type === "bambu_printer" ? "bambuPreviewMarkup" : "tilePreviewMarkup");
    const html = render({ type, entity_id: "sensor.missing", precond_entity_id: "switch.missing",
      pause_entity_id: "button.missing", resume_entity_id: "button.missing", stop_entity_id: "button.missing",
      rect: { w: 320, h: 360 } });
    assert.doesNotMatch(html, /control-active|control-primary|control-danger/, type);
  }
});

test("unknown trigger timestamps are usable but missing triggers are disabled", () => {
  const { run, fn } = loadEditor();
  const widget = { type: "scene_tile", entity_id: "scene.test", rect: { w: 180, h: 90 } };
  assert.match(fn("tilePreviewMarkup")(widget), /tp-icon-circle control-disabled/);
  run('editor.states.set("scene.test", "unknown");');
  assert.match(fn("tilePreviewMarkup")(widget), /tp-icon-circle control-action/);
});

test("unavailable media does not retain selected shuffle or repeat glyphs", () => {
  const { run, fn } = loadEditor();
  run('editor.states.set("media_player.test", "unavailable"); editor.stateMeta.set("media_player.test", {shuffle: true, repeat: "all"});');
  const html = fn("tilePreviewMarkup")({ type: "media_player", entity_id: "media_player.test", rect: { w: 340, h: 360 } });
  assert.doesNotMatch(html, /control-selected-icon|control-active|control-primary/);
  assert.ok((html.match(/control-disabled/g) || []).length >= 5);
});

test("room media and directional cover controls respect supported features and endpoints", () => {
  const { run, fn } = loadEditor();
  const action = fn("previewRoomAction");
  const media = { room_extra_mode: "media_player", room_extra_entity_id: "media_player.room" };
  run('editor.states.set("media_player.room", "playing"); editor.stateMeta.set("media_player.room", {supported_features: 0});');
  assert.equal(action(media).role, "control-disabled");
  run('editor.stateMeta.set("media_player.room", {supported_features: 1});');
  assert.equal(action(media).role, "control-active");
  const cover = { room_extra_mode: "cover", room_extra_entity_id: "cover.room" };
  run('editor.states.set("cover.room", "open"); editor.stateMeta.set("cover.room", {supported_features: 8, current_position: 100});');
  assert.equal(action(cover, "up").role, "control-disabled");
  assert.equal(action(cover, "down").role, "control-action");
  run('editor.states.set("cover.room", "opening"); editor.stateMeta.set("cover.room", {supported_features: 0});');
  assert.equal(action(cover, "up").role, "control-disabled");
});

test("optional Stop controls follow the reported capabilities", () => {
  const { run, fn } = loadEditor();
  for (const [type, id, stop] of [["cover_tile", "cover.test", "\u25A0"], ["vacuum_tile", "vacuum.test", "\u23F9"]]) {
    run(`editor.states.set(${JSON.stringify(id)}, "idle");`);
    for (const mask of [0, 8]) {
      run(`editor.stateMeta.set(${JSON.stringify(id)}, {supported_features:${mask}});`);
      const html = fn("tilePreviewMarkup")({ type, entity_id: id, rect: { w: 300, h: 300 } });
      assert.equal(html.includes(stop), mask === 8);
    }
  }
});

test("day and chart-period selections share the active role", () => {
  const { fn } = loadEditor();
  for (const width of [260, 680]) {
    const price = fn("tilePreviewMarkup")({ type: "energy_price_forecast", price_selector: true, rect: { w: width, h: 300 } });
    assert.equal((price.match(/control-active/g) || []).length, 1);
    assert.equal(price.includes('class="control-action"'), width >= 360);
  }
  const stock = fn("tilePreviewMarkup")({ type: "stock_quotes", stock_view: "chart", stock_period: "1y", rect: { w: 380, h: 330 } });
  assert.equal((stock.match(/control-active/g) || []).length, 1);
  const grid = fn("tilePreviewMarkup")({ type: "stock_quotes", stock_view: "grid", stock_period: "1y", rect: { w: 380, h: 330 } });
  assert.equal((grid.match(/control-active/g) || []).length, 1);
  assert.match(grid, /tp-stock-card/);
});

test("alarm and lock alerts remain distinct from active and selected controls", () => {
  const { run, fn } = loadEditor();
  for (const [type, state] of [["alarm_tile", "pending"], ["alarm_tile", "triggered"], ["lock_tile", "jammed"]]) {
    run(`editor.states.set("sensor.test", ${JSON.stringify(state)});`);
    const html = fn("tilePreviewMarkup")({ type, entity_id: "sensor.test", rect: { w: 280, h: 110 } });
    assert.match(html, /control-alert/);
    assert.doesNotMatch(html, /control-active/);
  }
});
