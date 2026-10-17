// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// The text size setting through the real editor code: control event -> request payload, saved value -> control, Reset to defaults,
// and the role sizes the panel reports (/api/version "text") that the editor's readability hints use.
import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import { loadEditor } from "./load_editor.mjs";

test("the only text size control and hint live in the layout header", () => {
  const html = fs.readFileSync(new URL("../../components/webui/www/index.html", import.meta.url), "utf8");
  assert.equal((html.match(/id="settingsUiTextStep"/g) || []).length, 1);
  const header = html.slice(html.indexOf('<div class="workspace-top">'), html.indexOf('<div id="restoreNoticeBanner"'));
  assert.ok(header.indexOf('id="settingsUiTextStep"') < header.indexOf('id="saveBtn"'));
  assert.ok(header.includes('id="settingsUiTextStepInfo"'));
  assert.ok(!html.slice(html.indexOf('id="settingsAppearanceHeading"'), html.indexOf('id="settingsTopbarHeading"')).includes('id="settingsUiTextStep"'));
});

function setup() {
  const { run } = loadEditor();
  run(`
    for (const key of Object.keys(el)) {
      if (Array.isArray(el[key])) continue;
      const listeners = new Map();
      el[key] = { value: "", checked: false, style: {}, dataset: {}, options: [], textContent: "",
        classList: { toggle() {}, add() {}, remove() {} },
        addEventListener(name, callback) {
          if (!listeners.has(name)) listeners.set(name, []);
          listeners.get(name).push(callback);
        },
        dispatchEvent(event) { for (const callback of listeners.get(event.type) || []) callback({ target: this }); },
        dispatch(name) { for (const callback of listeners.get(name) || []) callback({ target: this }); } };
    }
    document.getElementById = id => el[id] || null;
    globalThis.Event = class { constructor(type) { this.type = type; } };
    globalThis.puts = [];
    globalThis.previewRenders = 0;
    globalThis.renderCanvas = () => { previewRenders++; };
    globalThis.putSettings = async (payload) => { puts.push(JSON.parse(JSON.stringify(payload))); };
    globalThis.setTimeout = (fn) => { fn(); return 0; }; // the 400 ms debounce of the live push
    el.settingsUiStyle.value = "flat";
    el.settingsLanguage.value = "en";
    bindHardwareLive();
  `);
  return run;
}

test("choosing a text size reaches the settings request", () => {
  const run = setup();
  run("el.settingsUiTextStep.value = '2'; el.settingsUiTextStep.dispatch('change')");
  assert.equal(run("puts.length"), 1);
  assert.equal(run("puts[0].ui.text_step"), 2);
  assert.equal(run("puts[0].reboot"), false); // applied live, no reboot
  assert.equal(run("previewRenders"), 1);
  run("el.settingsUiTextStep.value = '1'; el.settingsUiTextStep.dispatch('change')");
  assert.equal(run("puts[1].ui.text_step"), 1);
});

test("the saved text size fills the control, an invalid value is clamped", () => {
  const run = setup();
  for (const [saved, shown] of [[0, "0"], [1, "1"], [2, "2"], [7, "2"], [-3, "0"], [1.4, "1"], ["abc", "0"], [undefined, "0"], [null, "0"]]) {
    run(`applyTextStepToForm({ text_step: ${JSON.stringify(saved) ?? "undefined"} })`);
    assert.equal(run("el.settingsUiTextStep.value"), shown, `saved ${saved}`);
  }
  run("applyTextStepToForm(undefined)");
  assert.equal(run("el.settingsUiTextStep.value"), "0");
});

test("appearance reset preserves the text size selected in the layout editor", () => {
  const run = setup();
  run("el.settingsUiTextStep.value = '2'");
  run("resetAppearanceDefaults()");
  assert.equal(run("el.settingsUiTextStep.value"), "2");
  assert.equal(run("puts.at(-1).ui.text_step"), 2);
});

test("the panel's role sizes follow the chosen step", () => {
  const run = setup();
  run(`editor.appText = { board: "tab5", floor: 20, roles: ["meta", "label", "value", "key", "hero"],
    sizes: [[24, 28, 32, 42, 48], [28, 32, 36, 48, 56], [32, 36, 42, 56, 64]] };`);
  run("el.settingsUiTextStep.value = '0'");
  assert.equal(run("textRoleSize('value', 1)"), 32);
  run("el.settingsUiTextStep.value = '2'");
  assert.equal(run("textRoleSize('value', 1)"), 42);
  assert.equal(run("textRoleSize('hero', 1)"), 64);
  assert.equal(run("textRoleSize('nope', 17)"), 17); // an unknown role keeps the fallback
  run("editor.appText = null");
  assert.equal(run("textRoleSize('value', 28)"), 28); // before the panel answered
  assert.equal(run("normalizeTextStep('1')"), 1);
});
