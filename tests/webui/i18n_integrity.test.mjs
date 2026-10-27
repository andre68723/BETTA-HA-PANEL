// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

test("built-in languages preserve interpolation variables and deliberately empty states", () => {
  const { run } = loadEditor();
  const tables = run("WEB_I18N_BUILTIN");
  const keys = Object.keys(tables.en).sort();
  for (const [language, table] of Object.entries(tables)) {
    assert.deepEqual(Object.keys(table).sort(), keys, language);
    for (const key of keys) {
      assert.equal(typeof table[key], "string", `${language} ${key}`);
      if (key !== "update.auto.state.wait_disabled") assert.ok(table[key].trim(), `${language} ${key}`);
      const vars = value => [...value.matchAll(/\{[a-zA-Z0-9_]+\}/g)].map(match => match[0]).sort();
      assert.deepEqual(vars(table[key]), vars(tables.en[key]), `${language} ${key}`);
    }
  }
});

test("every entity-first widget choice has a translated label and a complete success message", () => {
  const { run, fn } = loadEditor();
  const domains = ["light", "switch", "input_boolean", "sensor", "binary_sensor", "event", "select", "input_select",
    "input_datetime", "time", "date", "datetime", "lock", "alarm_control_panel", "scene", "script", "button",
    "input_button", "climate", "cover", "fan", "camera", "vacuum", "media_player", "weather", "todo"];
  run(`addWidget = () => ({}); renderAll = () => {}; setStatus = message => { globalThis.lastStatus = message; };`);
  for (const language of ["en", "de"]) {
    run(`editor.i18nMap = WEB_I18N_BUILTIN.${language};`);
    for (const domain of domains) {
      for (const choice of fn("entityFirstSuggestions")(`${domain}.test`, "door")) {
        const key = `wizard.entity.t.${choice.key}`;
        assert.ok(run(`WEB_I18N_BUILTIN.${language}[${JSON.stringify(key)}]`), `${language} ${key}`);
        fn("entityFirstAddTile")({ id: `${domain}.test`, name: "Test" }, choice);
        const message = run("lastStatus");
        assert.doesNotMatch(message, /\{\w+\}|wizard\.entity/);
        assert.ok(message.includes(`${domain}.test`), message);
      }
    }
  }
});

test("solar previews name the days by weekday in the editor language and translate the remaining forecast", () => {
  const { run, fn } = loadEditor({ now: new Date(2026, 9, 15, 12).getTime() });
  assert.equal(run("new Date('2026-03-29T01:00:00Z').toISOString()"), "2026-03-29T01:00:00.000Z");
  for (const [language, rest, weekdays] of [["en", "Remaining", ["Th", "Fr"]], ["de", "Rest", ["Do", "Fr"]]]) {
    run(`editor.i18nMap = WEB_I18N_BUILTIN.${language}; editor.i18nLanguage = "${language}";`);
    for (const orientation of ["horizontal", "vertical"]) {
      const markup = fn("solarPreviewMarkup")({ rect: { w: 640, h: 400 }, solar_forecast_bar_orientation: orientation, align_forecast: true });
      // As solar_day_label(): today's two-letter weekday first. The header carries only the unit, also for lined-up columns.
      for (const text of weekdays) assert.ok(markup.includes(`>${text}<`), `${language} ${orientation}: ${text}`);
      assert.ok(!markup.includes(`${rest} `), `${language} ${orientation}: no ${rest} header`);
      assert.doesNotMatch(markup, /preview\.solar\./);
      if (language === "en") assert.doesNotMatch(markup, /Heute|Morgen|\bRest\b|Tag [345]/);
    }
  }
});

test("time zone labels change language while stored POSIX strings stay untouched", () => {
  const { run, fn } = loadEditor();
  run(`globalThis.Option = function(text, value) { this.text = text; this.value = value; };
    el.settingsTimezone = { value: 'CET-1CEST,M3.5.0/2,M10.5.0/3' };
    el.settingsTimezonePreset = { replaceChildren(...options) { this.options = options; } };
    el.settingsTimezoneCustomRow = {};`);
  for (const [language, label] of [["de", "Mitteleuropa"], ["en", "Central Europe"]]) {
    run(`editor.i18nMap = WEB_I18N_BUILTIN.${language};`);
    fn("renderTimezonePresets")();
    assert.equal(run("el.settingsTimezonePreset.value"), "central_europe");
    assert.ok(run("el.settingsTimezonePreset.options[0].text").startsWith(label));
    assert.equal(run("el.settingsTimezone.value"), "CET-1CEST,M3.5.0/2,M10.5.0/3");
    for (const preset of run("TIMEZONE_PRESETS")) {
      assert.ok(run(`WEB_I18N_BUILTIN.${language}[${JSON.stringify(`settings.time.preset.${preset.id}`)}]`));
    }
  }
});

test("the colour editor redraws translated labels without discarding an edited palette", () => {
  const { run, fn } = loadEditor();
  run(`globalThis.nodes = {};
    document.getElementById = id => nodes[id] || null;
    document.createElement = tag => ({ tag, children: [], dataset: {},
      appendChild(child) { this.children.push(child); }, addEventListener() {} });
    for (const id of ['settingsThemeColors', 'settingsThemeNewId', 'settingsThemeNewName', 'settingsThemeBase', 'settingsThemeSaveCustomBtn'])
      nodes[id] = { children: [], appendChild(child) { this.children.push(child); }, set innerHTML(value) { this.children = []; } };
    themeState.list = [{ id: 'mine', name: 'My palette', builtin: false }];
    themeState.editing = { id: 'mine', name: 'My palette', colors: { card_bg_on: '#ffffff' } };`);
  for (const [language, group, save] of [["de", "Karten", "Änderungen speichern"], ["en", "Cards", "Save changes"]]) {
    run(`editor.i18nMap = WEB_I18N_BUILTIN.${language};`);
    fn("themeRenderColorGrid")(run("themeState.editing"));
    fn("themeSyncEditFields")();
    assert.equal(run("nodes.settingsThemeColors.children[0].children[0].textContent"), group);
    assert.equal(run("nodes.settingsThemeSaveCustomBtn.textContent"), save);
    assert.equal(run("themeState.editing.colors.card_bg_on"), "#ffffff");
    for (const entry of run("THEME_COLOR_GROUPS")) {
      for (const key of entry.keys) assert.ok(run(`WEB_I18N_BUILTIN.${language}[${JSON.stringify(`settings.theme.color.${key}`)}]`), key);
    }
  }
});
