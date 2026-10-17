// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// Every entry of the settings navigation has a section heading in index.html and a label in both editor languages;
// the design and the top bar are separate pages.
import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadEditor } from "./load_editor.mjs";

const WWW = path.join(path.dirname(fileURLToPath(import.meta.url)), "../../components/webui/www");

test("each settings page has a heading in index.html and a label in en and de", () => {
  const { run } = loadEditor();
  const items = JSON.parse(JSON.stringify(run("SETTINGS_NAV_ITEMS")));
  const html = fs.readFileSync(path.join(WWW, "index.html"), "utf8");
  const ids = items.map((item) => item.sectionId);
  assert.ok(ids.includes("settingsThemeSection") && ids.includes("settingsTopbarSection"), "design and top bar are separate");
  assert.equal(new Set(ids).size, ids.length, "section ids are unique");
  for (const item of items) {
    assert.ok(html.includes(`id="${item.headingId}"`), `${item.headingId} in index.html`);
    for (const lang of ["en", "de"]) {
      assert.ok(run(`WEB_I18N_BUILTIN.${lang}[${JSON.stringify(item.labelKey)}]`), `${item.labelKey} in ${lang}`);
    }
  }
});

test("the top bar heading starts its own section", () => {
  const html = fs.readFileSync(path.join(WWW, "index.html"), "utf8");
  const before = html.slice(0, html.indexOf('id="settingsTopbarHeading"'));
  assert.match(before.slice(before.lastIndexOf("<section")), /^<section>\s*<h2 $/, "a new <section> opens right before the heading");
});

test("the time zone list holds POSIX strings the panel can use, and the new texts exist in both languages", () => {
  const { run } = loadEditor();
  const presets = JSON.parse(JSON.stringify(run("TIMEZONE_PRESETS")));
  assert.equal(new Set(presets.map((p) => p.id)).size, presets.length, "ids are unique");
  assert.equal(new Set(presets.map((p) => p.tz)).size, presets.length, "zones are unique");
  assert.ok(presets.some((p) => p.tz === "CET-1CEST,M3.5.0/2,M10.5.0/3"), "the Central Europe default is in the list");
  for (const preset of presets) assert.match(preset.tz, /^[A-Za-z<>+0-9:.,/-]+$/, preset.id);
  for (const lang of ["en", "de"]) {
    for (const key of ["settings.time.zone", "settings.time.custom", "settings.ota.manual", "settings.alerts.save_note"]) {
      assert.ok(run(`WEB_I18N_BUILTIN.${lang}[${JSON.stringify(key)}]`), `${key} in ${lang}`);
    }
  }
});
