// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import { loadEditor } from "./load_editor.mjs";

function setupInspector(widgetType = "graph", entityId = "sensor.temperature", searchTarget = "", liveHardware = false, liveBrightness = false) {
  const { run } = loadEditor();
  run(`
    // Stub presentation and background polling; keep real event binding,
    // Inspector application, normalization and layout serialization.
    for (const name of ["setupSettingsWorkspace", "applySectionCollapseState", "initSimpleUiMenus",
      "bindAddTileDialog", "bindUpdateCheck", "bindCrashBanner", "bindInspectorHints",
      "refreshBrightnessNow", "refreshCameraBrightnessStatus", "bindHardwareLive", "bindBrightnessTests",
      "initEditorTheme", "bindSecurityControls", "bindStatusControls", "loadHaDiagnostics"]) {
      if (!(name === "bindHardwareLive" && ${liveHardware}) && !(name === "bindBrightnessTests" && ${liveBrightness})) globalThis[name] = () => {};
    }
    globalThis.setInterval = () => 0;
    window.setTimeout = () => 0;
    globalThis.Blob = class { constructor(parts) { this.size = parts.join("").length; } };
    globalThis.savedLayout = null;
    globalThis.Event = class { constructor(type) { this.type = type; } };
    globalThis.pickerOptions = null;
    openLightEntityPicker = (_kind, options) => { pickerOptions = options; };
    globalThis.fetch = async (_url, options) => {
      savedLayout = JSON.parse(options.body);
      return { ok: true };
    };
    for (const key of Object.keys(el)) {
      if (Array.isArray(el[key])) continue;
      const listeners = new Map();
      el[key] = { value: "", checked: false, style: {}, dataset: {}, events: listeners,
        classList: { toggle() {}, add() {}, remove() {}, contains() { return false; } },
        addEventListener(name, callback) {
          if (!listeners.has(name)) listeners.set(name, []);
          listeners.get(name).push(callback);
        },
        focus() {}, appendChild() {},
        dispatchEvent(event) { this.dispatch(event.type); },
        dispatch(name) {
          const event = { target: this };
          for (const callback of listeners.get(name) || []) callback(event);
          this["on" + name]?.(event);
        }
      };
    }
    globalThis.searchButton = { dataset: { inspectorEntityTarget: ${JSON.stringify(searchTarget)} },
      addEventListener(name, callback) { this[name] = callback; } };
    document.querySelectorAll = selector => selector === "[data-inspector-entity-target]" ? [searchButton] : [];
    document.getElementById = id => el[id] || null;
    bindUi();
    editor.layout = { pages: [{ id: "history", widgets: [{ id: "graph", type: ${JSON.stringify(widgetType)},
      entity_id: ${JSON.stringify(entityId)}, title: "Temperature", rect: { x: 20, y: 20, w: 590, h: 300 } }] }] };
    editor.selectedPageId = "history";
    editor.selectedWidgetId = "graph";
    el.fEntity.value = ${JSON.stringify(entityId)};
    el.fTitle.value = "Temperature";
    el.fGraphTimeWindowMin.value = "120";
    renderInspectorChange = () => { editor.dirty = true; };
  `);
  return run;
}

for (const [input, property] of [
  ["fEntity", "entity_id"], ["fSecondaryEntity", "secondary_entity_id"],
  ["fTertiaryEntity", "tertiary_entity_id"], ["fEtlCharge", "battery_charge_entity_id"],
  ["fEtlDischarge", "battery_discharge_entity_id"], ["fEtlSoc", "battery_soc_entity_id"],
  ["fEtlForecast", "forecast_today_entity_id"],
]) {
 for (const [view, binder] of [["prototype", "spikeBindEntityField"], ["editor", "bindWrappingEntityField"]]) {
  test(`${view} wrapping ${input}: edit, save, reload and clear ${property}`, async () => {
    const run = setupInspector("energy_timeline", "sensor.grid_import");
    run(fs.readFileSync(new URL("../../tools/editor_local/spike/entity-fields.js", import.meta.url), "utf8"));
    run(`
      scheduleEntityAutocomplete = () => {};
      loadVacuumModeOptions = async () => {};
      globalThis.wrapped = { value: "", events: {},
        addEventListener(type, callback) { this.events[type] = callback; },
        dispatch(type) { this.events[type]?.({target:this}); },
        focus() { this.dispatch("focus"); }, select() {}, blur() { this.dispatch("change"); this.dispatch("blur"); }
      };
      ${binder}(el.${input}, wrapped, () => {});
      wrapped.value = "sensor.demo_long_energy_counter_with_a_complete_entity_name";
      wrapped.dispatch("input"); wrapped.dispatch("change"); wrapped.dispatch("blur");
    `);
    const value = "sensor.demo_long_energy_counter_with_a_complete_entity_name";
    assert.equal(run(`selectedWidget().${property}`), value);
    await run("el.saveBtn.onclick()");
    assert.equal(run(`savedLayout.pages[0].widgets[0].${property}`), value);
    run(`editor.layout = savedLayout; wrapped.value = ""; wrapped.dispatch("change"); wrapped.dispatch("blur");`);
    await run("el.saveBtn.onclick()");
    // Clearing the required grid counter preserves the last valid binding; optional counters are removed.
    if (input === "fEntity") assert.equal(run(`savedLayout.pages[0].widgets[0].${property}`), value);
    else assert.ok(!run(`savedLayout.pages[0].widgets[0].${property}`));
  });
 }
}

for (const [view, binder] of [["prototype", "spikeBindEntityField"], ["editor", "bindWrappingEntityField"]]) {
test(`${view} Search selection updates the wrapping field and survives focus, save and reload`, async () => {
  const run = setupInspector("energy_timeline", "sensor.grid_import", "etl_charge");
  run(fs.readFileSync(new URL("../../tools/editor_local/spike/entity-fields.js", import.meta.url), "utf8"));
  run(`
    scheduleEntityAutocomplete = () => {};
    globalThis.wrapped = { value: "sensor.old_charge", events: {},
      addEventListener(type, callback) { this.events[type] = callback; },
      dispatch(type) { this.events[type]?.({target:this}); },
      focus() { this.dispatch("focus"); }, select() {}, blur() { this.dispatch("blur"); }
    };
    ${binder}(el.fEtlCharge, wrapped, () => {});
    searchButton.click();
    assignInspectorEntity(pickerOptions.inspectorTarget, "sensor.selected_battery_charge_counter");
  `);
  assert.equal(run("wrapped.value"), "sensor.selected_battery_charge_counter");
  run("wrapped.blur()");
  await run("el.saveBtn.onclick()");
  run("editor.layout = savedLayout");
  assert.equal(run("selectedWidget().battery_charge_entity_id"), "sensor.selected_battery_charge_counter");
});
}

for (const open of ["0", "1"]) {
  test(`prototype help restores the complete text from an existing info toggle (${open})`, () => {
    const { run } = loadEditor();
    run(fs.readFileSync(new URL("../../tools/editor_local/spike/entity-fields.js", import.meta.url), "utf8"));
    run(`
      globalThis.helpText = "Energy counters need a state class of total or total_increasing (long-term statistics). Battery charge and discharge go together. The forecast needs Solcast's detailedForecast attribute.";
      globalThis.hint = { textContent: "Energy counters need a state class… ℹ️", dataset: {
        hintFull: helpText, hintShown: "Energy counters need a state class…", hintOpen: ${JSON.stringify(open)}
      }, classes: new Set(),
        classList: { contains(name) { return hint.classes.has(name); }, add(name) { hint.classes.add(name); } },
        querySelector() { return null; }, closest() { return {}; }
      };
      spikeFullHint(hint); spikeFullHint(hint);
    `);
    assert.equal(run("hint.textContent"), run("helpText"));
    assert.equal(run("Object.keys(hint.dataset).length"), 0);
  });
}

for (const [input, property] of [
  ["energyHomePower", "home_power_entity_id"], ["energySolarPower", "solar_power_entity_id"],
  ["energyGridPower", "grid_power_entity_id"], ["energyGridImport", "grid_import_power_entity_id"],
  ["energyGridExport", "grid_export_power_entity_id"], ["energyBatteryPower", "battery_power_entity_id"],
  ["energyBatteryCharge", "battery_charge_power_entity_id"], ["energyBatteryDischarge", "battery_discharge_power_entity_id"],
  ["energyBatterySoc", "battery_soc_entity_id"], ["energyGas", "gas_entity_id"], ["energyWater", "water_entity_id"],
]) {
  for (const [view, binder] of [["prototype", "spikeBindEntityField"], ["editor", "bindWrappingEntityField"]]) {
  test(`${view} Energy page ${input}: edit, Search, source switch, save, reload and clear`, async () => {
    const run = setupInspector();
    run(fs.readFileSync(new URL("../../tools/editor_local/spike/entity-fields.js", import.meta.url), "utf8"));
    run(`
      renderAll = () => {};
      loadEnergyPreview = async () => {};
      editor.layout = { pages: [{ id: "energy", type: "energy_dashboard", widgets: [], energy: { source: "manual_live" } }] };
      editor.selectedPageId = "energy"; editor.selectedWidgetId = null;
      el.energySource.value = "manual_live";
      globalThis.wrapped = { value: "", events: {},
        addEventListener(type, callback) { this.events[type] = callback; },
        dispatch(type) { this.events[type]?.({target:this}); },
        focus() { this.dispatch("focus"); }, select() {}, blur() { this.dispatch("change"); this.dispatch("blur"); }
      };
      ${binder}(el.${input}, wrapped, () => {});
      wrapped.value = "sensor.complete_energy_page_sensor_name";
      wrapped.dispatch("input"); wrapped.dispatch("change"); wrapped.dispatch("blur");
    `);
    assert.equal(run(`selectedPage().energy.${property}`), "sensor.complete_energy_page_sensor_name");
    run(`openLightEntityPicker("page_sensor", { inspectorTarget: { input: el.${input}, domain: "sensor" } })`);
    assert.equal(run("assignInspectorEntity(pickerOptions.inspectorTarget, 'light.invalid')"), false);
    assert.equal(run("assignInspectorEntity(pickerOptions.inspectorTarget, 'sensor.chosen_energy_page_sensor')"), true);
    assert.equal(run("wrapped.value"), "sensor.chosen_energy_page_sensor");
    run("el.energySource.value = 'ha_energy'; el.energySource.onchange()");
    await run("el.saveBtn.onclick()");
    assert.equal(run(`savedLayout.pages[0].energy.${property}`), "sensor.chosen_energy_page_sensor");
    run("editor.layout = savedLayout; el.energySource.value = 'manual_live'; el.energySource.onchange(); wrapped.value = ''; wrapped.blur()");
    await run("el.saveBtn.onclick()");
    assert.ok(!run(`savedLayout.pages[0].energy.${property}`));
  });
  }
}

test("Recorder checkbox change reaches the saved graph model", async () => {
  const run = setupInspector();
  run("el.fGraphHaHistory.checked = true; el.fGraphHaHistory.dispatch('change')");
  assert.equal(run("selectedWidget().graph_ha_history"), true);
  assert.equal(run("selectedWidget().graph_time_window_min"), 1440);
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].graph_ha_history"), true);
  run("editor.layout = savedLayout; el.fGraphHaHistory.checked = false; el.fGraphHaHistory.dispatch('change')");
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].graph_ha_history"), undefined);
});

for (const [input, type, entity, property, firstChecked, firstValue, secondValue] of [
  ["fHistoryPopup", "sensor", "sensor.temperature", "history_popup", true, true, undefined],
  ["fHistoryPopup", "binary_tile", "binary_sensor.door", "history_popup", true, true, undefined],
  ["fHistoryPopup", "contact_tile", "binary_sensor.door", "history_popup", true, true, undefined],
  ["fHistoryPopup", "lock_tile", "lock.front_door", "history_popup", true, true, undefined],
  ["fRoomControls", "room_tile", "sensor.temperature", "room_controls", true, true, undefined],
  ["fHistoryPopup", "vacuum_tile", "vacuum.robot", "history_popup", true, true, undefined],
  ["fCameraLive", "camera_tile", "camera.printer", "camera_live", true, true, undefined],
  ["fLockConfirm", "lock_tile", "lock.front_door", "lock_confirm", true, true, undefined],
  ["fPriceDaySwitch", "price_forecast", "sensor.epex_spot_data_market_price", "price_day_switch", true, true, undefined],
  ["fPriceCheapTimes", "price_forecast", "sensor.epex_spot_data_market_price", "price_cheap_times", true, true, undefined],
  ["fPriceLine", "price_forecast", "sensor.epex_spot_data_market_price", "price_line", true, true, undefined],
  ["fParqetHideAmounts", "parqet_portfolio", "sensor.portfolio_value", "parqet_hide_amounts", true, true, undefined],
  ["fParqetSelector", "parqet_portfolio", "sensor.portfolio_value", "parqet_selector", true, true, undefined],
  ["fShowName", "sensor", "sensor.temperature", "show_name", false, false, undefined],
  ["fSensorShowAge", "sensor", "sensor.temperature", "sensor_show_age", false, false, undefined],
  ["fWeatherShowPrecip", "weather_3day", "weather.home", "show_precipitation", false, false, undefined],
  ["fAlignForecast", "solar_forecast", "sensor.solar", "align_forecast", true, true, undefined],
]) {
  test(`${input} on ${type}: change, save, reload model and reverse`, async () => {
    const run = setupInspector(type, entity);
    run(`el.${input}.checked = ${firstChecked}; el.${input}.dispatch('change')`);
    assert.equal(run(`selectedWidget().${property}`), firstValue);
    await run("el.saveBtn.onclick()");
    assert.equal(run(`savedLayout.pages[0].widgets[0].${property}`), firstValue);
    run(`editor.layout = savedLayout; el.${input}.checked = ${!firstChecked}; el.${input}.dispatch('change')`);
    await run("el.saveBtn.onclick()");
    assert.equal(run(`savedLayout.pages[0].widgets[0].${property}`), secondValue);
  });
}

test("portfolio view and interval: choose, save, reload and return to the defaults through Inspector events", async () => {
  const run = setupInspector("parqet_portfolio", "sensor.portfolio_value");
  run("populateMovePageOptions = renderEntityOptions = () => {}");
  run('el.fParqetView.value = "holdings"; el.fParqetView.dispatch("change")');
  run('el.fParqetInterval.value = "1m"; el.fParqetInterval.dispatch("change")');
  assert.equal(run("selectedWidget().parqet_view"), "holdings");
  assert.equal(run("selectedWidget().parqet_interval"), "1m");
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].parqet_view"), "holdings");
  assert.equal(run("savedLayout.pages[0].widgets[0].parqet_interval"), "1m");
  run("editor.layout = savedLayout; renderInspector()");
  assert.equal(run("el.fParqetView.value"), "holdings");
  assert.equal(run("el.fParqetInterval.value"), "1m");
  run('el.fParqetView.value = "performance"; el.fParqetView.dispatch("change"); el.fParqetInterval.value = ""; el.fParqetInterval.dispatch("change")');
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].parqet_view"), undefined);
  assert.equal(run("savedLayout.pages[0].widgets[0].parqet_interval"), undefined);
});

test("sensor format and unit: offered by device class, saved, kept while hidden and reset to the default", async () => {
  const run = setupInspector("sensor", "sensor.sun_next_rising");
  run("populateMovePageOptions = renderEntityOptions = () => {}");
  run(`editor.entities = [{ id: "sensor.sun_next_rising", device_class: "timestamp", unit: "" },
    { id: "sensor.temperature", device_class: "temperature", unit: "°C" },
    { id: "sensor.runtime", device_class: "duration", unit: "s" },
    { id: "sensor.power", device_class: "power", unit: "W" },
    { id: "sensor.cost", device_class: "monetary", unit: "EUR" },
    { id: "sensor.mode", device_class: "enum", unit: "" }]; renderInspector()`);
  assert.equal(run("sensorTimeFormatShown(selectedWidget(), el.fEntity.value)"), true);
  assert.equal(run("el.fEntityClass.dataset.text"), "Timestamp");
  assert.equal(run("el.fSensorFormatLabel.textContent"), "Time format");
  assert.equal(run("sensorUnitChoices(selectedWidget(), el.fEntity.value)"), null);
  run('el.fSensorFormat.value = "relative"; el.fSensorFormat.dispatch("change")');
  assert.equal(run("selectedWidget().sensor_format"), "relative");
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].sensor_format"), "relative");
  run("editor.layout = savedLayout; renderInspector()");
  assert.equal(run("el.fSensorFormat.value"), "relative");

  // A temperature sensor: no format, a unit choice whose default reads as the unit; the saved format stays.
  run('el.fEntity.value = "sensor.temperature"; el.fEntity.onchange()');
  assert.equal(run("el.fEntityClass.dataset.text"), "Temperature · °C");
  assert.equal(run("sensorFormatChoices(selectedWidget(), el.fEntity.value)"), null);
  assert.equal(run("JSON.stringify(sensorUnitChoices(selectedWidget(), el.fEntity.value).values)"), JSON.stringify(["", "degree", "hide"]));
  assert.match(run("el.fSensorUnit.innerHTML"), />°C</);
  run('el.fSensorUnit.value = "degree"; el.fSensorUnit.dispatch("change")');
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].sensor_unit"), "degree");
  assert.equal(run("savedLayout.pages[0].widgets[0].sensor_format"), "relative");
  run("editor.layout = savedLayout; renderInspector()");
  assert.equal(run("el.fSensorUnit.value"), "degree");

  // A duration: the format list changes; "relative" is not offered, so it is kept until a duration format is chosen.
  run('el.fEntity.value = "sensor.runtime"; el.fEntity.onchange()');
  assert.equal(run("el.fSensorFormatLabel.textContent"), "Format");
  assert.equal(run("el.fSensorFormat.value"), "text");
  assert.equal(run("selectedWidget().sensor_format"), "relative");
  run('el.fSensorFormat.value = "clock"; el.fSensorFormat.dispatch("change")');
  assert.equal(run("selectedWidget().sensor_format"), "clock");
  run('el.fSensorFormat.value = "text"; el.fSensorFormat.dispatch("change")');
  assert.equal(run("selectedWidget().sensor_format"), undefined);

  // Power and monetary: their own unit choices and default texts.
  run('el.fEntity.value = "sensor.power"; el.fEntity.onchange()');
  assert.equal(run("JSON.stringify(sensorUnitChoices(selectedWidget(), el.fEntity.value).values)"), JSON.stringify(["", "auto", "hide"]));
  assert.match(run("el.fSensorUnit.innerHTML"), /As in Home Assistant/);
  run('el.fSensorUnit.value = "auto"; el.fSensorUnit.dispatch("change")');
  assert.equal(run("selectedWidget().sensor_unit"), "auto");
  run('el.fEntity.value = "sensor.cost"; el.fEntity.onchange()');
  assert.equal(run("el.fSensorUnitLabel.textContent"), "Currency");
  assert.equal(run("el.fSensorUnit.value"), ""); // "auto" is no currency choice: shown as the default, kept saved
  assert.equal(run("selectedWidget().sensor_unit"), "auto");
  run('el.fSensorUnit.value = ""; el.fSensorUnit.dispatch("change")');
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].sensor_unit"), undefined);
  run('el.fEntity.value = "sensor.mode"; el.fEntity.onchange()');
  assert.equal(run("sensorUnitChoices(selectedWidget(), el.fEntity.value)"), null);
  assert.equal(run("sensorFormatChoices(selectedWidget(), el.fEntity.value)"), null);
});

test("entity and state lists: every page of the panel's answer is loaded", async () => {
  const run = setupInspector("sensor", "sensor.temperature");
  run(`globalThis.requested = []; globalThis.apiGet = async (path) => {
    requested.push(path);
    const offset = Number((path.match(/offset=(\\d+)/) || [0, 0])[1]);
    const ids = Array.from({ length: 300 }, (_, i) => "sensor.s" + i).slice(offset, offset + 128);
    return { items: ids.map((id) => ({ entity_id: id, id, state: "1", attributes_json: "{}" })), total: 300 };
  };`);
  await run("refreshStates()");
  assert.equal(run("editor.states.size"), 300);
  assert.equal(run("JSON.stringify(requested)"), JSON.stringify(["/api/state", "/api/state?offset=128", "/api/state?offset=256"]));
  run("requested.length = 0; apiGet = async (path) => { requested.push(path); return { items: [{ id: 'sensor.a' }] }; }");
  await run("loadEntities()");
  assert.equal(run("editor.entities.length"), 1); // an older panel without total: one page
  assert.equal(run("requested.length"), 1);
});

test("sensor preview: formats the state like the panel by device class", () => {
  const run = setupInspector("sensor", "sensor.temperature");
  run(`editor.i18nLanguage = "de"; editor.entities = [
    { id: "sensor.temperature", device_class: "temperature", unit: "°C" },
    { id: "sensor.power", device_class: "power", unit: "W" },
    { id: "sensor.runtime", device_class: "duration", unit: "s" },
    { id: "sensor.cost", device_class: "monetary", unit: "EUR" },
    { id: "sensor.pickup", device_class: "date", unit: "" },
    { id: "sensor.risk", device_class: "enum", unit: "" },
    { id: "binary_sensor.door", device_class: "door", unit: "" }];
    editor.states = new Map([["sensor.temperature", "21.5"], ["sensor.power", "2350"], ["sensor.runtime", "3725"],
      ["sensor.cost", "12.34"], ["sensor.pickup", "2026-10-21"], ["sensor.risk", "no_risk"], ["binary_sensor.door", "on"]]);`);
  const value = (id, extra = {}) => run(`previewSensorValue(${JSON.stringify({ entity_id: id, ...extra })})`);
  assert.equal(value("sensor.temperature"), "21,5 °C");
  assert.equal(value("sensor.temperature", { sensor_unit: "degree" }), "21,5°");
  assert.equal(value("sensor.temperature", { sensor_unit: "hide" }), "21,5");
  assert.equal(value("sensor.power"), "2350 W");
  assert.equal(value("sensor.power", { sensor_unit: "auto" }), "2,35 kW");
  assert.equal(value("sensor.runtime"), "1 h 2 min");
  assert.equal(value("sensor.runtime", { sensor_format: "clock" }), "1:02:05");
  assert.equal(value("sensor.runtime", { sensor_format: "raw" }), "3725 s");
  assert.equal(value("sensor.cost"), "12,34 €");
  assert.equal(value("sensor.cost", { sensor_unit: "code" }), "12,34 EUR");
  run('editor.states.set("sensor.cost", "0.968200683593693")');
  assert.equal(value("sensor.cost"), "0,97 €");
  assert.equal(value("sensor.pickup"), "21.10.2026");
  assert.equal(value("sensor.risk"), "No risk");
  assert.equal(value("binary_sensor.door"), "Open");
});

test("device class chips: named for sensors and binary sensors, none only for a listed entity, absent elsewhere", () => {
  const run = setupInspector("binary_tile", "binary_sensor.washer");
  run("populateMovePageOptions = renderEntityOptions = () => {}");
  run(`editor.entities = [{ id: "binary_sensor.washer", device_class: "power", unit: "" },
    { id: "sensor.counter", device_class: "", unit: "" }, { id: "light.desk", device_class: "", unit: "" }]; renderInspector()`);
  assert.equal(run("el.fEntityClass.dataset.text"), "Power on/off");
  assert.match(run("el.fEntityClass.innerHTML"), /^<span class="entity-class-chip" title="Device class">Power on\/off<\/span>$/);
  run(`editor.entities.push({ id: "sensor.temperature_out", device_class: "temperature", unit: "°C" });
    el.fEntity.value = "sensor.temperature_out"; el.fEntity.onchange()`);
  assert.equal(run("el.fEntityClass.dataset.text"), "Temperature · °C");
  assert.match(run("el.fEntityClass.innerHTML"), /title="Unit">°C</);
  run('el.fEntity.value = "sensor.counter"; el.fEntity.onchange()');
  assert.equal(run("el.fEntityClass.dataset.text"), "No device class");
  assert.match(run("el.fEntityClass.innerHTML"), /entity-class-chip is-none/);
  run('el.fEntity.value = "sensor.not_listed"; el.fEntity.onchange()');
  assert.equal(run("el.fEntityClass.dataset.text"), "");
  run('el.fEntity.value = "light.desk"; el.fEntity.onchange()');
  assert.equal(run("el.fEntityClass.dataset.text"), "");
  assert.equal(run('entityIdWithClass({ id: "sensor.t", device_class: "temperature" })'), "sensor.t · Temperature");
  assert.equal(run('entityIdWithClass({ id: "light.desk", device_class: "" })'), "light.desk");
});

test("price fixed average: edit, save, reload and clear through Inspector events", async () => {
  const run = setupInspector("price_forecast", "sensor.price");
  run("populateMovePageOptions = renderEntityOptions = () => {}"); // unrelated select/datalist presentation
  run('el.fPriceAverage.value = "0.25"; el.fPriceAverage.dispatch("change")');
  assert.equal(run("selectedWidget().price_average"), 0.25);
  await run("el.saveBtn.onclick()");
  run("editor.layout = savedLayout; renderInspector()");
  assert.equal(run("Number(el.fPriceAverage.value)"), 0.25);
  run('el.fPriceAverage.value = "0.125"; el.fPriceAverage.dispatch("blur")');
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].price_average"), 0.125);
  run('editor.layout = savedLayout; el.fPriceAverage.value = ""; el.fPriceAverage.dispatch("change")');
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].price_average"), undefined);
  run("editor.layout = savedLayout; renderInspector()");
  assert.equal(run("el.fPriceAverage.value"), "");
});

for (const [input, key, entity] of [
  ["fBambuNozzle", "nozzle_temp_entity_id", "sensor.printer_nozzle"],
  ["fBambuBed", "bed_temp_entity_id", "sensor.printer_bed"],
  ["fBambuProgress", "progress_entity_id", "sensor.printer_progress"],
  ["fBambuRemaining", "remaining_time_entity_id", "sensor.printer_remaining"],
  ["fBambuStage", "stage_entity_id", "sensor.printer_stage"],
  ["fBambuJob", "job_name_entity_id", "sensor.printer_job"],
  ["fBambuPause", "pause_entity_id", "button.printer_pause"],
  ["fBambuResume", "resume_entity_id", "button.printer_resume"],
  ["fBambuStop", "stop_entity_id", "button.printer_stop"],
  ["fBambuCamera", "camera_entity_id", "camera.printer"],
]) {
  test(`${input} on bambu_printer: enter, save, reload and clear ${key}`, async () => {
    const run = setupInspector("bambu_printer", "sensor.printer_status");
    run(`el.${input}.value = ${JSON.stringify(entity)}; el.${input}.dispatch('change')`);
    assert.equal(run(`selectedWidget().${key}`), entity);
    assert.equal(run("selectedWidget().secondary_entity_id"), undefined);
    assert.equal(run("selectedWidget().tertiary_entity_id"), undefined);
    await run("el.saveBtn.onclick()");
    assert.equal(run(`savedLayout.pages[0].widgets[0].${key}`), entity);
    assert.equal(run("savedLayout.pages[0].widgets[0].entity_id"), "sensor.printer_status");
    // reload: the Inspector shows what was saved, and clearing the field removes the key again
    run(`editor.layout = savedLayout; editor.selectedWidgetId = "graph";`);
    run(`bambuLoadInspector(selectedWidget())`);
    assert.equal(run(`el.${input}.value`), entity);
    run(`el.${input}.value = ""; el.${input}.dispatch('change')`);
    await run("el.saveBtn.onclick()");
    assert.equal(run(`savedLayout.pages[0].widgets[0].${key}`), undefined);
  });
}

test("alarm_tile actions: change, save, reload and restore all", async () => {
  const run = setupInspector("alarm_tile", "alarm_control_panel.house");
  // a loaded alarm tile shows every action checked (the stubbed elements start unchecked)
  run("for (const [key] of ALARM_ACTION_FIELDS) el[key].checked = true");
  run("el.fAlarmDisarm.checked = false; el.fAlarmDisarm.dispatch('change')");
  const expected = JSON.stringify(["arm_home", "arm_away", "arm_night", "arm_vacation", "arm_custom_bypass"]);
  assert.equal(run("JSON.stringify(selectedWidget().alarm_actions)"), expected);
  await run("el.saveBtn.onclick()");
  assert.equal(run("JSON.stringify(savedLayout.pages[0].widgets[0].alarm_actions)"), expected);
  run("editor.layout = savedLayout; editor.selectedWidgetId = 'graph'; alarmLoadInspector(selectedWidget())");
  assert.equal(run("el.fAlarmDisarm.checked"), false);
  assert.equal(run("el.fAlarmArmHome.checked"), true);
  run("el.fAlarmDisarm.checked = true; el.fAlarmDisarm.dispatch('change')");
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].alarm_actions"), undefined, "all actions is the default");
});

test("fVacuumBattery on vacuum_tile: enter, save, reload and clear battery_entity_id", async () => {
  const run = setupInspector("vacuum_tile", "vacuum.robot");
  run("el.fVacuumBattery.value = 'sensor.robot_battery'; el.fVacuumBattery.dispatch('change')");
  assert.equal(run("selectedWidget().battery_entity_id"), "sensor.robot_battery");
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].battery_entity_id"), "sensor.robot_battery");
  run("editor.layout = savedLayout; el.fVacuumBattery.value = ''; el.fVacuumBattery.dispatch('change')");
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].battery_entity_id"), undefined);
});

test("choosing a printer fills every bambu binding and keeps nothing of another printer", async () => {
  const run = setupInspector("bambu_printer", "sensor.old_status");
  run("el.fBambuStop.value = 'button.old_stop'; el.fBambuStop.dispatch('change')");
  run(`bambuApplyPrinterEntities({ entity_id: "sensor.a1_status", nozzle_temp_entity_id: "sensor.a1_nozzle",
    bed_temp_entity_id: "sensor.a1_bed", progress_entity_id: "sensor.a1_progress", remaining_time_entity_id: "sensor.a1_rest",
    pause_entity_id: "button.a1_pause", resume_entity_id: "button.a1_resume" }, "A1 Workshop")`);
  assert.equal(run("selectedWidget().entity_id"), "sensor.a1_status");
  assert.equal(run("selectedWidget().nozzle_temp_entity_id"), "sensor.a1_nozzle");
  assert.equal(run("selectedWidget().resume_entity_id"), "button.a1_resume");
  assert.equal(run("selectedWidget().stop_entity_id"), undefined, "a binding the printer lacks is cleared");
  assert.equal(run("selectedWidget().secondary_entity_id"), undefined);
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].bed_temp_entity_id"), "sensor.a1_bed");
});

test("the printer list is filled from /api/ha/printers and a single printer is chosen at once", async () => {
  const run = setupInspector("bambu_printer", "sensor.printer_status");
  run(`globalThis.apiGet = async () => ({ status: "ready", printers: [{ id: "e1", name: "A1 Workshop",
    entities: { entity_id: "sensor.a1_status", pause_entity_id: "button.a1_pause" } }] });
    document.createElement = () => ({ appendChild() {}, set textContent(v) { this._t = v; }, set value(v) { this._v = v; } });
    el.fBambuPrinter.appendChild = () => {};`);
  await run("bambuDetectPrinters({ autoPick: true })");
  assert.equal(run("selectedWidget().entity_id"), "sensor.a1_status");
  assert.equal(run("selectedWidget().pause_entity_id"), "button.a1_pause");
});

test("changing the status sensor of a bambu_printer keeps every other binding", async () => {
  const run = setupInspector("bambu_printer", "sensor.printer_status");
  run("el.fBambuPause.value = 'button.printer_pause'; el.fBambuPause.dispatch('change')");
  run("el.fBambuNozzle.value = 'sensor.printer_nozzle'; el.fBambuNozzle.dispatch('change')");
  run("el.fEntity.value = 'sensor.other_status'; el.fEntity.dispatch('change')");
  assert.equal(run("selectedWidget().pause_entity_id"), "button.printer_pause");
  assert.equal(run("selectedWidget().nozzle_temp_entity_id"), "sensor.printer_nozzle");
});

test("bambu bindings do not stay on a widget that changes type", () => {
  const run = setupInspector("bambu_printer", "sensor.printer_status");
  run("el.fBambuStop.value = 'button.printer_stop'; el.fBambuStop.dispatch('change')");
  assert.equal(run("selectedWidget().stop_entity_id"), "button.printer_stop");
  run("const w = selectedWidget(); bambuSaveInspector(w, 'sensor');");
  assert.equal(run("selectedWidget().stop_entity_id"), undefined);
});

test("every Inspector input has a registered editing event", () => {
  const run = setupInspector();
  const html = fs.readFileSync(new URL('../../components/webui/www/index.html', import.meta.url), 'utf8');
  const fields = [...html.matchAll(/<(?:input|select|textarea)\b[^>]*\bid="(f\w+)"/g)].map(match => match[1]);
  for (const field of fields) {
    assert.equal(run(`typeof el.${field}.onchange === 'function' || ['change', 'input', 'blur'].some(event => el.${field}.events.has(event))`), true, field);
  }
});

for (const [project, camera, rotation] of [
  ["betta-ha-panel-tab5", false, true],
  ["betta-ha-panel", false, false],
  ["betta-ha-panel-jc", true, false],
  ["betta-ha-panel-jc-v2", true, false],
]) {
  test(`Display reset uses board defaults and preserves sound for ${project}`, () => {
    const run = setupInspector();
    run(`editor.appProject = ${JSON.stringify(project)}; pushHardwareLive = () => {};
      el.settingsSoundVolume.value = '27'; el.settingsSoundEnabled.checked = true;
      el.settingsSoundNightMute.checked = false; el.settingsHardwareResetBtn.onclick()`);
    assert.equal(run("Number(el.settingsSoundVolume.value)"), 27);
    assert.equal(run("el.settingsSoundEnabled.checked"), true);
    assert.equal(run("el.settingsSoundNightMute.checked"), false);
    assert.equal(run("el.settingsHardwareNightEnabled.checked"), true);
    assert.equal(run("el.settingsHardwareNightStart.value"), "22:00");
    assert.equal(run("el.settingsHardwareNightEnd.value"), "06:00");
    assert.equal(run("el.settingsHardwareAutoBrightness.checked"), camera);
    assert.equal(run("el.settingsHardwarePresenceWake.checked"), camera);
    assert.equal(run("el.settingsHardwareAutoRotation.checked"), rotation);
  });
}

test("Energy page details choice reaches the saved model and survives a reload", async () => {
  const run = setupInspector();
  run(`
    renderAll = () => {};
    editor.layout = { pages: [{ id: "energy", type: "energy_dashboard", title: "Energy", widgets: [],
      energy: { source: "manual_live" } }] };
    editor.selectedPageId = "energy";
    el.energySource.value = "manual_live";
    el.energyDetails.value = "side";
    el.energyDetails.onchange();
  `);
  assert.equal(run("selectedPage().energy.details"), "side");
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].energy.details"), "side");
  // reload: the control shows the saved choice, and choosing the popup again stores nothing
  run("editor.layout = savedLayout; editor.selectedPageId = 'energy'; el.energySource.value = 'manual_live'; el.energyDetails.value = 'popup'; el.energyDetails.onchange()");
  assert.equal(run("'details' in selectedPage().energy"), false);
  // an unknown value is dropped on load
  run("editor.layout = { pages: [{ id: 'e', type: 'energy_dashboard', widgets: [], energy: { details: 'wide' } }] }; normalizeEnergyConfig(editor.layout.pages[0]);");
  assert.equal(run("'details' in editor.layout.pages[0].energy"), false);
});

for (const [input,key,entity] of [
  ["fEvccVehicle","evcc_vehicle_entity_id","select.demo_car"],
  ["fEvccTarget","evcc_target_soc_entity_id","number.demo_target"],
  ["fEvccPower","evcc_power_entity_id","sensor.demo_power"],
  ["fEvccSoc","evcc_soc_entity_id","sensor.demo_soc"],
  ["fEvccConnected","evcc_connected_entity_id","binary_sensor.demo_connected"],
  ["fEvccCharging","evcc_charging_entity_id","binary_sensor.demo_charging"],
  ["fEvccDisabled","evcc_disabled_entity_id","binary_sensor.demo_disabled"],
  ["fEvccEffective","evcc_effective_soc_entity_id","sensor.demo_effective"],
  ["fEvccEnergy","evcc_energy_entity_id","sensor.demo_energy"],
  ["fEvccRemaining","evcc_remaining_entity_id","sensor.demo_remaining"]
]) {
  test(`${input}: actual input event, save, reload and clear`, async () => {
    const run = setupInspector("wallbox_evcc", "select.demo_mode");
    run(`el.${input}.value=${JSON.stringify(entity)}; el.${input}.dispatch('change')`);
    assert.equal(run(`selectedWidget().${key}`), entity);
    assert.equal(run("selectedWidget().secondary_entity_id"), undefined);
    await run("el.saveBtn.onclick()");
    assert.equal(run(`savedLayout.pages[0].widgets[0].${key}`),entity);
    run("editor.layout=savedLayout; evccLoadInspector(selectedWidget())");
    assert.equal(run(`el.${input}.value`),entity);
    run(`el.${input}.value=''; el.${input}.dispatch('change')`);
    await run("el.saveBtn.onclick()"); assert.equal(run(`savedLayout.pages[0].widgets[0].${key}`),undefined);
  });
}
test("loadpoint choice replaces only with acceptance and saves the number binding", async () => {
  const run=setupInspector("wallbox_evcc","select.manual");
  run(`evccLoadpoints=[{id:'demo',entities:{entity_id:'select.demo_mode',evcc_target_soc_entity_id:'number.demo_target',evcc_soc_entity_id:'sensor.demo_soc'}}]; window.confirm=()=>false;`);
  assert.equal(run("evccChooseLoadpoint('demo')"),false);
  assert.equal(run("selectedWidget().entity_id"),'select.manual');
  run("window.confirm=()=>true"); assert.equal(run("evccChooseLoadpoint('demo')"),true);
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].entity_id"),'select.demo_mode');
  assert.equal(run("savedLayout.pages[0].widgets[0].evcc_target_soc_entity_id"),'number.demo_target');
  assert.equal(run("savedLayout.pages[0].widgets[0].evcc_loadpoint_id"),'demo');
  run("el.fEvccMode.value='select.manual_again'; el.fEvccMode.dispatch('change')");
  assert.equal(run("selectedWidget().entity_id"),'select.manual_again');
});

// Exercise the real Search click, assignment event and persistence for every extra binding.
const searchBindings = [
  ["bambu_printer", "bambu_camera", "fBambuCamera", "camera_entity_id", "camera.printer"],
  ["energy_timeline", "etl_charge", "fEtlCharge", "battery_charge_entity_id", "sensor.charge"],
  ["energy_timeline", "etl_discharge", "fEtlDischarge", "battery_discharge_entity_id", "sensor.discharge"],
  ["energy_timeline", "etl_soc", "fEtlSoc", "battery_soc_entity_id", "sensor.soc"],
  ["energy_timeline", "etl_forecast", "fEtlForecast", "forecast_today_entity_id", "sensor.forecast"],
  ["car_widget", "car_precond", "fCarPrecond", "precond_entity_id", "climate.car"],
  ["car_widget", "car_lock", "fCarLock", "lock_entity_id", "binary_sensor.car_lock"],
  ["car_widget", "car_temp", "fCarTemp", "interior_temp_entity_id", "sensor.car_temp"],
  ["vacuum_tile", "vacuum_battery", "fVacuumBattery", "battery_entity_id", "sensor.battery"],
  ["parqet_portfolio", "parqet_dividends", "fParqetDividends", "dividends_entity_id", "sensor.portfolio_dividends"],
  ["parqet_portfolio", "parqet_return", "fParqetReturn", "return_entity_id", "sensor.portfolio_return"],
  ...["Nozzle", "Bed", "Progress", "Remaining", "Stage", "Job", "Pause", "Resume", "Stop"].map((field, i) =>
    ["bambu_printer", `bambu_${field.toLowerCase()}`, `fBambu${field}`,
      ["nozzle_temp_entity_id", "bed_temp_entity_id", "progress_entity_id", "remaining_time_entity_id", "stage_entity_id", "job_name_entity_id", "pause_entity_id", "resume_entity_id", "stop_entity_id"][i],
      `${i < 6 ? "sensor" : "button"}.printer_${field.toLowerCase()}`]),
  ...["Mode", "Vehicle", "Target", "Power", "Soc", "Connected", "Charging", "Disabled", "Effective", "Energy", "Remaining"].map((field, i) =>
    ["wallbox_evcc", `evcc_${field.toLowerCase()}`, `fEvcc${field}`,
      ["entity_id", "evcc_vehicle_entity_id", "evcc_target_soc_entity_id", "evcc_power_entity_id", "evcc_soc_entity_id", "evcc_connected_entity_id", "evcc_charging_entity_id", "evcc_disabled_entity_id", "evcc_effective_soc_entity_id", "evcc_energy_entity_id", "evcc_remaining_entity_id"][i],
      `${i < 2 ? "select" : i === 2 ? "number" : i >= 5 && i <= 7 ? "binary_sensor" : "sensor"}.evcc_${field.toLowerCase()}`]),
];
for (const [type, slot, field, property, id] of searchBindings) {
  test(`Search ${slot}: click, choose, save, reload and clear`, async () => {
    const primary = type === "wallbox_evcc" ? "select.mode" : type === "vacuum_tile" ? "vacuum.robot" : "sensor.main";
    const run = setupInspector(type, primary, slot);
    if (type === "wallbox_evcc") run("evccLoadInspector(selectedWidget())");
    run("searchButton.click()");
    assert.equal(run("pickerOptions.inspectorTarget.input === el." + field), true);
    assert.equal(run("assignInspectorEntity(pickerOptions.inspectorTarget, 'light.wrong')"), false);
    assert.equal(run(`assignInspectorEntity(pickerOptions.inspectorTarget, ${JSON.stringify(id)})`), true);
    assert.equal(run(`selectedWidget().${property}`), id);
    await run("el.saveBtn.onclick()");
    assert.equal(run(`savedLayout.pages[0].widgets[0].${property}`), id);
    run("editor.layout = savedLayout");
    assert.equal(run(`selectedWidget().${property}`), id);
    if (property !== "entity_id") {
      run(`el.${field}.value = ''; el.${field}.dispatch('change')`);
      await run("el.saveBtn.onclick()");
      assert.equal(run(`savedLayout.pages[0].widgets[0].${property}`), undefined);
    }
  });
}

test("Room Search appends, rejects duplicates and respects the seven-entity limit", async () => {
  const run = setupInspector("room_tile", "sensor.main", "room_entities");
  run("el.fRoomEntities.value = 'sensor.humidity'; searchButton.click()");
  assert.equal(run("assignInspectorEntity(pickerOptions.inspectorTarget, 'sensor.main')"), false);
  assert.equal(run("assignInspectorEntity(pickerOptions.inspectorTarget, 'sensor.humidity')"), false);
  assert.equal(run("assignInspectorEntity(pickerOptions.inspectorTarget, 'light.room')"), true);
  await run("el.saveBtn.onclick()");
  assert.deepEqual(Array.from(run("savedLayout.pages[0].widgets[0].room_entities")), ["sensor.humidity", "light.room"]);
  run("editor.layout = savedLayout; el.fRoomEntities.value = Array.from({length:7}, (_,i)=>'sensor.extra'+i).join('\\n')");
  assert.equal(run("inspectorEntityPickerTarget('room_entities')"), null);
  assert.equal(run("assignInspectorEntity(pickerOptions.inspectorTarget, 'light.other')"), false);
});

test("Room extra Search chooses only an existing card entity", async () => {
  const run = setupInspector("room_tile", "sensor.main", "room_extra");
  run("el.fRoomEntities.value = 'cover.room'; el.fRoomExtraMode.value = 'cover'; searchButton.click()");
  assert.equal(run("assignInspectorEntity(pickerOptions.inspectorTarget, 'cover.outside')"), false);
  assert.equal(run("assignInspectorEntity(pickerOptions.inspectorTarget, 'cover.room')"), true);
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].room_extra_entity_id"), "cover.room");
});

test("Search filters mixed domains and room candidates before showing results", () => {
  const { run } = loadEditor();
  assert.deepEqual(Array.from(run(`entityPickerAllowedItems([
    {id:'number.target'}, {id:'select.target'}, {id:'sensor.other'}
  ], {allowedDomains:['number','select']}).map(item=>item.id)`)), ["number.target", "select.target"]);
  assert.deepEqual(Array.from(run(`entityPickerAllowedItems([
    {id:'cover.room'}, {id:'cover.other'}, {id:'sensor.main'}
  ], {allowedEntityIds:['cover.room','sensor.main'], excludedEntityIds:['sensor.main']}).map(item=>item.id)`)), ["cover.room"]);
});

test("Every Inspector entity input has a Search button", () => {
  const html = fs.readFileSync("components/webui/www/index.html", "utf8");
  const entityInputs = [...html.matchAll(/<input\b[^>]*id="(f[^" ]+)"[^>]*list="[^"]*Options"[^>]*>/g)].map(match=>match[1]);
  entityInputs.push("fRoomEntities", "fRoomExtraEntity");
  assert.equal(entityInputs.length, 40);
  for (const id of entityInputs) {
    const row = new RegExp(`<div class="entity-input-row">\\s*<(?:input|textarea|select)\\b[^>]*id="${id}"[^>]*>[\\s\\S]*?data-inspector-entity-target="[^"]+"[\\s\\S]*?</div>`);
    assert.match(html, row, `${id} lacks a Search button`);
  }
});

for (const key of ["grid", "solar", "export", "battery_out", "battery_in", "forecast"]) {
  test(`Energy color ${key}: edit, save, reload and reset`, async () => {
    const run = setupInspector("energy_timeline", "sensor.grid_import");
    run("renderAll = () => { editor.dirty = true; }");
    assert.equal(run(`editEnergyTimelineColor("${key}", "#123ABC")`), true);
    await run("el.saveBtn.onclick()");
    assert.equal(run(`savedLayout.pages[0].widgets[0].energy_colors.${key}`), "#123abc");
    run("editor.layout = JSON.parse(JSON.stringify(savedLayout)); normalizeLayoutWidgets(editor.layout)");
    assert.equal(run(`energyTimelinePalette(selectedWidget()).${key}`), "#123abc");
    assert.equal(run(`editEnergyTimelineColor("${key}", "#zzzzzz")`), false);
    assert.equal(run(`selectedWidget().energy_colors.${key}`), "#123abc");
    run("resetEnergyTimelineColors()"); await run("el.saveBtn.onclick()");
    assert.equal(run("savedLayout.pages[0].widgets[0].energy_colors"), undefined);
  });
}

test("Energy custom palette reaches both preview presentations and preserves original defaults", () => {
  const run = setupInspector("energy_timeline", "sensor.grid_import");
  run("selectedWidget().energy_colors = {solar:'#010203', grid:'#000000', battery_out:'#040506', forecast:'#070809'}");
  for (const variant of ["default", "nested"]) {
    run(`selectedWidget().style_variant = "${variant}"`);
    const markup = run("tilePreviewMarkup(selectedWidget())");
    for (const hex of ["#010203", "#000000", "#040506", "#070809"]) assert.ok(markup.includes(hex));
  }
  run("delete selectedWidget().energy_colors");
  assert.equal(run("energyTimelinePalette(selectedWidget()).solar_dest"), "#8bc34a");
});

test("Display page reset restores every group, saves and reloads without changing sound", async () => {
  const run = setupInspector();
  run(`
    globalThis.sentSettings = null;
    globalThis.timerCallback = null;
    setTimeout = callback => { timerCallback = callback; return 1; };
    putSettings = async payload => { sentSettings = payload; editor.settings = payload; };
    el.settingsHardwareNightEnabled.checked = false;
    el.settingsHardwareNightStart.value = "23:30"; el.settingsHardwareNightEnd.value = "05:00";
    el.settingsHardwareNightBrightness.value = "3"; el.settingsHardwareActiveBrightness.value = "30";
    el.settingsHardwareDimBrightness.value = "5"; el.settingsHardwareDimTimeout.value = "12";
    editor.appProject = 'betta-ha-panel-jc';
    el.settingsSaverMode.value = "weather"; el.settingsSaverTimeout.value = '20';
    el.settingsSaverWeatherEntity.value = 'weather.example'; el.settingsSaverWeatherMinutes.value = '60';
    el.settingsSaverWeatherBrightness.value = '13'; el.settingsSaverPagesMinutes.value = '45';
    el.settingsSaverPageInterval.value = '9'; el.settingsSaverPageWake.value = 'stay';
    el.settingsSaverPixelRefresh.checked = false;
    el.settingsHardwareAutoBrightness.checked = false; el.settingsHardwarePresenceWake.checked = false;
    el.settingsHardwareNightPresenceWake.checked = true; el.settingsHardwarePresenceSensitivity.value = '9';
    el.settingsHardwareAutoRotation.checked = true;
    el.settingsSoundEnabled.checked = true; el.settingsSoundVolume.value = '27';
    el.settingsSoundNightMute.checked = false;
    el.settingsHardwareResetBtn.onclick();
  `);
  await run("timerCallback()");
  const hardware = run("sentSettings.hardware");
  assert.equal(hardware.night_enabled, true);
  assert.equal(hardware.night_start, "22:00"); assert.equal(hardware.night_end, "06:00");
  assert.equal(hardware.night_brightness_percent, 1); assert.equal(hardware.active_brightness, 100);
  assert.equal(hardware.dim_brightness, 50); assert.equal(hardware.dim_timeout_seconds, 300);
  assert.equal(hardware.saver_mode, "disabled"); assert.equal(hardware.saver_timeout_minutes, 10);
  assert.equal(hardware.saver_weather_entity_id, ''); assert.equal(hardware.saver_weather_minutes, 30);
  assert.equal(hardware.saver_weather_brightness_percent, 5); assert.equal(hardware.saver_pages_minutes, 30);
  assert.equal(hardware.saver_page_interval_minutes, 5); assert.equal(hardware.saver_page_wake_home, true);
  assert.equal(hardware.saver_pixel_refresh, true); assert.equal(hardware.auto_rotation, false);
  assert.equal(hardware.auto_brightness, true); assert.equal(hardware.presence_wake, true);
  assert.equal(hardware.night_presence_wake, false); assert.equal(hardware.presence_sensitivity, 5);
  assert.equal(hardware.sound_enabled, true); assert.equal(hardware.sound_volume, 27);
  assert.equal(hardware.sound_night_mute, false);
  run("apiGet = async () => sentSettings; loadI18nLanguage = async () => {}; renderTimezonePresets = () => {}; renderSecretFields = () => {}; renderRestoreNotice = () => {}; resetSettingsDirty = () => {}; document.body = {dataset:{}};");
  assert.notEqual(await run("loadSettings(true)"), null, run("editor.settingsLoadError"));
  assert.equal(run("el.settingsHardwareActiveBrightness.value"), '100');
  assert.equal(run("el.settingsSaverMode.value"), 'disabled');
  assert.equal(run("el.settingsHardwarePresenceSensitivity.value"), '5');
  run("el.settingsHardwareNightEnabled.checked = false; pushHardwareLive()");
  await run("timerCallback()");
  assert.equal(run("sentSettings.hardware.night_enabled"), false);
  // Use the real settings loader to prove a saved false is not replaced by the new default.
  run("apiGet = async () => sentSettings; loadI18nLanguage = async () => {}; renderTimezonePresets = () => {}; renderSecretFields = () => {}; renderRestoreNotice = () => {}; resetSettingsDirty = () => {};");
  run("el.settingsHardwareNightEnabled.checked = true; document.body = {dataset:{}};");
  assert.notEqual(await run("loadSettings(true)"), null, run("editor.settingsLoadError"));
  assert.equal(run("el.settingsHardwareNightEnabled.checked"), false);
});

for (const [control, dependent, property] of [
  ["settingsHardwareNightEnabled", "settingsNightFields", "night_enabled"],
  ["settingsHardwarePresenceWake", "settingsPresenceFields", "presence_wake"],
]) {
  test(`Display ${property}: toggle dependent fields, preserve preferences, save and reload`, async () => {
    const run = setupInspector("graph", "sensor.temperature", "", true);
    run(`
      globalThis.sentSettings = null;
      globalThis.timerCallback = null;
      setTimeout = callback => { timerCallback = callback; return 1; };
      putSettings = async payload => { sentSettings = payload; editor.settings = payload; };
      el.settingsHardwareNightStart.value = '23:15'; el.settingsHardwareNightEnd.value = '05:45';
      el.settingsHardwareNightBrightness.value = '7';
      el.settingsHardwarePresenceSensitivity.value = '9';
      el.settingsHardwareNightPresenceWake.checked = true;
      el.${control}.checked = true; el.${control}.dispatch('change');
    `);
    assert.equal(run(`el.${dependent}.hidden`), false);
    run(`el.${control}.checked = false; el.${control}.dispatch('change')`);
    assert.equal(run(`el.${dependent}.hidden`), true);
    await run("timerCallback()");
    assert.equal(run(`sentSettings.hardware.${property}`), false);
    assert.equal(run("sentSettings.hardware.night_start"), '23:15');
    assert.equal(run("sentSettings.hardware.night_end"), '05:45');
    assert.equal(run("sentSettings.hardware.night_brightness_percent"), 7);
    assert.equal(run("sentSettings.hardware.presence_sensitivity"), 9);
    assert.equal(run("sentSettings.hardware.night_presence_wake"), true);
    run("apiGet = async () => sentSettings; loadI18nLanguage = async () => {}; renderTimezonePresets = () => {}; renderSecretFields = () => {}; renderRestoreNotice = () => {}; resetSettingsDirty = () => {}; document.body = {dataset:{}};");
    assert.notEqual(await run("loadSettings(true)"), null, run("editor.settingsLoadError"));
    assert.equal(run(`el.${dependent}.hidden`), true);
    run(`el.${control}.checked = true; el.${control}.dispatch('change')`);
    await run("timerCallback()");
    assert.equal(run(`sentSettings.hardware.${property}`), true);
    assert.equal(run(`el.${dependent}.hidden`), false);
    assert.equal(run("el.settingsHardwareNightStart.value"), '23:15');
    assert.equal(run("el.settingsHardwarePresenceSensitivity.value"), '9');
  });
}

test("Display dependencies keep night-only waking out of the daytime controls", () => {
  const run = setupInspector("graph", "sensor.temperature", "", true);
  run("el.settingsHardwareNightEnabled.checked = false; el.settingsHardwarePresenceWake.checked = true; el.settingsHardwareNightEnabled.dispatch('change')");
  assert.equal(run("el.settingsNightPresenceRow.hidden"), true);
  assert.equal(run("el.settingsPresenceFields.hidden"), false);
  run("el.settingsHardwareNightEnabled.checked = true; el.settingsHardwareNightEnabled.dispatch('change')");
  assert.equal(run("el.settingsNightPresenceRow.hidden"), false);
  run("el.settingsCameraBrightnessStatus.textContent = '42%'; el.settingsHardwareAutoBrightness.checked = false; el.settingsHardwareAutoBrightness.dispatch('change')");
  assert.equal(run("el.settingsCameraBrightnessStatus.textContent"), '');
  for (const mode of ['weather', 'pages', 'off', 'disabled']) {
    run(`el.settingsSaverMode.value = '${mode}'; el.settingsSaverMode.dispatch('change')`);
    assert.equal(run("el.settingsSaverAdvanced.hidden"), mode === 'disabled');
    assert.equal(run("el.settingsSaverWeatherFields.hidden"), mode !== 'weather');
    assert.equal(run("el.settingsSaverPagesFields.hidden"), mode !== 'pages');
  }
});

function setupBrightnessTest() {
  const run = setupInspector("graph", "sensor.temperature", "", true, true);
  run(`
    globalThis.testRequests = [];
    globalThis.testDeviceState = {active:false,kind:'active',remaining_s:0};
    globalThis.testTimers = new Map();
    globalThis.savedHardware = null;
    clearInterval = () => {};
    setInterval = () => 7;
    setTimeout = (callback, delay) => { testTimers.set(delay, callback); return delay; };
    putSettings = async payload => { savedHardware = payload.hardware; };
    fetch = async (url, options = {}) => {
      if (url !== '/api/display/test') throw new Error('Unexpected endpoint');
      const body = options.body ? JSON.parse(options.body) : null;
      testRequests.push({url, ...options, body});
      if (body?.action === 'start') testDeviceState = {active:true,kind:body.kind,remaining_s:30,percent:body.percent};
      if (body?.action === 'update' && testDeviceState.active) testDeviceState.percent = body.percent;
      if (body?.action === 'stop') testDeviceState = {...testDeviceState,active:false,remaining_s:0};
      return {ok:true,json:async()=>({...testDeviceState})};
    };
    el.settingsHardwareActiveBrightness.value = '100'; el.settingsHardwareDimBrightness.value = '50';
    el.settingsHardwareNightBrightness.value = '10'; el.settingsHardwareNightEnabled.checked = true;
    el.settingsHardwareNightStart.value = '22:00'; el.settingsHardwareNightEnd.value = '06:00';
  `);
  return run;
}

for (const [kind, suffix, value] of [['active','Active',75],['dim','Dim',25],['night','Night',0]]) {
  test(`Brightness ${kind}: real Test button, live slider, saved settings, Stop and expiration`, async () => {
    const run = setupBrightnessTest();
    await run("brightnessTestRequests");
    await run(`el.settings${suffix}BrightnessTestBtn.onclick()`);
    assert.equal(run("testDeviceState.kind"), kind);
    assert.equal(run("testDeviceState.remaining_s"), 30);
    assert.equal(run("savedHardware"), null, 'starting a test must not change the schedule/settings');
    assert.equal(run(`el.settings${suffix}BrightnessTestBtn.textContent`), 'Stop test');
    run(`testDeviceState.remaining_s = 18; el.settingsHardware${suffix}Brightness.value = '${value}'; el.settingsHardware${suffix}Brightness.dispatch('input')`);
    await run("testTimers.get(80)()");
    await run("testTimers.get(300)()");
    assert.equal(run("testDeviceState.percent"), value);
    assert.equal(run("testDeviceState.remaining_s"), 18, 'slider edits must not extend the preview');
    assert.equal(run(`savedHardware.${kind === 'night' ? 'night_brightness_percent' : kind === 'dim' ? 'dim_brightness' : 'active_brightness'}`), value);
    assert.equal(run("savedHardware.night_enabled"), true);
    assert.equal(run("savedHardware.night_start"), '22:00');
    assert.equal(run("savedHardware.night_end"), '06:00');
    await run(`el.settings${suffix}BrightnessTestBtn.onclick()`);
    assert.equal(run("testDeviceState.active"), false);
    assert.equal(run(`el.settings${suffix}BrightnessTestBtn.textContent`), 'Test');
    assert.equal(run(`el.settings${suffix}BrightnessTestStatus.hidden`), true);
    assert.ok(run("testRequests.filter(r=>r.method==='POST').every(r=>r.headers['Content-Type']==='application/json')"));
    await run(`el.settings${suffix}BrightnessTestBtn.onclick()`);
    run("testDeviceState.active = false; testDeviceState.remaining_s = 0");
    await run("refreshBrightnessTest()");
    assert.equal(run("brightnessTestState.active"), false, 'touch or device timeout ends the editor test');
    assert.equal(run(`el.settings${suffix}BrightnessTestBtn.textContent`), 'Test');
  });
}

test("Brightness test switches levels and survives a status reload without starting another test", async () => {
  const run = setupBrightnessTest();
  await run("brightnessTestRequests");
  await run("el.settingsNightBrightnessTestBtn.onclick()");
  await run("el.settingsDimBrightnessTestBtn.onclick()");
  assert.equal(run("brightnessTestState.kind"), 'dim');
  assert.equal(run("el.settingsNightBrightnessTestBtn.textContent"), 'Test');
  run("testDeviceState.remaining_s = 12; brightnessTestState = {active:false}");
  await run("refreshBrightnessTest()");
  assert.equal(run("brightnessTestState.remaining_s"), 12);
  assert.equal(run("el.settingsDimBrightnessTestBtn.textContent"), 'Stop test');
  assert.equal(run("testRequests.filter(r=>r.body?.action==='start').length"), 2);
  run("fetch = async () => {throw new Error('offline')}");
  await run("refreshBrightnessTest()");
  assert.match(run("el.settingsDimBrightnessTestStatus.textContent"), /ends the test automatically/);
  assert.equal(run("el.settingsDimBrightnessTestBtn.disabled"), false, 'Stop remains available after a poll failure');
});

test("Disabling night mode and resetting Display stop a running test without hiding its escape control", async () => {
  const run = setupBrightnessTest();
  await run("brightnessTestRequests");
  await run("el.settingsNightBrightnessTestBtn.onclick()");
  run("el.settingsHardwareNightEnabled.checked = false; el.settingsHardwareNightEnabled.dispatch('change')");
  await run("brightnessTestRequests");
  assert.equal(run("testDeviceState.active"), false);
  assert.equal(run("el.settingsNightFields.hidden"), true);
  await run("el.settingsActiveBrightnessTestBtn.onclick()");
  run("el.settingsHardwareResetBtn.onclick()");
  await run("brightnessTestRequests");
  assert.equal(run("testDeviceState.active"), false);
  assert.equal(run("el.settingsHardwareNightEnabled.checked"), true);
});

test("Brightness busy retries preserve queued slider and Stop ordering", async () => {
  const run = setupBrightnessTest();
  await run("brightnessTestRequests");
  run(`
    globalThis.realTestFetch = fetch;
    globalThis.attempts = [];
    globalThis.failedOnce = new Set();
    setTimeout = callback => { callback(); return 1; };
    fetch = async (url, options) => {
      const body = JSON.parse(options.body);
      attempts.push(body.action);
      if (!failedOnce.has(body.action)) { failedOnce.add(body.action); return {ok:false,status:503}; }
      return realTestFetch(url, options);
    };
  `);
  await run("el.settingsNightBrightnessTestBtn.onclick()");
  await run("Promise.all([requestBrightnessTest({action:'update',percent:0}), stopBrightnessTest()])");
  assert.deepEqual(Array.from(run("attempts")), ['start','start','update','update','stop','stop']);
  assert.equal(run("testDeviceState.percent"), 0);
  assert.equal(run("testDeviceState.active"), false);
  assert.equal(run("brightnessTestError"), '');
  assert.equal(run("savedHardware"), null);
});

for (const [status, attempts] of [[503, 3], [404, 1]]) {
  test(`Brightness ${status} failure is bounded and leaves Test available`, async () => {
    const run = setupBrightnessTest();
    await run("brightnessTestRequests");
    run(`
      globalThis.attemptCount = 0;
      setTimeout = callback => { callback(); return 1; };
      fetch = async () => { attemptCount++; return {ok:false,status:${status}}; };
    `);
    await run("el.settingsNightBrightnessTestBtn.onclick()");
    assert.equal(run("attemptCount"), attempts);
    assert.equal(run("el.settingsNightBrightnessTestBtn.disabled"), false);
    assert.equal(run("el.settingsNightBrightnessTestStatus.hidden"), false);
    assert.equal(run("savedHardware"), null);
  });
}

test("Manual OTA check sends JSON accepted by the API guard and never installs firmware", async () => {
  const run = setupInspector();
  run(`
    globalThis.requests = [];
    renderOtaUpdateCheck = () => {}; setOtaInfo = () => {};
    setTimeout = callback => { callback(); return 1; };
    fetch = async (url, options) => { requests.push({url, ...options}); return {ok: options.headers?.['Content-Type'] === 'application/json'}; };
    loadOtaStatus = async () => ({update:{state:'current', checked_at:100}});
  `);
  await run("checkForUpdateNow()");
  assert.equal(run("requests.length"), 1);
  assert.equal(run("requests[0].url"), "/api/ota/check");
  assert.equal(run("requests[0].method"), "POST");
  assert.equal(run("requests[0].body"), "{}");
  assert.equal(run("editor.ota.checkPolling"), false);
});


test("Color popup HEX and native picker events edit the model, save and reload", async () => {
  const run = setupInspector("energy_timeline", "sensor.grid_import");
  run(`
    globalThis.elements = [];
    document.createElement = tag => {
      const node = {tagName:tag, children:[], value:'', attributes:{}, listeners:{},
        setAttribute(key,value){this.attributes[key]=value;},
        setCustomValidity(value){this.validation=value;}, reportValidity(){},
        append(...items){this.children.push(...items);}, addEventListener(key,fn){this.listeners[key]=fn;},
        showModal(){this.open=true;}, close(){this.listeners.close?.();}, remove(){}
      }; elements.push(node); return node;
    };
    document.body = {append(){}};
    renderAll = () => {editor.dirty=true;};
    openEnergyTimelineColor('grid');
    globalThis.hexField = elements.find(node=>node.tagName==='input'&&node.type==='text');
    globalThis.colorField = elements.find(node=>node.tagName==='input'&&node.type==='color');
    hexField.value='#12abcd'; hexField.oninput();
  `);
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].energy_colors.grid"), "#12abcd");
  assert.equal(run("colorField.value"), "#12abcd");
  run("editor.layout = savedLayout; hexField.value='#bad'; hexField.oninput()");
  assert.equal(run("hexField.attributes['aria-invalid']"), "true");
  assert.equal(run("selectedWidget().energy_colors.grid"), "#12abcd");
  run("colorField.value='#000000'; colorField.oninput()");
  await run("el.saveBtn.onclick()");
  assert.equal(run("savedLayout.pages[0].widgets[0].energy_colors.grid"), "#000000");
  assert.equal(run("hexField.value"), "#000000");
});
