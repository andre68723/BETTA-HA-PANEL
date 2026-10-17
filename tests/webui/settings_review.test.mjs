// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723
import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import { loadEditor } from "./load_editor.mjs";

function setupBindings() {
  const { run } = loadEditor();
  run(`
    globalThis.realHardwareBinder = bindHardwareLive;
    for (const name of ["setupSettingsWorkspace", "applySectionCollapseState", "initSimpleUiMenus",
      "bindAddTileDialog", "bindUpdateCheck", "bindCrashBanner", "bindInspectorHints",
      "refreshBrightnessNow", "refreshCameraBrightnessStatus", "bindHardwareLive", "bindBrightnessTests",
      "initEditorTheme", "bindSecurityControls", "bindStatusControls", "loadHaDiagnostics"])
      globalThis[name] = () => {};
    globalThis.setInterval = () => 0;
    window.setTimeout = () => 0;
    globalThis.Event = class { constructor(type) { this.type = type; } };
    for (const key of Object.keys(el)) {
      if (Array.isArray(el[key])) continue;
      const listeners = new Map();
      el[key] = { value: "", checked: false, style: {}, dataset: {},
        classList: { toggle() {}, add() {}, remove() {} },
        addEventListener(name, callback) { listeners.set(name, [...(listeners.get(name) || []), callback]); },
        dispatchEvent(event) { for (const fn of listeners.get(event.type) || []) fn({ target: this }); },
        focus() { this.focused = true; } };
    }
    document.getElementById = id => el[id] || null;
    bindUi();
    globalThis.puts = [];
    putSettings = async payload => { puts.push(payload); };
  `);
  return run;
}

test("setup URL suggestion inserts an editable value which reaches saved settings", async () => {
  const run = setupBindings();
  const html = fs.readFileSync(new URL("../../components/webui/www/index.html", import.meta.url), "utf8");
  const suggestion = html.match(/id="provHaUrl"[^>]*placeholder="([^"]+)"/)[1];
  run(`el.provHaUrl.placeholder = ${JSON.stringify(suggestion)}; el.provHaUseUrlBtn.onclick()`);
  assert.equal(run("el.provHaUrl.value"), suggestion);
  assert.equal(run("el.provHaUrl.focused"), true);
  assert.equal(run("puts.length"), 0);
  run("el.provHaUrl.value = el.provHaUrl.value.replace('homeassistant.local', '192.0.2.10'); el.provHaToken.value = 'synthetic-token'");
  await run("el.provHaSaveBtn.onclick()");
  assert.equal(run("puts[0].ha.ws_url"), "ws://192.0.2.10:8123/api/websocket");
  assert.equal(run("puts[0].reboot"), true);
});

test("crash time survives periodic status rendering and banner dismissal without duplication", async () => {
  const { run } = loadEditor();
  run(`globalThis.nodes = Object.fromEntries(['crashBanner', 'crashBannerTitle', 'crashBannerHint', 'coredumpRow', 'coredumpText'].map(id => [id, { textContent: '', hidden: false }]));
    document.getElementById = id => nodes[id] || null;
    globalThis.dismissed = null;
    storageGet = () => dismissed;
    apiGet = async () => ({ entries: [{ seq: 1, reason: 'panic', previous_wall_epoch: Math.floor(Date.now()/1000)-300 }] });
    globalThis.dump = { present: true, task: 'taskLVGL', pc: '0x1234', mcause: 2, mcause_name: 'Illegal instruction' };
    renderCrashBanner(dump);`);
  await run("loadCrashTime()");
  const initial = run("nodes.coredumpText.textContent");
  assert.ok(initial.includes(run("formatCrashTime(editor.crashTimeEpoch)")));
  run("dismissed = crashDumpId(dump); renderCrashBanner(dump); renderCrashBanner(dump)");
  assert.equal(run("nodes.crashBanner.hidden"), true);
  assert.equal(run("nodes.coredumpText.textContent"), initial);
  run("renderCrashBanner({ ...dump, pc: '0x5678' })");
  assert.equal(run("editor.crashTimeEpoch"), 0);
  assert.equal(run("nodes.coredumpText.textContent.includes('ago')"), false);
  run("renderCrashBanner({ present: false })");
  assert.equal(run("nodes.coredumpRow.hidden"), true);
});

test("late crash-time response cannot attach to a different dump", async () => {
  const { run } = loadEditor();
  run(`document.getElementById = () => null;
    apiGet = () => new Promise(resolve => { globalThis.resolveTime = resolve; });
    renderCrashBanner({ present: true, task: 'A', pc: '1' });
    globalThis.pendingTime = loadCrashTime();
    renderCrashBanner({ present: true, task: 'B', pc: '2' });
    resolveTime({ entries: [{ seq: 1, reason: 'panic', previous_wall_epoch: 123456 }] });`);
  await run("pendingTime");
  assert.equal(run("editor.crashTimeEpoch"), 0);
});

test("editor menu persists per tab across bootstrap and safely falls back for invalid storage", () => {
  const run = setupBindings();
  run(`globalThis.locationStore = new Map();
    window.sessionStorage = { getItem: key => locationStore.get(key) || null, setItem: (key, value) => locationStore.set(key, value) };
    el.actionsPanel.children = [];
    el.settingsNavButtons = [];
    el.settingsContentSections = [];
    renderCanvas = () => {};
    renderHaDiagnosticsBanner = () => {};
    restoreEditorLocation();
    setActivePane('settings');
    setActiveSettingsSection('settingsHardwareSection');
    editor.workspaceNavigationReady = false;
    editor.activeSettingsSection = 'settingsDeviceSection';
    setActivePane('layout');
    restoreEditorLocation();`);
  assert.equal(run("editor.activePane"), "settings");
  assert.equal(run("editor.activeSettingsSection"), "settingsHardwareSection");
  run("setActivePane('layout'); restoreEditorLocation()");
  assert.equal(run("editor.activePane"), "layout");
  run("locationStore.set('betta-editor-location', '{'); restoreEditorLocation()");
  assert.equal(run("editor.activePane"), "layout");
  assert.equal(run("editor.activeSettingsSection"), "settingsDeviceSection");
  run("window.sessionStorage.getItem = () => { throw new Error('storage blocked'); }; restoreEditorLocation()");
  assert.equal(run("editor.activePane"), "layout");
});

test("time zone changes use the live settings path without saving the time server", () => {
  const run = setupBindings();
  run(`globalThis.setTimeout = callback => { callback(); return 0; };
    el.settingsTimezone.value = 'CET-1CEST,M3.5.0/2,M10.5.0/3';
    el.settingsNtpServer.value = 'pool.ntp.org';
    realHardwareBinder();
    el.settingsTimezone.dispatchEvent(new Event('change'));`);
  assert.equal(run("puts[0].time.timezone"), 'CET-1CEST,M3.5.0/2,M10.5.0/3');
  assert.equal(run("puts[0].reboot"), false);
  assert.equal(run("puts[0].time.ntp_server"), undefined);
});

test("custom translation upload uses the adjacent language code and JSON file", async () => {
  const run = setupBindings();
  run(`el.uploadLanguageCode.value = 'nl';
    el.uploadLanguageFile.files = [{ text: async () => JSON.stringify({ 'tabs.settings': 'Instellingen' }) }];
    fetch = async (url, options) => { globalThis.translationRequest = { url, options }; return { ok: true }; };`);
  await run("uploadLanguageJson()");
  assert.equal(run("translationRequest.url"), '/api/i18n/custom?lang=nl');
  assert.equal(run("translationRequest.options.method"), 'PUT');
  assert.deepEqual(JSON.parse(run("translationRequest.options.body")), { 'tabs.settings': 'Instellingen' });
});
