// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
import test from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

function setup() {
  const { run } = loadEditor();
  run(`
    function node() { return { textContent: '', hidden: false, children: [], handlers: {},
      append(...children) { this.children.push(...children); },
      replaceChildren(...children) { this.children = children; },
      addEventListener(event, handler) { this.handlers[event] = handler; } }; }
    globalThis.nodes = Object.fromEntries(['coredumpList', 'coredumpRow', 'coredumpError',
      'crashBanner', 'crashBannerTitle', 'crashBannerHint'].map(id => [id, node()]));
    document.createElement = node;
    document.getElementById = id => nodes[id] || null;
    globalThis.dismissed = '';
    storageGet = () => dismissed;
    window.confirm = () => true;
    globalThis.a = { id: 'a'.repeat(64), task: 'taskLVGL', pc: '0x4ff0e298', mcause: 2,
      size: 24768, crashed_at: 1791380000, git_commit: '90790a9e-dirty', elf_sha256: '943dc7109',
      cause: 'ppa_timeout', ppa: { width: 352, height: 68, input_x: 15, input_y: 421,
        rotation: 90, wait_ms: 1000, input_buffer: 0x48d8ca80, output_buffer: 0x48e54b00, output_capacity: 409600 } };
    globalThis.b = { ...a, id: 'b'.repeat(64), crashed_at: 0, cause: 'lvgl_stall', ppa: undefined };
    globalThis.reports = [a, b];
    globalThis.requests = [];
    fetch = async (url, options) => { requests.push({ url, options }); return { ok: true }; };
    refreshDeviceStatus = async () => renderCrashBanner({ items: reports });
    renderCrashBanner({ items: reports });
  `);
  return run;
}

test("two reports render stable download IDs, independent times and PPA evidence", async () => {
  const run = setup();
  assert.equal(run("nodes.coredumpList.children.length"), 2);
  assert.equal(run("nodes.coredumpList.children[0].children[2].children[0].href"), `/api/coredump?id=${"a".repeat(64)}`);
  assert.equal(run("nodes.coredumpList.children[1].children[2].children[0].href"), `/api/coredump?id=${"b".repeat(64)}`);
  assert.match(run("nodes.coredumpList.children[0].children[0].textContent"), /PPA rotation timed out/);
  assert.match(run("nodes.coredumpList.children[0].children[3].children[1].textContent"), /352 × 68 @ 15,421.*90°.*1000 ms/);
  assert.match(run("nodes.coredumpList.children[1].children[1].textContent"), /Crash time unknown/);
  run("apiGet = async () => { throw new Error('archived reports must not read a new panic timestamp'); }");
  await run("loadCrashTime()");
  assert.equal(run("editor.crashTimeEpoch"), 0);
});

test("erase click confirms, deletes only its ID, survives refresh, and hides the last report", async () => {
  const run = setup();
  run("window.confirm = () => false");
  await run("nodes.coredumpList.children[1].children[2].children[1].handlers.click()");
  assert.equal(run("requests.length"), 0);
  run("window.confirm = () => true; reports = [a]");
  await run("nodes.coredumpList.children[1].children[2].children[1].handlers.click()");
  assert.equal(run("requests[0].url"), `/api/coredump/erase?id=${"b".repeat(64)}`);
  assert.equal(run("requests[0].options.method"), "POST");
  assert.equal(run("nodes.coredumpList.children.length"), 1);
  run("renderCrashBanner({ items: reports }); reports = []");
  await run("nodes.coredumpList.children[0].children[2].children[1].handlers.click()");
  assert.equal(run("requests[1].url"), `/api/coredump/erase?id=${"a".repeat(64)}`);
  assert.equal(run("nodes.coredumpRow.hidden"), true);
  assert.equal(run("nodes.crashBanner.hidden"), true);
});

test("failed deletion retains both reports and a later crash at the same PC shows the banner", async () => {
  const run = setup();
  run("fetch = async () => ({ ok: false, status: 404 })");
  await run("nodes.coredumpList.children[0].children[2].children[1].handlers.click()");
  assert.equal(run("nodes.coredumpList.children.length"), 2);
  assert.match(run("nodes.coredumpError.textContent"), /Could not delete/);
  assert.equal(run("editor.crashReportBusy"), false);
  run("dismissed = a.id; renderCrashBanner({ items: [a, b] })");
  assert.equal(run("nodes.crashBanner.hidden"), true);
  run("renderCrashBanner({ items: [{ ...a, id: 'c'.repeat(64) }, b] })");
  assert.equal(run("nodes.crashBanner.hidden"), false);
});

test("legacy one-report firmware keeps download and erase actions without an ID", async () => {
  const run = setup();
  run("globalThis.legacy = { present: true, task: 'old', pc: '0x1', mcause_name: 'Illegal instruction' }; renderCrashBanner(legacy)");
  assert.equal(run("nodes.coredumpList.children.length"), 1);
  assert.equal(run("nodes.coredumpList.children[0].children[2].children[0].href"), "/api/coredump");
  run("reports = []");
  await run("nodes.coredumpList.children[0].children[2].children[1].handlers.click()");
  assert.equal(run("requests[0].url"), "/api/coredump/erase");
});
