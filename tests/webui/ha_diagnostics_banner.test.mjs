// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork
import test from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

function setup(stored = {}) {
  const { run } = loadEditor();
  run(`
    function node() { return { textContent: '', hidden: true, children: [], innerHTML: '',
      setAttribute() {}, appendChild(child) { this.children.push(child); } }; }
    for (const key of ['haDiagnosticsBanner', 'haDiagnosticsTitle', 'haDiagnosticsHint',
      'haDiagnosticsList', 'haDiagnosticsDismiss']) el[key] = node();
    document.createElement = node;
    globalThis.stored = ${JSON.stringify(stored)};
    storageGet = key => stored[key] ?? null;
    storageSet = (key, value) => { stored[key] = value; };
    globalThis.missing = ['sensor.meter_in', 'sensor.meter_out'];
    apiGet = async () => ({ missing_entities: missing, missing_total: missing.length });
    bindHaDiagnosticsBanner();
  `);
  return run;
}

test("dismissing the missing-entity banner hides it across reloads until the set changes", async () => {
  const run = setup();
  await run("loadHaDiagnostics()");
  assert.equal(run("el.haDiagnosticsBanner.hidden"), false);
  run("el.haDiagnosticsDismiss.onclick()");
  assert.equal(run("el.haDiagnosticsBanner.hidden"), true);
  await run("loadHaDiagnostics()");
  assert.equal(run("el.haDiagnosticsBanner.hidden"), true);

  const reloaded = setup(run("stored"));
  await reloaded("loadHaDiagnostics()");
  assert.equal(reloaded("el.haDiagnosticsBanner.hidden"), true);
  reloaded("missing.push('sensor.other')");
  await reloaded("loadHaDiagnostics()");
  assert.equal(reloaded("el.haDiagnosticsBanner.hidden"), false);
});
