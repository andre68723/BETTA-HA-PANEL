// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// The tile previews take their sample texts from the editor language: the English editor must not show German
// words (several previews used to be German-only), and the German one shows the German samples.
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

const GERMAN = /Geoeffnet|Zuhause|Szene|Offen\b|Spielt|Licht an|Anwesend|Kuehlen|Luefter|Jetzt|Verriegelt|Vorklima|Schwenken|Natur\b|\bAN \d/;

test("previews show no German sample text in the English editor, and German ones in the German editor", () => {
  const { fn, run } = loadEditor();
  const types = Array.from(run("WIDGET_NAME_TYPES")).filter((type) => type !== "roborock_tile");
  const render = (type, rect, extra = {}) => {
    const widget = { type, id: "w", title: "T", entity_id: "x.y", rect, room_extra_mode: "cover", ...extra };
    const renderer = type === "car_widget" ? fn("carPreviewMarkup") : fn("tilePreviewMarkup");
    return renderer(widget) || "";
  };
  run(`editor.i18nMap=WEB_I18N_BUILTIN.en;`);
  for (const type of types) {
    const markup = render(type, { w: 260, h: 340 }, { lock_entity_id: "lock.a", precond_entity_id: "climate.a" });
    assert.ok(!GERMAN.test(markup), `${type}: ${markup.match(GERMAN)}`);
  }
  run(`editor.i18nMap=WEB_I18N_BUILTIN.de;`);
  assert.match(render("climate_tile", { w: 240, h: 200 }), /Heizen/);
  assert.match(render("contact_tile", { w: 180, h: 70 }), /Offen/);
});

test("the media player preview drops the cover where the panel does", () => {
  const { fn, run } = loadEditor();
  run(`editor.i18nMap=WEB_I18N_BUILTIN.en;`);
  const markup = (h) => fn("tilePreviewMarkup")({ type: "media_player", id: "m", title: "M", rect: { w: 300, h } });
  assert.ok(!markup(280).includes("tp-cover"), "300x280 has no room for a cover on the panel");
  assert.ok(markup(420).includes("tp-cover"), "300x420 does");
});

test("price preview keeps the interval on its own row and one day toggle on a narrow tile", () => {
  const { fn, run } = loadEditor();
  run("editor.i18nMap = WEB_I18N_BUILTIN.en");
  const tile = { type: "energy_price_forecast", id: "p", title: "Price", price_selector: true, rect: { w: 240, h: 150 } };
  const narrow = fn("tilePreviewMarkup")(tile);
  assert.match(narrow, /class="tp-price-slot">\d{2}:\d{2}-\d{2}:\d{2}<\/div>/);
  assert.match(narrow, /Today/);
  assert.doesNotMatch(narrow, /Tomorrow/);
  assert.match(fn("tilePreviewMarkup")({ ...tile, rect: { w: 680, h: 300 } }), /Tomorrow/);
});

test("wallbox distinguishes a guest assignment from an unplugged vehicle in both languages", () => {
  const { fn, run } = loadEditor();
  const widget = { type: "wallbox_evcc", id: "w", rect: { w: 360, h: 460 },
    evcc_vehicle_entity_id: "select.car", evcc_connected_entity_id: "binary_sensor.connected" };
  for (const [language, guest, none] of [["en", "Guest vehicle", "No vehicle"], ["de", "Gastfahrzeug", "Kein Fahrzeug"]]) {
    run(`editor.i18nMap=WEB_I18N_BUILTIN.${language}; editor.states.set('select.car','null'); editor.states.set('binary_sensor.connected','on');`);
    let markup = fn("evccPreviewMarkup")(widget);
    assert.ok(markup.includes(guest)); assert.ok(!markup.includes(none));
    run("editor.states.set('select.car','ev1'); editor.stateMeta.set('select.car',{vehicle_name:'Demo car'})");
    assert.match(fn("evccPreviewMarkup")(widget), /Demo car/);
    run("editor.states.set('binary_sensor.connected','off')");
    markup = fn("evccPreviewMarkup")(widget);
    assert.ok(markup.includes(none)); assert.ok(!markup.includes(guest));
  }
});

test("wallbox headers follow the panel's height threshold and never invent missing power", () => {
  const { fn, run } = loadEditor();
  run("editor.i18nMap = WEB_I18N_BUILTIN.en");
  const widget = { type: "wallbox_evcc", id: "w", title: "Wallbox", rect: { w: 360, h: 600 } };
  for (const header of ["power", "flow"]) {
    const height = fn("evccHeaderNeededHeight")(header);
    assert.equal(fn("evccHeaderPreview")({ ...widget, evcc_header: header, rect: { w: 360, h: height - 1 } }, null), "");
    assert.match(fn("evccHeaderPreview")({ ...widget, evcc_header: header, rect: { w: 360, h: height } }, null), /evcc-header/);
  }
  assert.match(fn("evccHeaderPreview")({ ...widget, evcc_header: "power" }, null), /-- \/ 11 kW/);
  const empty = fn("evccHeaderPreview")({ ...widget, evcc_header: "flow" }, null);
  assert.doesNotMatch(empty, /<b>|evcc-arm|<svg/); // no reading: no value, no bracket
  run(`editor.states.set('sensor.pv', '5400'); editor.stateMeta.set('sensor.pv', {unit:'W'});
       editor.states.set('sensor.battery', '-1.2'); editor.stateMeta.set('sensor.battery', {unit:'kW'});`);
  const live = fn("evccHeaderPreview")({ ...widget, evcc_header: "flow", evcc_pv_power_entity_id: "sensor.pv", evcc_battery_power_entity_id: "sensor.battery" }, 3.7);
  // one bar of the sources with the largest value and its unit; PV named above, the car and battery charge below
  assert.match(live, /<b>5\.4 kW<\/b>/);
  assert.equal((live.match(/evcc-arm above/g) || []).length, 2);
  assert.equal((live.match(/evcc-arm below/g) || []).length, 4);
  assert.match(live, /<svg[^>]*color:#43bf79/);
});
