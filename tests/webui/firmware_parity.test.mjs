// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// The editor's widget limits (js/state.js widgetSizeLimits, js/layout-model.js
// allowedEntityDomainsForWidgetType) against the firmware's table in main/layout/widget_types.c, which
// the panel's layout validator enforces: a size or entity the editor allows but the firmware rejects
// only shows up as a failed save on the panel. Found this way: covers taller than 200 px.
import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadEditor } from "./load_editor.mjs";

const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), "../..");
const table = fs.readFileSync(path.join(ROOT, "main/layout/widget_types.c"), "utf8");
// {"type", {min_w, min_h, max_w, max_h}, "domains"} rows; a value is a number, CBW or CBH.
const ROWS = [...table.matchAll(/\{"(\w+)",\s*\{([^}]*)\},\s*"([^"]*)"\}/g)].map(([, type, limits, domains]) => ({
  type,
  limits: limits.split(/,(?![^(]*\))/).map((value) => value.trim()),
  domains: domains === "*" ? null : domains.split("|"),
}));
// Legacy type names the editor renames on load (normalizeLayoutWidgets), so it never sizes them itself.
const RENAMED_ON_LOAD = new Set(["roborock_tile"]);

function firmwareValue(expr, width, height) {
  if (expr === "CBW") return width;
  if (expr === "CBH") return height;
  return Number(expr);
}

test("the firmware table was found", () => {
  assert.ok(ROWS.length >= 20, `only ${ROWS.length} rows parsed from widget_types.c`);
});

for (const { width, height, name } of [
  { width: 1280, height: 680, name: "10 inch" },
  { width: 720, height: 600, name: "4 inch" },
]) {
  test(`editor size limits match the firmware on the ${name} canvas`, () => {
    const { fn } = loadEditor({ width, height });
    const limits = fn("widgetSizeLimits");
    for (const row of ROWS) {
      if (RENAMED_ON_LOAD.has(row.type)) continue;
      const [minW, minH, maxW, maxH] = row.limits.map((expr) => firmwareValue(expr, width, height));
      const editor = limits(row.type);
      assert.equal(editor.minW, minW, `${row.type} min width`);
      assert.equal(editor.minH, minH, `${row.type} min height`);
      assert.equal(Math.min(editor.maxW, width), Math.min(maxW, width), `${row.type} max width`);
      assert.equal(Math.min(editor.maxH, height), Math.min(maxH, height), `${row.type} max height`);
    }
  });
}

test("the editor only offers entity domains the firmware accepts", () => {
  const { fn } = loadEditor();
  const allowed = fn("allowedEntityDomainsForWidgetType");
  for (const row of ROWS) {
    if (!row.domains || RENAMED_ON_LOAD.has(row.type)) continue;
    for (const domain of Array.from(allowed(row.type))) {
      assert.ok(row.domains.includes(domain), `${row.type}: editor offers ${domain}, firmware accepts ${row.domains.join("|")}`);
    }
  }
});

test("look-specific size limits match the firmware", () => {
  const rows = [...table.matchAll(/\{"(\w+)",\s*"(\w+)",\s*\{([^}]*)\}\}/g)];
  assert.ok(rows.length >= 1, "s_variant_sizes rows parsed from widget_types.c");
  const { fn } = loadEditor();
  for (const [, type, look, limits] of rows) {
    const [minW, minH] = limits.split(",").map((value) => Number(value.trim()));
    const editor = fn("widgetSizeLimits")(fn("widgetSizeType")({ type, style_variant: look }));
    assert.equal(editor.minW, minW, `${type} ${look} min width`);
    assert.equal(editor.minH, minH, `${type} ${look} min height`);
  }
});

test("the linear gauge's LED block limits match the firmware", () => {
  const header = fs.readFileSync(path.join(ROOT, "main/layout/widget_types.h"), "utf8");
  const define = (name) => Number(header.match(new RegExp(`#define ${name} (\\d+)`))[1]);
  const { fn, run } = loadEditor();
  assert.equal(run("GAUGE_SEGMENTS_MIN"), define("GAUGE_SEGMENT_COUNT_MIN"));
  assert.equal(run("GAUGE_SEGMENTS_MAX"), define("GAUGE_SEGMENT_COUNT_MAX"));
  assert.equal(run("GAUGE_SEGMENTS_DEFAULT"), define("GAUGE_SEGMENT_COUNT_DEFAULT"));
  const normalize = fn("normalizeGaugeSegmentCount");
  assert.equal(normalize(""), 0, "empty keeps the default");
  assert.equal(normalize(10), 0, "the default is left out of the layout");
  assert.equal(normalize("20"), 20);
  assert.equal(normalize(99), 30);
  assert.equal(normalize(1), 4);
  assert.equal(normalize("abc"), 0);
});

test("the editor's widgets-per-page limit matches the firmware", () => {
  const config = fs.readFileSync(path.join(ROOT, "main/app_config.h"), "utf8");
  const firmware = Number(config.match(/#define APP_MAX_WIDGETS_PER_PAGE (\d+)/)[1]);
  assert.equal(loadEditor().run("MAX_WIDGETS_PER_PAGE"), firmware);
});

test("name visibility capability matches every firmware widget type", () => {
  const { fn } = loadEditor();
  for (const { type } of ROWS) assert.equal(fn("widgetSupportsName")(type), true, type);
  assert.equal(fn("widgetSupportsName")("unknown_widget"), false);
});

test("the editor's default chime volume equals the firmware default", () => {
  const source = fs.readFileSync(path.join(ROOT, "main/settings/runtime_settings.c"), "utf8");
  const defaults = source.slice(source.indexOf("out->sound_enabled = false;"));
  const firmware = Number(defaults.match(/out->sound_volume = (\d+);/)?.[1]);
  assert.ok(Number.isFinite(firmware), "firmware default not found");
  const { run } = loadEditor();
  assert.equal(run("DEFAULT_SOUND_VOLUME"), firmware);
});
