// SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

test("current weather accepts the optional report and preserves its hourly choice", () => {
  const { fn } = loadEditor();
  const config = fn("secondaryEntityConfigForWidgetType")("weather_tile");
  assert.equal(config.enabled, true);
  assert.equal(config.domain, "sensor");
  assert.equal(config.optional, true);
  const layout = {pages:[{id:"p",widgets:[{id:"w",type:"weather_tile",entity_id:"weather.home",
    secondary_entity_id:"sensor.dwd_report",weather_hourly:true,rect:{x:0,y:0,w:680,h:360}}]}]};
  fn("normalizeLayoutWidgets")(layout);
  assert.equal(layout.pages[0].widgets[0].secondary_entity_id,"sensor.dwd_report");
  assert.equal(layout.pages[0].widgets[0].weather_hourly,true);
  assert.equal(layout.pages[0].widgets[0].rect.w,680);
});
test("hourly weather preview retains the current header and 24 scrollable samples", () => {
  const { fn } = loadEditor();
  const widget={type:"weather_tile",entity_id:"weather.home",rect:{w:680,h:360}};
  const plain=fn("tilePreviewMarkup")(widget);
  const hourly=fn("tilePreviewMarkup")({...widget,weather_hourly:true});
  assert.ok(!plain.includes('tp-weather-hours'));
  assert.ok(hourly.includes('tp-weather-hourly-head'));
  assert.equal((hourly.match(/class="tp-weather-hour"/g)||[]).length,24);
  assert.ok(hourly.includes('10:00'));
});
