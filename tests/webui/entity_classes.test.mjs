// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723
import test from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

test("Inspector classes cover every HA class-capable platform and use domain-specific translations", () => {
  const { run, fn } = loadEditor();
  const cases = [
    ["sensor", "power", "Power", "Leistung"], ["number", "temperature", "Temperature", "Temperatur"],
    ["binary_sensor", "power", "Power on/off", "Strom an/aus"],
    ["button", "restart", "Restart", "Neustart"], ["cover", "shutter", "Shutter", "Rollladen"],
    ["event", "doorbell", "Doorbell", "Türklingel"], ["humidifier", "dehumidifier", "Dehumidifier", "Luftentfeuchter"],
    ["media_player", "speaker", "Speaker", "Lautsprecher"], ["switch", "outlet", "Outlet", "Steckdose"],
    ["update", "firmware", "Firmware", "Firmware"], ["valve", "water", "Water", "Wasser"],
  ];
  for (const [domain, deviceClass, en, de] of cases) {
    const id = `${domain}.test`;
    run(`editor.entities = [{id:${JSON.stringify(id)},device_class:${JSON.stringify(deviceClass)}}];`);
    for (const [lang, expected] of [["en", en], ["de", de]]) {
      run(`editor.i18nLanguage=${JSON.stringify(lang)};editor.i18nMap=WEB_I18N_BUILTIN[${JSON.stringify(lang)}];`);
      assert.equal(fn("entityClassChips")(id)[0].text, expected, `${domain}/${lang}`);
    }
  }
});

test("classes from live attributes fill registry omissions and keep the reported unit", () => {
  const { run, fn } = loadEditor();
  run(`editor.entities=[{id:'cover.test',device_class:'',unit:''}];
    editor.stateMeta.set('cover.test',{device_class:'blind'});`);
  assert.equal(fn("entityClassChips")("cover.test")[0].text, "Blind");
  run(`editor.entities=[{id:'sensor.test',device_class:'temperature',unit:'°C'}];
    editor.stateMeta.set('sensor.test',{device_class:'',unit:''});`);
  assert.deepEqual(Array.from(fn("entityClassChips")("sensor.test"), chip => chip.text), ["Temperature", "°C"]);
});

test("classless domains do not get a misleading none badge; unknown reported classes remain visible", () => {
  const { run, fn } = loadEditor();
  for (const domain of ["lock", "scene", "light", "fan", "climate", "input_button"]) {
    const id = `${domain}.test`;
    run(`editor.entities=[{id:${JSON.stringify(id)}}];`);
    assert.equal(fn("entityClassChips")(id).length, 0, domain);
  }
  run(`editor.entities=[{id:'cover.test'}];`);
  assert.equal(fn("entityClassChips")("cover.test")[0].none, true);
  run(`editor.entities=[{id:'custom.test',device_class:'future_class'}];`);
  assert.equal(fn("entityClassChips")("custom.test")[0].text, "future_class");
  assert.equal(fn("entityClassChips")("sensor.not_loaded").length, 0);
});

test("class-specific display choices belong to the Sensor widget and do not leak into actor widgets", () => {
  const { run, fn } = loadEditor();
  const formats = fn("sensorFormatChoices"), units = fn("sensorUnitChoices");
  const cases = [
    ["timestamp", "", ["datetime", "date", "time", "relative"], null],
    ["date", "", ["date", "relative"], null], ["duration", "s", ["text", "clock", "raw"], null],
    ["temperature", "°C", null, ["", "degree", "hide"]],
    ["temperature_delta", "°C", null, ["", "degree", "hide"]],
    ["power", "W", null, ["", "auto", "hide"]], ["energy", "kWh", null, ["", "auto", "hide"]],
    ["monetary", "EUR", null, ["", "code", "hide"]], ["humidity", "%", null, ["", "hide"]],
    ["enum", "", null, null],
  ];
  for (const [dc, unit, formatValues, unitValues] of cases) {
    run(`el.fType={value:'sensor'};editor.entities=[{id:'sensor.test',device_class:${JSON.stringify(dc)},unit:${JSON.stringify(unit)}}];`);
    assert.deepEqual(formats({type:"sensor"}, "sensor.test")?.values ? Array.from(formats({type:"sensor"}, "sensor.test").values) : null, formatValues, dc);
    assert.deepEqual(units({type:"sensor"}, "sensor.test")?.values ? Array.from(units({type:"sensor"}, "sensor.test").values) : null, unitValues, dc);
    for (const type of Array.from(run("WIDGET_NAME_TYPES")).filter(type => type !== "sensor")) {
      run(`el.fType.value=${JSON.stringify(type)};`);
      assert.equal(formats({type}, "sensor.test"), null, `${type}/${dc}`);
      assert.equal(units({type}, "sensor.test"), null, `${type}/${dc}`);
    }
  }
});

test("Sensor and gauge previews use live device-class metadata when the registry omits it", () => {
  const { run, fn } = loadEditor();
  run(`editor.entities=[{id:'sensor.duration',device_class:'',unit:''},{id:'sensor.distance'}];
    editor.stateMeta.set('sensor.duration',{device_class:'duration',unit:'s'});
    editor.stateMeta.set('sensor.distance',{device_class:'distance',unit:'m'});
    editor.states.set('sensor.duration','3725');editor.states.set('sensor.distance','2500');`);
  assert.equal(fn("previewSensorValue")({type:"sensor",entity_id:"sensor.duration",sensor_format:"clock"}), "1:02:05");
  assert.equal(fn("previewGaugeInfo")("sensor.distance"), "2.5 km");
});
