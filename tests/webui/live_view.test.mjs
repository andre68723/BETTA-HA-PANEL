// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

// Fake browser services: manual timers, controllable fetch/decode, a Blob URL registry.
function setup({ local = false } = {}) {
  const { run } = loadEditor();
  run(`
    globalThis.log = { fetches: [], revoked: [], created: [], aborted: 0, timers: new Map(), clears: 0, decodes: [] };
    globalThis.pending = [];
    let timerId = 0;
    liveViewEnv.fetch = (url, options) => new Promise((resolve, reject) => {
      log.fetches.push([url, options]);
      pending.push({ resolve, reject, options });
      options.signal.addEventListener("abort", () => reject(Object.assign(new Error("aborted"), { name: "AbortError" })));
    });
    liveViewEnv.createObjectURL = blob => { const url = "blob:" + log.created.length; log.created.push(url); return url; };
    liveViewEnv.revokeObjectURL = url => log.revoked.push(url);
    liveViewEnv.decode = url => new Promise((resolve, reject) => log.decodes.push({ url, resolve, reject }));
    liveViewEnv.setTimeout = (fn, ms) => { log.timers.set(++timerId, { fn, ms }); return timerId; };
    liveViewEnv.clearTimeout = id => { log.clears++; log.timers.delete(id); };
    liveViewEnv.newAbort = () => { const listeners = []; return { signal: { addEventListener: (type, fn) => listeners.push(fn) }, abort() { log.aborted++; listeners.forEach(fn => fn()); } }; };
    liveViewEnv.now = () => new Date(2026, 10, 7, 12, 30, 5);
    document.body = { dataset: ${local ? '{ localUi: "classic" }' : "{}"} };
    editor.layout = { pages: [{ id: "p1", title: "Living" }] };
    editor.selectedPageId = "p1";
    editor.i18nMap = WEB_I18N_BUILTIN.en;
  `);
  return run;
}
const tick = () => new Promise((resolve) => setImmediate(resolve));
const ok = (body = "bmp", type = "image/jpeg") => `({ ok: true, status: 200, blob: async () => ({ type: "${type}", body: "${body}" }) })`;
const fail = (status) => `({ ok: false, status: ${status}, statusText: "x", json: async () => ({ error: "boom" }) })`;

test("labels follow the environment in both languages", () => {
  const run = setup();
  assert.equal(run('t(liveViewKey("button"))'), "Live view");
  run('editor.i18nMap = WEB_I18N_BUILTIN.de');
  assert.equal(run('t(liveViewKey("button"))'), "Livebild");
  assert.match(run('t(liveViewKey("note"))'), /Momentaufnahme des Geräts\. Mit Aktualisieren neu laden\. Ungespeicherte Editoränderungen sind nicht enthalten\./);
  const local = setup({ local: true });
  assert.equal(local('t(liveViewKey("button"))'), "Preview");
  local('editor.i18nMap = WEB_I18N_BUILTIN.de');
  assert.equal(local('t(liveViewKey("button"))'), "Vorschau");
});

test("device: one fetch per load, no second request while busy, success stores the image and enables download", async () => {
  const run = setup();
  run("liveViewLoad(); liveViewLoad(); liveViewLoad()");
  assert.equal(run("log.fetches.length"), 1);
  assert.equal(run("log.fetches[0][0]"), "/api/screenshot.jpg");
  assert.equal(run("log.timers.size"), 1);
  assert.equal(run("[...log.timers.values()][0].ms"), 30000);
  run(`pending[0].resolve(${ok()})`);
  await tick();
  run("log.decodes[0].resolve()");
  await tick();
  assert.equal(run("liveView.load"), null);
  assert.equal(run("liveView.current.url"), "blob:0");
  assert.equal(run("liveView.current.name"), "betta-panel-20261107-123005.jpg");
  assert.equal(run("log.timers.size"), 0);
  run("liveViewLoad()"); // manual refresh is the only way to a second request
  assert.equal(run("log.fetches.length"), 2);
});

test("device: timeout during body or decode aborts, reports, keeps the previous image and allows a manual retry", async () => {
  const run = setup();
  run("liveView.current = { blob: 'old', url: 'blob:old', time: new Date(), name: 'o.jpg' }");
  run("liveViewLoad()");
  run(`pending[0].resolve(${ok()})`);
  await tick(); // body received, decode never completes
  run("[...log.timers.values()][0].fn()");
  assert.equal(run("liveView.load"), null);
  assert.equal(run("log.aborted"), 1);
  assert.equal(run("liveView.previous"), true);
  assert.equal(run("liveView.current.url"), "blob:old");
  // The late decode of the timed-out load neither replaces the image nor leaks its Blob URL.
  run("log.decodes[0].resolve()");
  await tick();
  assert.equal(run("liveView.current.url"), "blob:old");
  assert.equal(run("JSON.stringify(log.revoked)"), JSON.stringify(["blob:0"]));
  run("liveViewLoad()");
  assert.equal(run("log.fetches.length"), 2);
});

test("device: HTTP failures map to distinct messages and never retry on their own", async () => {
  const run = setup();
  for (const [status, text] of [[503, /busy/], [401, /Not authorized/], [501, /cannot provide/], [500, /status 500/]]) {
    run("liveViewLoad()");
    run(`pending.shift().resolve(${fail(status)})`);
    await tick();
    assert.equal(run("liveView.load"), null);
    assert.equal(run("log.fetches.length"), 1 + [503, 401, 501, 500].indexOf(status));
    assert.match(run("liveViewFailure(Object.assign(new Error('x'), { liveStatus: " + status + " }), { timedOut: false })"), text);
  }
  run("liveViewLoad()");
  run("pending.shift().reject(new TypeError('fail'))");
  await tick();
  assert.equal(run("liveView.load"), null);
  assert.equal(run("log.timers.size"), 0);
});

test("device: a decode failure keeps the previous image and is marked", async () => {
  const run = setup();
  run("liveView.current = { blob: 'old', url: 'blob:old', time: new Date(), name: 'o.jpg' }");
  run("liveViewLoad()");
  run(`pending[0].resolve(${ok()})`);
  await tick();
  run("log.decodes[0].reject(new Error('decode'))");
  await tick();
  assert.equal(run("liveView.current.url"), "blob:old");
  assert.equal(run("liveView.previous"), true);
  assert.equal(run("JSON.stringify(log.revoked)"), JSON.stringify(["blob:0"]));
});

test("close cancels the request; a reopened load is not disturbed by late cleanup of the old one", async () => {
  const run = setup();
  run("liveViewLoad()");
  run("liveViewCancel()");
  assert.equal(run("log.aborted"), 1);
  assert.equal(run("liveView.load"), null);
  run("liveViewLoad()"); // reopened: new load
  assert.equal(run("log.fetches.length"), 2);
  const timersBefore = run("log.timers.size");
  await tick(); // the aborted first fetch rejects now
  assert.equal(run("liveView.load !== null"), true); // the newer load is still in flight
  assert.equal(run("log.timers.size"), timersBefore);
  run(`pending[1].resolve(${ok()})`);
  await tick();
  run("log.decodes[0].resolve()");
  await tick();
  assert.equal(run("liveView.current.url"), "blob:0");
  assert.equal(run("JSON.stringify(log.revoked)"), JSON.stringify([]));
});

test("a response arriving after close is discarded without a Blob URL leak", async () => {
  const run = setup();
  run("liveViewLoad()");
  run(`pending[0].resolve(${ok()})`);
  await tick();
  run("liveViewCancel()");
  run("log.decodes[0].resolve()");
  await tick();
  assert.equal(run("liveView.current"), null);
  assert.equal(run("JSON.stringify(log.revoked)"), JSON.stringify(["blob:0"]));
});

test("close releases the shown image and its Blob URL; the next open starts clean", async () => {
  const run = setup();
  run("liveView.current = { blob: 'b', url: 'blob:shown', time: new Date(), name: 'x.jpg' }; liveView.previous = true");
  run("liveViewClose()");
  assert.equal(run("liveView.current"), null);
  assert.equal(run("liveView.previous"), false);
  assert.equal(run("JSON.stringify(log.revoked)"), JSON.stringify(["blob:shown"]));
  run("liveViewLoad()"); // reopened: a fresh request, nothing carried over
  assert.equal(run("log.fetches.length"), 1);
});

test("close during fetch, decode or after timeout leaves no image and no leaked Blob URL", async () => {
  const run = setup();
  run("liveViewLoad()"); // closed while the body is in flight
  run("liveViewClose()");
  run(`pending[0].resolve(${ok()})`);
  await tick();
  assert.equal(run("log.created.length"), 0);
  assert.equal(run("liveView.current"), null);

  run("liveViewLoad()"); // closed while the image decodes
  run(`pending[1].resolve(${ok()})`);
  await tick();
  run("liveViewClose()");
  run("log.decodes[0].resolve()");
  await tick();
  assert.equal(run("liveView.current"), null);
  assert.equal(run("JSON.stringify(log.revoked)"), JSON.stringify(["blob:0"]));

  run("liveViewLoad()"); // timed out, then closed, then the late body arrives
  const timer = run("[...log.timers.entries()].find(([, t]) => t.ms === 30000)[0]");
  run(`log.timers.get(${timer}).fn()`);
  run("liveViewClose()");
  run(`pending[2].resolve(${ok()})`);
  await tick();
  assert.equal(run("liveView.current"), null);
  assert.equal(run("log.created.length"), 1);
});

test("a download in progress keeps its Blob URL for a short grace period after close", () => {
  const run = setup();
  run(`
    liveView.current = { blob: "b", url: "blob:shown", time: new Date(), name: "x.jpg" };
    document.createElement = () => ({ click() {}, remove() {} });
    document.body.appendChild = () => {};
    liveViewDownload();
    liveViewClose();
  `);
  assert.equal(run("JSON.stringify(log.revoked)"), JSON.stringify([]));
  assert.equal(run("[...log.timers.values()].some(t => t.ms === 3000)"), true);
  run("[...log.timers.values()].find(t => t.ms === 3000).fn()");
  assert.equal(run("JSON.stringify(log.revoked)"), JSON.stringify(["blob:shown"]));
});

test("download uses the displayed Blob URL without another request", () => {
  const run = setup();
  run(`
    liveView.current = { blob: "b", url: "blob:shown", time: new Date(), name: "betta-panel-x.jpg" };
    globalThis.clicked = [];
    document.createElement = () => ({ click() { clicked.push([this.href, this.download]); }, remove() {} });
    document.body.appendChild = () => {};
    liveViewDownload();
  `);
  assert.equal(run("JSON.stringify(clicked)"), JSON.stringify([["blob:shown", "betta-panel-x.jpg"]]));
  assert.equal(run("log.fetches.length"), 0);
  run("liveView.current = null");
  run("liveViewDownload()");
  assert.equal(run("clicked.length"), 1);
});

test("local: unavailable until the local startup enabled it, then always uses the simulator, never the device endpoint", async () => {
  const run = setup({ local: true });
  assert.equal(run("liveViewAvailable()"), false);
  run("liveViewLoad()");
  assert.equal(run("log.fetches.length"), 0);
  run('liveViewEnableLocal({ variant: "paneljc" })'); // flag missing
  assert.equal(run("liveViewAvailable()"), false);
  run('liveViewEnableLocal({ variant: "paneljc", lvgl_preview: true })');
  assert.equal(run("liveViewAvailable()"), true);
  run("liveViewLoad()");
  assert.equal(run("log.fetches[0][0]"), "/api/local/simulator");
  assert.equal(run("log.timers.size"), 0); // the cold build/render path has no device timeout
  const body = JSON.parse(run("log.fetches[0][1].body"));
  assert.equal(body.page_id, "p1");
  assert.equal(body.layout.pages[0].title, "Living");
  run(`pending[0].resolve(${fail(500)})`);
  await tick();
  assert.equal(run("liveView.load"), null);
  assert.equal(run("log.fetches.every(([url]) => url !== '/api/screenshot.jpg')"), true);
});
