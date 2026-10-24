// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

test("Panel follows: off by default, then sends a saved page once and skips pages the panel does not have", () => {
  const { run } = loadEditor();
  run(`
    const store = new Map();
    window.localStorage = { getItem: k => store.get(k) ?? null, setItem: (k, v) => store.set(k, String(v)), removeItem: k => store.delete(k) };
    globalThis.sent = [];
    globalThis.fetch = (url, options) => { sent.push([url, JSON.parse(options.body).page]); return Promise.resolve({ ok: true }); };
    savedLayoutSnapshot = JSON.stringify({ pages: [{ id: "living" }, { id: "energy" }] });
    editor.selectedPageId = "living";
  `);
  run("syncPanelPage()");
  assert.equal(run("sent.length"), 0); // off by default
  run("setFollowPage(true)");
  assert.equal(run("JSON.stringify(sent)"), JSON.stringify([["/api/navigate", "living"]]));
  run("syncPanelPage(); editor.selectedPageId = 'unsaved_new'; syncPanelPage()");
  assert.equal(run("sent.length"), 1); // the same page once; a page not saved yet is not on the panel
  run("editor.selectedPageId = 'energy'; syncPanelPage()");
  assert.equal(run("sent[1][1]"), "energy");
  run("setFollowPage(false); editor.selectedPageId = 'living'; syncPanelPage()");
  assert.equal(run("sent.length"), 2);
  assert.equal(run("followPageEnabled()"), false);
});

test("Panel follows retries a page after HTTP rejection", async () => {
  const { run } = loadEditor();
  run(`
    followPageEnabled = () => true;
    savedLayoutSnapshot = JSON.stringify({pages: [{id: "living"}]});
    editor.selectedPageId = "living";
    globalThis.sent = 0;
    globalThis.fetch = async () => { sent++; return {ok: false}; };
  `);
  run("syncPanelPage()");
  await new Promise(resolve => setImmediate(resolve));
  run("syncPanelPage()");
  assert.equal(run("sent"), 2);
});
