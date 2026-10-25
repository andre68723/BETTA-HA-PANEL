// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// Room page import (js/room-import.js): titles, roles and preselection, and the zone packer on every
// canvas size. Run with `node --test tests/webui/` (scripts/check.sh does).
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

const CANVASES = [
  { width: 1280, height: 680 }, // paneljc, panel10
  { width: 720, height: 600 }, // panel4
];

const item = (id, room, extra = {}) => ({ id, name: extra.name || id, room, device_class: extra.dc || "", ...extra });

// A busy room: more of everything than a calm first page should show.
function busyRoom(room) {
  const items = [
    item("sensor.klima_temp", room, { dc: "temperature" }),
    item("sensor.cpu_display_temp", room, { dc: "temperature" }),
    item("sensor.klima_hum", room, { dc: "humidity" }),
    item("binary_sensor.motion", room, { dc: "motion" }),
    item("climate.ac", room),
    item("media_player.tv", room),
    item("media_player.sonos", room),
    item("vacuum.robot", room),
  ];
  for (let i = 1; i <= 5; i++) items.push(item(`binary_sensor.window_${i}`, room, { dc: "window" }));
  for (let i = 1; i <= 7; i++) items.push(item(`light.lamp_${i}`, room));
  items.push(item("light.panel_display_backlight", room));
  for (let i = 1; i <= 5; i++) items.push(item(`cover.blind_${i}`, room));
  items.push(item("fan.ceiling", room));
  return items;
}

// True when two rects overlap or come closer than the packer's 10 px gap.
const tooClose = (a, b, gap = 10) =>
  a.x < b.x + b.w + gap && b.x < a.x + a.w + gap && a.y < b.y + b.h + gap && b.y < a.y + a.h + gap;

test("titles drop the room name, also behind a floor prefix or without umlauts", () => {
  const { fn } = loadEditor();
  const title = fn("roomImportTitle");
  assert.equal(title({ name: "Wohnzimmer Deckenlicht" }, "Wohnzimmer"), "Deckenlicht");
  assert.equal(title({ name: "EG-Wohnzimmer_Fenster_1" }, "Wohnzimmer"), "Fenster 1");
  assert.equal(title({ name: "EG-Kueche_Fenster" }, "Küche"), "Fenster");
  assert.equal(title({ name: "Henri (2)" }, "Henri"), "Henri (2)"); // nothing but a suffix left: keep the name
  assert.equal(title({ name: "Christiane" }, "Christiane"), "Christiane");
  assert.equal(title({ name: "Stehlampe" }, "Christiane"), "Stehlampe");
  assert.equal(title({ name: "Flur Sensor Temperatur" }, "Eingang"), "Flur Sensor Temperatur");
});

test("roles keep only the exact area, cap the preselection and prefer the designated sensor", () => {
  const { fn } = loadEditor();
  const items = [...busyRoom("Bad"), item("light.bath_2", "Badezimmer")];
  const roles = fn("roomImportBuildRoles")(items, "Bad", { temperature_entity_id: "sensor.cpu_display_temp" });
  const byKey = Object.fromEntries(roles.map((group) => [group.role.key, group.entries]));
  // Arrays from the vm context have their own prototype; copy them before a deep comparison.
  const selected = (key) => Array.from(byKey[key].filter((entry) => entry.selected), (entry) => entry.item.id);

  assert.ok(!byKey.light.some((entry) => entry.item.room !== "Bad"), "a substring area must not leak in");
  assert.deepEqual(selected("temperature"), ["sensor.cpu_display_temp"], "the area's designated sensor wins");
  assert.equal(byKey.temperature[0].item.id, "sensor.cpu_display_temp");
  assert.equal(selected("light").length, 4);
  assert.equal(selected("contact").length, 2);
  assert.equal(selected("media").length, 1);
  assert.equal(selected("cover").length, 4);
  const backlight = byKey.light.find((entry) => entry.item.id === "light.panel_display_backlight");
  assert.equal(backlight.selected, false, "a display backlight is listed but never preselected");
  assert.equal(byKey.light.at(-1), backlight, "and sorted to the end");
});

test("without a designated sensor the best scoring one is preselected", () => {
  const { fn } = loadEditor();
  const roles = fn("roomImportBuildRoles")(busyRoom("Bad"), "Bad", null);
  const temperature = roles.find((group) => group.role.key === "temperature").entries;
  assert.equal(temperature[0].item.id, "sensor.klima_temp");
  assert.equal(temperature[0].selected, true);
  assert.equal(temperature[1].selected, false);
});

for (const canvas of CANVASES) {
  test(`packer on ${canvas.width}x${canvas.height}: valid tiles that fit, 10 px apart`, () => {
    const { fn } = loadEditor(canvas);
    const roles = fn("roomImportBuildRoles")(busyRoom("Wohnzimmer"), "Wohnzimmer", null);
    fn("roomImportFitPreselection")(roles);
    const { placed, overflow } = fn("roomImportPack")(fn("roomImportSelectedByZone")(roles), canvas.width, canvas.height);
    const limits = fn("widgetSizeLimits");

    assert.equal(overflow.length, 0, "the preselection always fits");
    for (const { tile } of placed.filter(({ tile }) => tile.role === "contact")) {
      assert.equal(tile.type, "binary_tile", "a window/door contact is a binary tile ...");
      assert.equal(tile.displayMode, "door", "... in the door look");
    }
    assert.ok(placed.length > 0);
    const statusRows = new Set(placed.filter(({ tile }) => tile.statusRow !== undefined).map(({ rect }) => rect.y));
    assert.ok(statusRows.size <= 1, "a fresh page has a single status row");
    for (const { tile, rect } of placed) {
      const min = limits(tile.sizeType || tile.type);
      assert.ok(rect.x >= 0 && rect.y >= 0, `${tile.type} starts on the canvas`);
      assert.ok(rect.x + rect.w <= canvas.width && rect.y + rect.h <= canvas.height, `${tile.type} ends on the canvas`);
      assert.ok(rect.w >= min.minW && rect.h >= min.minH, `${tile.type} ${rect.w}x${rect.h} is at least ${min.minW}x${min.minH}`);
      assert.equal(rect.x % 10 + rect.y % 10 + rect.w % 10 + rect.h % 10, 0, `${tile.type} sits on the 10 px grid`);
    }
    for (let i = 0; i < placed.length; i++) {
      for (let j = i + 1; j < placed.length; j++) {
        assert.ok(!tooClose(placed[i].rect, placed[j].rect),
          `${placed[i].tile.type} and ${placed[j].tile.type} overlap or touch`);
      }
    }
  });
}

test("packer reports what does not fit instead of dropping it", () => {
  const { fn } = loadEditor();
  const roles = fn("roomImportBuildRoles")(busyRoom("Wohnzimmer"), "Wohnzimmer", null);
  for (const group of roles) for (const entry of group.entries) entry.selected = true; // the user ticks everything
  const zones = fn("roomImportSelectedByZone")(roles);
  const total = Object.values(zones).reduce((sum, tiles) => sum + tiles.length, 0);
  const { placed, overflow } = fn("roomImportPack")(zones, 1280, 680);
  assert.ok(overflow.length > 0);
  assert.equal(placed.length + overflow.length, total, "every selected tile is either placed or reported");
});

test("a room with only large tiles lays them side by side", () => {
  const { fn } = loadEditor();
  const roles = fn("roomImportBuildRoles")([item("climate.ac", "Bad"), item("media_player.tv", "Bad")], "Bad", null);
  const { placed } = fn("roomImportPack")(fn("roomImportSelectedByZone")(roles), 1280, 680);
  assert.equal(placed.length, 2);
  assert.equal(placed[0].rect.y, placed[1].rect.y);
});
