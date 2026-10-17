// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

test("device memory status clears previous readings when the API omits memory", async () => {
  const { run } = loadEditor();
  run(`
    globalThis.nodes = new Map();
    for (const id of ["settingsDeviceSection", "settingsStatusRam", "settingsStatusPsram",
        "settingsStatusRamBarFill", "settingsStatusPsramBarFill", "settingsStatusRamBarMin"]) {
      nodes.set(id, {textContent:"", style:{}, classList:{contains:()=>false}});
    }
    document.getElementById = id => nodes.get(id) || null;
    renderCrashBanner = () => {};
    globalThis.statusPayload = {memory:{free_heap_internal:321*1024,
      min_free_heap_internal:210*1024, internal_total:560*1024,
      free_psram:11*1024*1024, psram_total:27*1024*1024}};
    globalThis.fetch = async () => ({ok:true, json:async()=>statusPayload});
  `);
  await run("refreshDeviceStatus()");
  assert.match(run('nodes.get("settingsStatusRam").textContent'), /321 KB/);
  assert.match(run('nodes.get("settingsStatusPsram").textContent'), /11264 KB/);
  assert.equal(run('nodes.get("settingsStatusRamBarMin").hidden'), false);
  run("statusPayload = {}");
  await run("refreshDeviceStatus()");
  for (const id of ["settingsStatusRam", "settingsStatusPsram"]) {
    assert.equal(run(`nodes.get("${id}").textContent`), "-");
  }
  assert.equal(run('nodes.get("settingsStatusRamBarFill").style.width'), "0%");
  assert.equal(run('nodes.get("settingsStatusPsramBarFill").style.width'), "0%");
  assert.equal(run('nodes.get("settingsStatusRamBarMin").hidden'), true);
  assert.equal(run("formatKb(undefined)"), "-");
});
