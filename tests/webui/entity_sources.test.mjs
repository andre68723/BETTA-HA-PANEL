// SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

const ids = list => Array.from(list, item => item.id);
const counter = { id: "sensor.import", unit: "kWh", state_class: "total_increasing", device_class: "energy" };

test("Parqet searches separate monetary amounts from percentage metrics in every binding", () => {
  const { fn, run } = loadEditor();
  const sources = [{ id: "sensor.portfolio", device_class: "monetary", unit: "EUR" },
    { id: "sensor.dividends", device_class: "monetary", unit: "EUR" },
    { id: "sensor.xirr", unit: "%" }, { id: "sensor.ttwror", unit: "%" },
    { id: "sensor.temperature", device_class: "temperature", unit: "°C" },
    { id: "sensor.energy", device_class: "energy", unit: "kWh" }, { id: "sensor.battery", unit: "%", device_class: "battery" }, { id: "sensor.humidity", unit: "%", device_class: "humidity" }, { id: "sensor.unknown" }];
  for (const slot of ["primary", "secondary", "parqet_dividends"]) {
    assert.deepEqual(ids(fn("applyEntityClassRule")(sources, "parqet_portfolio", slot)), ["sensor.portfolio", "sensor.dividends"]);
  }
  for (const slot of ["tertiary", "parqet_return"]) {
    assert.deepEqual(ids(fn("applyEntityClassRule")(sources, "parqet_portfolio", slot)), ["sensor.xirr", "sensor.ttwror"]);
  }
  for (const language of ["en", "de"]) {
    run(`editor.i18nMap=WEB_I18N_BUILTIN.${language}`);
    assert.match(fn("entityPickerFilterHint")({ domain: "sensor" }, "parqet_portfolio", "primary"), /Monetary|Geldbetrag/);
    assert.match(fn("entityPickerFilterHint")({ domain: "sensor" }, "parqet_portfolio", "tertiary"), /%/);
  }
  run(`editor.entities=${JSON.stringify(sources)}; inspectorWidgetType=()=>"parqet_portfolio"`);
  assert.deepEqual(ids(fn("primaryEntitySource")()), ["sensor.portfolio", "sensor.dividends"]);
  run("editor.entities=editor.entities.slice(2)");
  assert.deepEqual(ids(fn("primaryEntitySource")()), [], "no fallback to unrelated sensors");
});

test("energy source searches distinguish counters, battery SOC and Solcast", () => {
  const { fn } = loadEditor();
  const sources = [counter,
    { id: "sensor.power", unit: "W", state_class: "measurement", device_class: "power" },
    { id: "sensor.price", unit: "EUR/kWh", state_class: "measurement" },
    { id: "sensor.forecast", unit: "kWh", state_class: "total", has_solar_forecast: true },
    { id: "sensor.battery", unit: "%", device_class: "battery" },
    { id: "sensor.humidity", unit: "%", device_class: "humidity" }, { id: "sensor.unknown" }];
  const filter = slot => ids(fn("applyEntityClassRule")(sources, "energy_timeline", slot));
  for (const slot of ["primary", "secondary", "tertiary", "etl_charge", "etl_discharge"]) assert.deepEqual(filter(slot), ["sensor.import"]);
  assert.deepEqual(filter("etl_soc"), ["sensor.battery"]);
  assert.deepEqual(filter("etl_forecast"), ["sensor.forecast"]);
  assert.equal(fn("widgetEntitySourceMatches")({ ...counter, unit: "MWh", state_class: "total" }, "energy_timeline"), true);
  assert.equal(fn("widgetEntitySourceMatches")({ ...counter, state_class: "measurement" }, "energy_timeline"), false);
});

test("price searches require a series rather than a scalar price or a CO2 value", () => {
  const { fn, run } = loadEditor();
  const sources = [{ id: "sensor.co2", unit: "gCO2/kWh" },
    { id: "sensor.current_price", unit: "EUR/kWh" },
    { id: "sensor.price_series", unit: "EUR/kWh", has_price_series: true }];
  assert.deepEqual(ids(fn("applyEntityClassRule")(sources, "energy_price_forecast", "primary")), ["sensor.price_series"]);
  assert.deepEqual(ids(fn("applyEntityClassRule")(sources, "energy_price_forecast", "secondary")), ["sensor.current_price", "sensor.price_series"], "the optional average reference needs a scalar EUR/kWh value");
  run(`editor.entities=${JSON.stringify(sources)}; inspectorWidgetType=()=>"energy_price_forecast";`);
  assert.deepEqual(ids(fn("primaryEntitySource")()), ["sensor.price_series"]);
  run("editor.entities=editor.entities.slice(0,2)");
  assert.deepEqual(ids(fn("primaryEntitySource")()), [], "no fallback to every sensor when none matches");
  run("editor.stateMeta.set('sensor.current_price',{has_price_series:true,unit:'EUR/kWh'})");
  assert.deepEqual(ids(fn("primaryEntitySource")()), ["sensor.current_price"], "live metadata can fill a registry omission");
});

test("remote picker normalization and merging retain source metadata", () => {
  const { fn, run } = loadEditor();
  const normalized = fn("normalizeEntityPickerItems")([{ ...counter, has_price_series: false }], "sensor");
  run("renderEntityOptions=()=>{}");
  assert.equal(normalized[0].state_class, "total_increasing"); assert.equal(normalized[0].unit, "kWh");
  fn("mergeEntityPickerItemsIntoEntities")(normalized, "sensor");
  assert.equal(run("editor.entities[0].state_class"), "total_increasing");
  assert.equal(fn("widgetEntitySourceMatches")(normalized[0], "energy_timeline"), true);
});

test("the actual empty search popup still explains its source filter in both languages", () => {
  const { fn, run } = loadEditor();
  run(`
    el.lightEntityPickerRooms={innerHTML:"",appendChild(){}};
    el.lightEntityPickerStatus={textContent:""}; el.lightEntityPickerFilter={textContent:""};
    editor.lightPicker.widgetType="energy_price_forecast"; editor.lightPicker.search="CO2";
  `);
  for (const language of ["en", "de"]) {
    run(`editor.i18nMap=WEB_I18N_BUILTIN.${language}`);
    fn("renderLightEntityPicker")({ status: "ready", items: [{ id: "sensor.co2", name: "CO2", unit: "gCO2/kWh" }] });
    assert.deepEqual(ids(run("editor.lightPicker.items")), []);
    assert.match(run("el.lightEntityPickerFilter.textContent"), /sensor\.\*/);
    assert.match(run("el.lightEntityPickerFilter.textContent"), /CO₂/);
    assert.ok(run("el.lightEntityPickerStatus.textContent.length > 0"));
    const hint = fn("entityPickerFilterHint")({ domain: "sensor" }, "energy_timeline");
    assert.match(hint, /total_increasing/); assert.match(hint, /Recorder/);
  }
});

test("invalid saved bindings get a visible warning without being silently removed", () => {
  const { fn, run } = loadEditor();
  run(`el.entitySourceWarning={textContent:"",classList:{toggle(name,hidden){this.hidden=hidden}}};
    editor.entities=[{id:'sensor.co2',unit:'gCO2/kWh',has_price_series:false}];`);
  const widget = { type: "energy_price_forecast", entity_id: "sensor.co2" };
  fn("refreshEntitySourceWarning")(widget);
  assert.match(run("el.entitySourceWarning.textContent"), /Check 1 source/);
  assert.ok(run("el.entitySourceWarning.textContent.length") < 80);
  assert.equal(run("el.entitySourceWarning.classList.hidden"), false);
  assert.equal(widget.entity_id, "sensor.co2");
  run("editor.entities[0].has_price_series=true"); fn("refreshEntitySourceWarning")(widget);
  assert.equal(run("el.entitySourceWarning.classList.hidden"), true);
});

test("the popup's current binding uses the shared entity metadata reader", () => {
  const { fn, run } = loadEditor();
  run(`
    globalThis.currentName={textContent:""}; globalThis.currentValue={textContent:""};
    globalThis.currentActions={firstChild:{textContent:""}};
    el.lightEntityPickerCurrent={firstChild:{},classList:{toggle(){}},
      querySelector(selector){return selector==='strong'?currentName:selector==='.meta'?currentValue:currentActions}};
    editor.entities=[{id:'sensor.import',name:'Grid import',unit:'kWh',device_class:'energy'}];
    editor.states.set('sensor.import','1234');
  `);
  fn("renderEntityPickerCurrent")({ input: { value: "sensor.import" } });
  assert.match(run("currentName.textContent"), /Grid import/);
  assert.match(run("currentValue.textContent"), /1234 kWh/);
});

test("measurement fields reject incompatible classes and units, retaining unavailable numeric metadata", () => {
  const { fn } = loadEditor();
  const cases = [
    ["car_widget", "primary", { unit: "%", device_class: "battery" }, { unit: "%", device_class: "humidity" }],
    ["car_widget", "secondary", { unit: "kW", device_class: "power" }, { unit: "kWh", device_class: "energy" }],
    ["car_widget", "tertiary", { unit: "mi", device_class: "distance" }, { unit: "min", device_class: "duration" }],
    ["car_widget", "car_temp", { unit: "°F", device_class: "temperature" }, { unit: "%", device_class: "humidity" }],
    ["heating_tile", "secondary", { unit: "°C", device_class: "temperature" }, { unit: "W", device_class: "power" }],
    ["vacuum_tile", "vacuum_battery", { unit: "%", device_class: "battery" }, { unit: "%", device_class: "humidity" }],
    ["room_tile", "room_extra", { unit: "%", device_class: "humidity" }, { unit: "%", device_class: "battery" }],
    ["bambu_printer", "bambu_nozzle", { unit: "°C", device_class: "temperature" }, { unit: "W", device_class: "power" }],
    ["bambu_printer", "bambu_progress", { unit: "%" }, { unit: "min", device_class: "duration" }],
    ["bambu_printer", "bambu_remaining", { unit: "h", device_class: "duration" }, { unit: "%" }],
  ];
  for (const [type, slot, good, bad] of cases) {
    const sources = [{ id: "sensor.good", ...good, search_flags: 32 }, { id: "sensor.bad", ...bad, search_flags: 32 }];
    assert.deepEqual(ids(fn("applyEntityClassRule")(sources, type, slot)), ["sensor.good"], `${type}/${slot}`);
  }
  for (const type of ["graph", "gauge"]) {
    assert.deepEqual(ids(fn("applyEntityClassRule")([{ id: "sensor.raw", search_flags: 1 },
      { id: "sensor.offline", unit: "W", state_class: "measurement" }, { id: "sensor.job", device_class: "enum" },
      { id: "sensor.date", device_class: "timestamp", unit: "s" }], type, "primary")), ["sensor.raw", "sensor.offline"]);
  }
  assert.deepEqual(ids(fn("applyEntityClassRule")([{ id: "sensor.job", device_class: "enum" }], "gauge", "secondary")), ["sensor.job"], "radial extra text stays generic");
});

test("integration and attribute contracts exclude superficially similar sources", () => {
  const { fn } = loadEditor();
  for (const [type, slot, flag] of [["stock_quotes", "primary", 8], ["stock_quotes", "stock_8", 8],
    ["sonos_favorites", "primary", 16], ["sonos_favorites", "secondary", 4], ["weather_3day", "secondary", 2],
    ["bambu_printer", "primary", 160]]) {
    const sources = [{ id: "sensor.good", search_flags: flag }, { id: "sensor.fake", symbol: "FAKE", device_class: "monetary", search_flags: 1 }];
    assert.deepEqual(ids(fn("applyEntityClassRule")(sources, type, slot)), ["sensor.good"], `${type}/${slot}`);
  }
  const attrs = fn("entitySearchAttributeMetadata")("25", { items: {}, current_temperature: 21, supported_features: 4, supported_color_modes: ["onoff"] });
  assert.equal(attrs.search_flags, 69); assert.equal(attrs.supported_features, 4); assert.equal(attrs.capabilities.dimming, false);
  assert.equal(fn("entitySearchAttributeMetadata")("unknown", {}).search_flags, 0);
  assert.equal(fn("entitySearchAttributeMetadata")("off", {supported_features:1}).capabilities.dimming, true, "legacy light dimming bit");
  assert.equal(fn("entitySearchAttributeMetadata")("off", {supported_features:1,supported_color_modes:["onoff"]}).capabilities.dimming, false, "modern modes override legacy bits like the native parser");
});

test("slider eligibility requires the real dimming, volume or position capability", () => {
  const { fn } = loadEditor();
  const sources = [{ id: "light.dim", capabilities: { dimming: true } }, { id: "light.relay", capabilities: { dimming: false } },
    { id: "media_player.volume", supported_features: 4 }, { id: "media_player.play_only", supported_features: 16384 },
    { id: "cover.position", supported_features: 7 }, { id: "cover.open_close", supported_features: 3 }, { id: "light.missing" }];
  assert.deepEqual(ids(fn("applyEntityClassRule")(sources, "slider", "primary")), ["light.dim", "media_player.volume", "cover.position"]);
});

test("room candidates match the native room roles, including unknown-class exclusions", () => {
  const { fn } = loadEditor();
  const sources = [{ id: "sensor.temp", device_class: "temperature" }, { id: "sensor.humidity", device_class: "humidity" },
    { id: "sensor.energy", device_class: "energy" }, { id: "binary_sensor.motion", device_class: "motion" },
    { id: "binary_sensor.smoke", device_class: "smoke" }, { id: "sensor.unknown" }, { id: "light.room" }, { id: "scene.room" }];
  assert.deepEqual(ids(fn("applyEntityClassRule")(sources, "room_tile", "room_entities")), ["sensor.temp", "sensor.humidity", "binary_sensor.motion", "light.room"]);
  assert.equal(fn("widgetEntitySourceMatches")({ id: "climate.room", search_flags: 64 }, "room_tile"), true);
  assert.equal(fn("widgetEntitySourceMatches")({ id: "climate.room" }, "room_tile"), false);
});

test("every widget and every named entity search has translated rules, also when empty", () => {
  const { fn, run } = loadEditor();
  const primaryTypes = Array.from(run("WIDGET_NAME_TYPES"));
  assert.deepEqual(primaryTypes.filter(type => !run(`Object.hasOwn(ENTITY_SEARCH_PRIMARY,${JSON.stringify(type)})`)), []);
  const fields = [...primaryTypes.map(type => [type, "primary"]),
    ...Array.from(run("INSPECTOR_EXTRA_ENTITY_FIELDS"), ([type, slot]) => [type, slot]),
    ...["power", "battery", "gas", "water"].map(slot => ["energy_page", slot])];
  for (const type of primaryTypes) for (const slot of ["secondary", "tertiary"]) {
    if (fn(slot === "secondary" ? "secondaryEntityConfigForWidgetType" : "tertiaryEntityConfigForWidgetType")(type).enabled) fields.push([type, slot]);
  }
  for (const slot of ["solar_today", "solar_tomorrow", "solar_day_3", "solar_day_4", "solar_day_5", "solar_day_6", "solar_day_7"]) fields.push(["solar_forecast", slot]);
  fields.push(["room_tile", "room_entities"], ["room_tile", "room_extra"]);
  run(`el.lightEntityPickerRooms={innerHTML:'',appendChild(){}};
    el.lightEntityPickerStatus={textContent:''};el.lightEntityPickerFilter={textContent:''}`);
  for (const language of ["en", "de"]) {
    run(`editor.i18nMap=WEB_I18N_BUILTIN.${language}`);
    for (const [type, slot] of fields) {
      const role = fn("widgetEntitySearchRole")(type, slot);
      assert.equal(run(`Object.hasOwn(WEB_I18N_BUILTIN.${language},${JSON.stringify("entity_picker.rule_" + role)})`), true, `${language}/${type}/${slot}`);
      run(`editor.lightPicker.configOverride={domain:'sensor'};editor.lightPicker.inspectorTarget={slot:${JSON.stringify(slot)},ruleType:${JSON.stringify(type)}}`);
      fn("renderLightEntityPicker")({ status: "ready", items: [] });
      assert.match(run("el.lightEntityPickerFilter.textContent"), /sensor\.\*/);
      assert.ok(!run("el.lightEntityPickerFilter.textContent").includes("entity_picker.rule_"));
    }
  }
});

test("creation domains cover supported scene, date/time and camera sources, with deliberate aliases", () => {
  const { fn, run } = loadEditor();
  for (const type of ["sensor", "scene_tile", "datetime_tile", "camera_tile", "binary_tile", "button"]) {
    run(`editor.lightPicker.configOverride=null;editor.lightPicker.widgetType=${JSON.stringify(type)}`);
    assert.deepEqual(Array.from(fn("entityPickerConfig")().allowedDomains), Array.from(fn("allowedEntityDomainsForWidgetType")(type)), type);
  }
  assert.equal(fn("entityPickerConfigForDomain")("sensor").allowedDomains, undefined, "secondary sensor-only fields do not inherit binary_sensor from the Sensor widget");
});

test("missing cached counter metadata is pending help, not a false invalid-source warning", () => {
  const { fn, run } = loadEditor();
  run(`el.entitySourceWarning={textContent:'',classList:{toggle(name,hidden){this.hidden=hidden}}};
    el.energyTimelineHint={textContent:''};
    editor.entities=[{id:'sensor.charge',unit:'kWh',device_class:'energy'}];`);
  const widget = { type: "energy_timeline", battery_charge_entity_id: "sensor.charge" };
  fn("refreshEntitySourceWarning")(widget);
  assert.equal(run("el.entitySourceWarning.classList.hidden"), true);
  assert.match(run("el.energyTimelineHint.textContent"), /Source check incomplete/);
  assert.match(run("el.energyTimelineHint.textContent"), /Charge/);
  run("editor.entities[0].state_class='total_increasing'"); fn("refreshEntitySourceWarning")(widget);
  assert.ok(!run("el.energyTimelineHint.textContent").includes("Source check incomplete"));
  run("editor.entities[0].unit='W'"); fn("refreshEntitySourceWarning")(widget);
  assert.equal(run("el.entitySourceWarning.classList.hidden"), false);
  assert.match(run("el.energyTimelineHint.textContent"), /Check selected sources/);
});
