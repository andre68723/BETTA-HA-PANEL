// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
import test from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

test("Room air normalises explicit units and rejects missing/invalid measurements", () => {
  const { run } = loadEditor();
  assert.equal(run('airNormalize("temperature", "68", "°F")'), 20);
  for (const unit of ["µg/m³", "μg/m³"]) assert.equal(run(`airNormalize("pm25", "0", ${JSON.stringify(unit)})`), 0);
  for (const raw of ["", "unknown", "unavailable", "NaN", "Infinity", "-1", "0x10", " 12", "12junk"]) {
    assert.equal(run(`airNormalize("pm25", ${JSON.stringify(raw)}, "µg/m³")`), null);
  }
  assert.equal(run('airNormalize("humidity", "101", "%")'), null);
  assert.equal(run('airNormalize("co2", "1200", "ppb")'), null);
  assert.equal(run('airNormalize("voc", "400", "VOC index")'), null);
});

test("Room air preview keeps missing inputs and native ratings separate from BETTA colour", () => {
  const { run } = loadEditor();
  run(`editor.i18nMap=WEB_I18N_BUILTIN.en;
    editor.entities=[{id:'sensor.rh',device_class:'humidity',unit:'%'},{id:'sensor.co2',device_class:'carbon_dioxide',unit:'ppm'},
      {id:'sensor.voc',device_class:'volatile_organic_compounds_parts',unit:'ppb'}];
    editor.states=new Map([['sensor.rh','50'],['sensor.co2','2000'],['sensor.voc','718'],['sensor.rating','100']]);
    var widget={type:'room_air_tile',entity_id:'sensor.rh',co2_entity_id:'sensor.co2',native_rating_entity_id:'sensor.rating',rect:{w:360,h:260}};`);
  assert.equal(run("airPreviewData(widget).overall"), 2);
  run("editor.states.set('sensor.rh','unavailable')");
  assert.equal(run("airPreviewData(widget).overall"), 2);
  assert.equal(run("airPreviewData(widget).partial"), true);
  run("editor.states.set('sensor.rh','50');editor.states.set('sensor.co2','744');widget.voc_entity_id='sensor.voc';");
  assert.equal(run("airPreviewData(widget).overall"), -1);
  run("delete widget.voc_entity_id; editor.states.set('sensor.rating','poor')");
  assert.equal(run("airPreviewData(widget).overall"), 0);
  assert.ok(!run("airPreviewMarkup(widget)").includes("NaN"));
  assert.ok(run("airPreviewMarkup(widget)").includes("poor"));
});

test("Room air field events save JSON, retain disabled outdoor sources and reload defaults", () => {
  const { run } = loadEditor();
  run(`var nodes={};
    function input(value=''){return {value:String(value),checked:true,listeners:{},addEventListener(k,fn){this.listeners[k]=fn;},classList:{toggle(){}}};}
    document.getElementById=id=>nodes[id]||null;
    for(const [key,field] of AIR_ROLES) el['fAir'+field]=input();
    for(const [key,value] of Object.entries(AIR_DEFAULTS)) nodes['air_'+key]=input(value);
    nodes.airOutdoorEnabled=input();nodes.airReset=input();
    var widget={type:'room_air_tile',entity_id:'sensor.rh',outdoor_temperature_entity_id:'weather.demo',outdoor_humidity_entity_id:'weather.demo'};
    var saved='';
    autoApplyInspector=()=>{if(airSaveInspector(widget,widget.type))saved=JSON.stringify(widget);};
    airLoadInspector(widget);bindAirInspector();
    el.fAirCo2.value='sensor.co2';el.fAirCo2.listeners.change();`);
  assert.equal(run("JSON.parse(saved).co2_entity_id"), "sensor.co2");
  run("nodes.airOutdoorEnabled.checked=false;nodes.airOutdoorEnabled.listeners.change();widget=JSON.parse(saved);airLoadInspector(widget);");
  assert.equal(run("widget.air_outdoor_enabled"), false);
  assert.equal(run("el.fAirOutHumidity.value"), "weather.demo");
  run("nodes.airOutdoorEnabled.checked=true;nodes.airOutdoorEnabled.listeners.change();widget=JSON.parse(saved);airLoadInspector(widget);");
  assert.equal(run("widget.air_outdoor_enabled"), true);
  assert.equal(run("widget.outdoor_temperature_entity_id"), "weather.demo");
  run("nodes.air_co2_red.value='2500';nodes.air_co2_red.listeners.change();widget=JSON.parse(saved);airLoadInspector(widget);");
  assert.equal(run("widget.air_thresholds.co2_red"), 2500);
  run("nodes.airReset.onclick();widget=JSON.parse(saved);airLoadInspector(widget);");
  assert.equal(run("widget.air_thresholds.co2_red"), 2000);
  assert.equal(run("widget.co2_entity_id"), "sensor.co2");
  run("var before=saved;setStatus=()=>{};nodes.air_co2_red.value='500';nodes.air_co2_red.listeners.change();");
  assert.equal(run("saved===before"), true);
});

test("Humidity picker rejects percent scores and battery; PM accepts both micro spellings", () => {
  const { run } = loadEditor();
  for (const dc of ["battery", ""]) assert.equal(run(`widgetEntitySourceMatches({id:'sensor.score',device_class:${JSON.stringify(dc)},unit:'%',search_flags:1},'room_air_tile')`), false);
  assert.equal(run("widgetEntitySourceMatches({id:'sensor.rh',device_class:'humidity',unit:'%',search_flags:1},'room_air_tile')"), true);
  for (const unit of ["µg/m³", "μg/m³"]) assert.equal(run(`widgetEntitySourceMatches({id:'sensor.pm',device_class:'pm25',unit:${JSON.stringify(unit)},search_flags:1},'room_air_tile','air_pm25')`), true);
});
