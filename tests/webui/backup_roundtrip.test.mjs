// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// Backup and restore (js/settings.js): what the panel reports in GET /api/settings has to come back through
// settingsForBackup(), and a restored layout has to survive normalizeLayoutWidgets() unchanged. Found by a tester
// whose solar production counter (energy timeline) and, as it turned out, text size and update options were
// missing after a restore.
import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadEditor } from "./load_editor.mjs";

const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), "../..");
const source = fs.readFileSync(path.join(ROOT, "main/api/api_settings.c"), "utf8");

// section.key pairs of the GET /api/settings reply: `cJSON_Add*ToObject(<var>, "key", ...)` and the
// `cJSON_AddItemToObject(root, "section", <var>)` that names the variable's section.
function reportedKeys() {
  const sections = new Map([
    ...[...source.matchAll(/cJSON_AddItemToObject\(root, "(\w+)", (\w+)\)/g)].map(([, name, variable]) => [variable, name]),
    ...[...source.matchAll(/cJSON \*(\w+) = cJSON_AddObjectToObject\(root, "(\w+)"\)/g)].map(([, variable, name]) => [variable, name]),
  ]);
  const keys = new Set();
  for (const [, variable, key] of source.matchAll(/cJSON_Add(?:Bool|String|Number)ToObject\((\w+), "(\w+)"/g)) {
    const section = sections.get(variable);
    if (section) keys.add(`${section}.${key}`);
  }
  return keys;
}

// Reported but deliberately not part of a backup: secrets, live status and values derived from other fields.
const NOT_BACKED_UP = new Set([
  "wifi.password_set", "wifi.configured", "wifi.connected", "wifi.setup_ap_active", "wifi.setup_ap_ssid", "wifi.rssi_dbm",
  "wifi.connected_bssid", "wifi.connected_channel", "wifi.scan_supported",
  "ha.access_token_set", "ha.configured", "ha.connected", "ha.auth",
  "mqtt.password_set", "mqtt.configured", "mqtt.connected", "mqtt.command_topic",
  "device.id", "device.name", // the id and name derive from the room
  "security.password_set", "security.user",
  "hardware.sound_supported", "hardware.rotation_supported", // capabilities of the board, not choices
]);

test("the firmware's settings reply was parsed", () => {
  const keys = reportedKeys();
  assert.ok(keys.size >= 60, `only ${keys.size} keys found in api_settings.c`);
  for (const key of ["ui.text_step", "ota.auto_install", "hardware.sound_volume", "mqtt.broker_host"]) assert.ok(keys.has(key), key);
});

test("every setting the panel reports comes back through the backup", () => {
  const { run } = loadEditor();
  const reported = reportedKeys();
  const settings = {};
  for (const key of reported) {
    const [section, name] = key.split(".");
    (settings[section] ||= {})[name] = key === "ui.text_step" ? 2 : name.includes("_slots") ? ["clock", "date"] : 1;
  }
  const backup = JSON.parse(run(`JSON.stringify(settingsForBackup(${JSON.stringify(settings)}))`));
  const missing = [...reported].filter((key) => {
    if (NOT_BACKED_UP.has(key)) return false;
    const [section, name] = key.split(".");
    return !(backup[section] && name in backup[section]);
  });
  assert.deepEqual(missing, [], `settings the backup drops: ${missing.join(", ")}`);
  assert.equal(backup.ui.text_step, 2);
  assert.equal(backup.ota.auto_install, true);
});

test("a panel without update support gets no ota section back", () => {
  const { run } = loadEditor();
  const backup = JSON.parse(run(`JSON.stringify(settingsForBackup({ ui: { language: "de", style: "flat", text_step: 1 } }))`));
  assert.equal("ota" in backup, false);
  assert.equal(backup.ui.text_step, 1);
});

// ---- every widget key the firmware reads survives a restore ----

// tests/fixtures/backup_layout.json: every optional field of every widget type, a house page, an energy page and an
// alert, valid for the firmware too (tests/host/test_backup_layout.c validates the same file).
const FIXTURE = JSON.parse(fs.readFileSync(path.join(ROOT, "tests/fixtures/backup_layout.json"), "utf8"));
const WIDGET_ENVELOPE = new Set(["id", "type", "title", "rect", "entity_id"]);

// The keys ui_runtime_widget_from_json() and the slot readers take from a widget's JSON.
function firmwareWidgetKeys() {
  const runtime = fs.readFileSync(path.join(ROOT, "main/ui/ui_runtime.c"), "utf8");
  const start = runtime.indexOf("static bool ui_runtime_widget_from_json");
  const body = runtime.slice(start, runtime.indexOf("\n}\n", start));
  const keys = new Set([...body.matchAll(/\((?:widget_json|widget)\s*,\s*"([a-z0-9_]+)"/g)].map((match) => match[1]));
  for (const file of ["main/layout/evcc_slots.c", "main/layout/bambu_printer_slots.c"]) {
    const text = fs.readFileSync(path.join(ROOT, file), "utf8");
    for (const [, key] of text.matchAll(/"((?:evcc_)?[a-z0-9_]+_entity_id|evcc_[a-z0-9_]+)"/g)) keys.add(key);
  }
  return keys;
}

test("the fixture sets every widget key the firmware reads", () => {
  const keys = firmwareWidgetKeys();
  assert.ok(keys.size >= 50, `only ${keys.size} widget keys found in the firmware`);
  const covered = new Set(FIXTURE.pages.flatMap((page) => page.widgets).flatMap((widget) => Object.keys(widget)));
  const missing = [...keys].filter((key) => !WIDGET_ENVELOPE.has(key) && !covered.has(key));
  assert.deepEqual(missing, [], `add these firmware widget keys to tests/fixtures/backup_layout.json: ${missing.join(", ")}`);
});

test("a restore keeps every field of every page, widget and alert in the fixture", () => {
  const { run } = loadEditor();
  const restored = JSON.parse(run(`(function(){ const l = ${JSON.stringify(FIXTURE)}; normalizeLayoutWidgets(l); return JSON.stringify(l); })()`));
  assert.deepEqual(restored.alerts, FIXTURE.alerts);
  const problems = [];
  for (const page of FIXTURE.pages) {
    const out = restored.pages.find((candidate) => candidate.id === page.id);
    if (!out) { problems.push(`page ${page.id} dropped`); continue; }
    for (const key of ["house", "energy"]) {
      if (key in page && JSON.stringify(out[key]) !== JSON.stringify(page[key])) problems.push(`page ${page.id}.${key} changed`);
    }
    for (const original of page.widgets) {
      const widget = out.widgets.find((candidate) => candidate.id === original.id);
      if (!widget) { problems.push(`${original.type}: widget dropped`); continue; }
      for (const [key, value] of Object.entries(original)) {
        if (JSON.stringify(widget[key]) !== JSON.stringify(value)) problems.push(`${original.type}.${key}: ${JSON.stringify(value)} -> ${JSON.stringify(widget[key])}`);
      }
    }
  }
  assert.deepEqual(problems, []);
});
