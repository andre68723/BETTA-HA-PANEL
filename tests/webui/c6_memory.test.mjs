// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork
import test from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

function setup() {
  const editor = loadEditor();
  editor.run(`
    var c6Nodes = Object.fromEntries([
      "settingsStatusC6", "settingsStatusC6Dma", "settingsStatusC6Error", "settingsStatusC6Incident",
      "settingsStatusC6Label", "settingsStatusC6Row"
    ].map(id => [id, { textContent: "", hidden: false }]));
    document.getElementById = id => c6Nodes[id] || null;
  `);
  return editor;
}

test("C6 status distinguishes absent, collecting, unsupported and real zero", () => {
  const e = setup();
  const render = e.fn("renderC6MemoryStatus");
  const text = () => e.run("c6Nodes.settingsStatusC6.textContent");
  render({});
  assert.match(text(), /unavailable/i);
  render({ c6_memory: { state: "collecting", available: false } });
  assert.match(text(), /Collecting/);
  render({ c6_memory: { state: "unsupported", available: false } });
  // unsupported hides the whole row (label and values) instead of showing a text
  assert.equal(e.run("c6Nodes.settingsStatusC6Label.hidden"), true);
  assert.equal(e.run("c6Nodes.settingsStatusC6Row.hidden"), true);
  render({ c6_memory: { state: "collecting", available: false } });
  assert.equal(e.run("c6Nodes.settingsStatusC6Label.hidden"), false);
  assert.equal(e.run("c6Nodes.settingsStatusC6Row.hidden"), false);
  render({ c6_memory: { state: "fresh", available: true, free_heap_bytes: 0,
    internal_largest_block_bytes: 0, sample_age_ms: 0 } });
  assert.match(text(), /Free 0 KB, lowest --, largest block 0 KB/);
  assert.match(text(), /0 s old.*Current/);
  assert.equal(e.run("c6Nodes.settingsStatusC6Error.hidden"), true);
});

test("C6 status refresh preserves stale provenance and separates incident values", () => {
  const e = setup();
  const render = e.fn("renderC6MemoryStatus");
  render({ c6_memory: { state: "stale", available: true, free_heap_bytes: 51200,
    min_free_heap_bytes: 20480, internal_largest_block_bytes: 10240,
    dma_free_bytes: 40960, dma_largest_block_bytes: 8192, sample_age_ms: 181000,
    last_error_code: 263 }, wifi_diag: { c6_last_incident: {
      free_heap_bytes: 30720, sample_age_ms: 6000, same_host_boot: false,
    } } });
  assert.match(e.run("c6Nodes.settingsStatusC6.textContent"), /Free 50 KB.*lowest 20 KB.*181 s old.*Outdated/);
  assert.match(e.run("c6Nodes.settingsStatusC6Dma.textContent"), /free 40 KB.*8 KB/);
  assert.match(e.run("c6Nodes.settingsStatusC6Incident.textContent"), /earlier panel boot.*30 KB.*6 s old/);
  assert.equal(e.run("c6Nodes.settingsStatusC6Error.hidden"), false);
  render({ c6_memory: { state: "fresh", available: true, free_heap_bytes: 61440,
    internal_largest_block_bytes: 10240, sample_age_ms: 1000, last_error_code: 0 } });
  assert.match(e.run("c6Nodes.settingsStatusC6.textContent"), /Free 60 KB.*Current/);
  assert.equal(e.run("c6Nodes.settingsStatusC6Error.textContent"), "");
  assert.match(e.run("c6Nodes.settingsStatusC6Incident.textContent"), /No saved/);
  render({ c6_memory: { state: "fresh", available: true }, wifi_diag: { c6_last_incident: {
    free_heap_bytes: 30720, sample_age_ms: 2000, same_host_boot: false, restart_cause: "net_watchdog" } } });
  assert.match(e.run("c6Nodes.settingsStatusC6Incident.textContent"), /30 KB.*network watchdog restarted/);
});

test("existing status refresh renders cached C6 JSON via one API fetch", async () => {
  const e = setup();
  e.run(`
    var statusCalls = 0;
    var c6Pane = { classList: { contains: () => false } };
    var statusLookup = document.getElementById;
    document.getElementById = id => id === "settingsDeviceSection" ? c6Pane : statusLookup(id);
    globalThis.fetch = async () => { statusCalls++; return { ok: true,
      json: async () => ({ c6_memory: {state: "collecting", available: false} }) }; };
  `);
  await e.fn("refreshDeviceStatus")();
  assert.equal(e.run("statusCalls"), 1);
  assert.match(e.run("c6Nodes.settingsStatusC6.textContent"), /Collecting/);
});
