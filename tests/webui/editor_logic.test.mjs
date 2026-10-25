// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// The editor's older, DOM-free logic: layout normalization before a save, ids and entity search, the
// tile previews' HTML escaping, settings helpers, the OTA variant and the translation tables.
import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadEditor } from "./load_editor.mjs";

const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), "../..");
// Values from the vm context have their own prototypes; a JSON round trip gives plain ones to compare.
const plain = (value) => JSON.parse(JSON.stringify(value));

test("layout normalization before a save", () => {
  const { fn } = loadEditor();
  const layout = {
    pages: [{
      id: "p",
      widgets: [
        { id: "v", type: "roborock_tile", rect: { x: 10, y: 10, w: 300, h: 300 } }, // upstream v0.8.2 name
        { id: "d", type: "binary_tile", display_mode: "door", rect: { x: 10, y: 10, w: 160, h: 70 } },
        { id: "b", type: "button", entity_id: "switch.x", button_mode: "play_pause", button_accent_color: "0xABCDEF" },
        { id: "g", type: "graph", graph_time_window_min: -5, graph_display_mode: "pie", graph_point_count: 500,
          graph_bar_bucket_min: 7, graph_line_color: "nope", tertiary_entity_id: "sensor.t" },
        { id: "g2", type: "graph", entity_id: "sensor.t", graph_ha_history: true },
        { id: "g3", type: "graph", entity_id: "sensor.t", graph_ha_history: "yes" },
        { id: "gauge", type: "gauge", gauge_min: 50, gauge_max: 10,
          secondary_entity_id: "sensor.extra1", tertiary_entity_id: "sensor.extra2" },
      ],
    }],
  };
  fn("normalizeLayoutWidgets")(layout);
  const [vacuum, door, button, graph, graphRecorder, graphNotBool, gauge] = layout.pages[0].widgets;
  assert.equal(vacuum.type, "vacuum_tile");
  assert.equal(door.rect.w, 180, "a door-look tile grows to the size the panel draws it");
  assert.equal(button.button_mode, "auto", "a media-only button mode needs a media player");
  assert.equal(button.button_accent_color, "#abcdef");
  assert.equal(graph.graph_time_window_min, 120);
  assert.equal(graph.graph_display_mode, "line");
  assert.equal(graph.graph_point_count, 64);
  assert.equal(graph.graph_bar_bucket_min, 15);
  assert.equal(graph.graph_line_color, "#6fe8ff");
  assert.equal(graphRecorder.graph_ha_history, true, "a graph that takes its history from Home Assistant keeps the flag");
  assert.equal(graphNotBool.graph_ha_history, undefined, "anything but true is dropped");
  assert.equal(graph.tertiary_entity_id, undefined, "graphs do not keep a tertiary entity");
  assert.equal(gauge.gauge_max, 150, "a gauge maximum below its minimum becomes minimum + 100");
  assert.equal(gauge.secondary_entity_id, "sensor.extra1");
  assert.equal(gauge.tertiary_entity_id, "sensor.extra2", "loading and saving a gauge preserves both extra readouts");
});

test("house and energy pages keep only their own configuration", () => {
  const { fn, run } = loadEditor();
  const house = { type: "house_dashboard", widgets: [{ id: "x" }], house: { rooms: [
    { name: "Bad", entity_id: "sensor.t", room_entities: ["a", "b", "c", "d", "e", "f", "g", "h", "i"] },
    { name: "no sensor" },
  ] } };
  fn("normalizeHouseConfig")(house);
  assert.equal(house.widgets.length, 0);
  assert.equal(house.house.rooms.length, 1, "a room without an entity is dropped");
  assert.equal(house.house.rooms[0].room_entities.length, 7);
  const energy = { type: "energy_dashboard", widgets: [{ id: "x" }], energy: { grid_power_entity_id: " sensor.grid " } };
  fn("normalizeEnergyConfig")(energy);
  assert.equal(energy.widgets.length, 0);
  assert.equal(energy.energy.grid_power_entity_id, "sensor.grid");
  assert.equal(energy.energy.source, run("ENERGY_SOURCE_MANUAL"), "manual sensors without a source mean the manual source");
});

test("widget ids and page ids stay unique and valid", () => {
  const { fn, run } = loadEditor();
  assert.equal(fn("sanitizeIdPart")("  Küche OG!! "), "k_che_og");
  assert.equal(fn("sanitizeIdPart")("***"), "item");
  assert.equal(fn("uniqueId")("room", [{ id: "room_1" }, { id: "room_2" }]), "room_3");
  run(`editor.layout = { pages: [{ id: "living", widgets: [{ id: "living_light_tile_1" }] }, { id: "b", widgets: [] }] };`);
  assert.equal(fn("createWidgetIdForPage")({ id: "living" }, "light_tile"), "living_light_tile_2");
});

test("entity search input and suggestions", () => {
  const { fn } = loadEditor();
  assert.deepEqual(plain(fn("parseEntitySearchInput")("light.kitchen", "")), { domain: "light", search: "kitchen" });
  assert.deepEqual(plain(fn("parseEntitySearchInput")("  kitchen ", "sensor")), { domain: "sensor", search: "kitchen" });
  assert.deepEqual(plain(fn("parseEntitySearchInput")("light.*bath", "")), { domain: "light", search: "bath" });
  const source = [
    { id: "light.kitchen", name: "Kitchen" }, { id: "light.bath", name: "Bad" },
    { id: "sensor.kitchen_temp", name: "Kitchen temperature" }, { id: "" },
  ];
  const ids = (list) => Array.from(list, (entity) => entity.id);
  assert.deepEqual(ids(fn("filterLocalEntitySuggestions")(source, "light", "kit", 10)), ["light.kitchen"]);
  assert.deepEqual(ids(fn("filterLocalEntitySuggestions")(source, "", "kitchen", 1)), ["light.kitchen"], "respects the limit");
  const rule = fn("applyEntityClassRule");
  const sensors = [
    { id: "binary_sensor.motion", device_class: "motion" }, { id: "binary_sensor.door", device_class: "door" },
    { id: "binary_sensor.unknown" },
  ];
  assert.deepEqual(ids(rule(sensors, "contact_tile", "primary")), ["binary_sensor.door"],
    "the door look requires a reported contact class");
});

test("tile previews escape titles, entity ids and states", () => {
  const { fn, run } = loadEditor();
  const evil = `"><img src=x onerror=alert(1)>`;
  const types = Array.from(run("Object.keys(WIDGET_TYPE_LABEL_KEYS)"));
  run(`editor.states = new Map([[${JSON.stringify(evil)}, ${JSON.stringify(evil)}]]);`);
  const markups = [];
  for (const type of types) {
    const widget = { id: "w", type, title: evil, entity_id: evil, secondary_entity_id: evil, tertiary_entity_id: evil,
      rect: { x: 0, y: 0, w: 400, h: 300 } };
    const markup = type === "solar_forecast" ? fn("solarPreviewMarkup")(widget)
      : type === "car_widget" ? fn("carPreviewMarkup")(widget)
        : type === "bambu_printer" ? fn("bambuPreviewMarkup")(widget)
        : type === "sonos_favorites" ? fn("sonosFavoritesPreviewMarkup")(widget)
          : fn("tilePreviewMarkup")(widget);
    if (markup) markups.push([type, markup]);
  }
  assert.ok(markups.length > 15, "previews were rendered");
  for (const [type, markup] of markups) {
    assert.ok(!markup.includes("<img"), `${type}: an HTML tag from a title or entity reached the preview`);
    assert.ok(!markup.includes(evil), `${type}: a title or entity reached the preview unescaped`);
  }
  assert.equal(fn("escapeHtml")(`<a href="x">&`), "&lt;a href=&quot;x&quot;&gt;&amp;");
});

test("a new widget goes to a free spot, or cascades when the page is full", () => {
  const { fn, run } = loadEditor({ width: 720, height: 600 });
  const spot = fn("findFreeSpot")({ widgets: [{ rect: { x: 10, y: 10, w: 200, h: 200 } }] }, 200, 200);
  assert.deepEqual(plain(spot), { x: 210, y: 10 }, "right next to the existing tile");
  const full = { widgets: [{ rect: { x: 0, y: 0, w: 720, h: 600 } }] };
  assert.deepEqual(plain(fn("findFreeSpot")(full, 200, 200)), { x: 30, y: 30 });
  run(`CANVAS_WIDTH = 720; CANVAS_HEIGHT = 600;`);
  const layout = { pages: [{ widgets: [{ rect: { x: 600, y: 0, w: 200, h: 100 } }, { rect: { x: 0, y: 0, w: 100, h: 100 } }] },
    { type: "energy_dashboard", widgets: [{ rect: { x: 9999, y: 0, w: 1, h: 1 } }] }] };
  assert.equal(fn("countWidgetsOutsideCanvas")(layout), 1);
});

test("energy preview flows balance", () => {
  const { fn } = loadEditor();
  const flows = fn("energyPreviewComputeFlows")({ from_grid_kwh: 4, to_grid_kwh: 2, solar_kwh: 10, to_battery_kwh: 3, from_battery_kwh: 1 });
  const used = flows.usedSolar + flows.usedGrid + flows.usedBattery;
  assert.equal(flows.usedTotal, 10);
  assert.ok(Math.abs(used - flows.usedTotal) < 1e-9, "home consumption is fully attributed");
  assert.ok(flows.solarToBattery + flows.solarToGrid + flows.usedSolar <= 10 + 1e-9, "no more solar used than produced");
  assert.equal(fn("energyPreviewFormatValue")(12.345, "kWh"), "12.3 kWh");
  assert.equal(fn("energyPreviewFormatValue")(123.4, "W"), "123 W");
  assert.equal(fn("energyPreviewFormatValue")("n/a", "W"), "-- W");
});

test("settings helpers", () => {
  const { fn } = loadEditor();
  const stage = fn("provisioningStageForSettings");
  assert.equal(stage({ wifi: { configured: true }, ha: { configured: true } }), null);
  assert.equal(stage({ wifi: { configured: true }, ha: { configured: false } }), "ha");
  assert.equal(stage({ wifi: { configured: true, setup_ap_active: true }, ha: { configured: true } }), "wifi");
  assert.equal(stage({ wifi: { configured: false }, ha: { configured: true } }), "wifi");

  assert.deepEqual(plain(fn("deriveTopbarSettings")({ topbar: { left_slots: ["date", "none"], stock_entity_id: "sensor.s" } })),
    { slot1: "date", slot2: "", slot3: "", weatherEntity: "", stockEntity: "sensor.s" });
  assert.deepEqual(plain(fn("deriveTopbarSettings")({ topbar: { left_slots: ["date", "weather", "stock"] } })).slot3, "stock",
    "a third left item is read");
  assert.equal(fn("deriveTopbarSettings")({}).slot1, "date", "no topbar yet (an upgrade from v0.8.2) shows the date");

  const backup = fn("settingsForBackup")({
    wifi: { ssid: "home", password: "secret-wifi" }, ha: { ws_url: "ws://ha", access_token: "secret-token" },
    mqtt: { enabled: true, password: "secret-mqtt" }, security: { admin_password: "secret-admin" },
  });
  const text = JSON.stringify(backup);
  for (const secret of ["secret-wifi", "secret-token", "secret-mqtt", "secret-admin"]) {
    assert.ok(!text.includes(secret), `a backup must not contain ${secret}`);
  }
  assert.equal(backup.weather, undefined, "the old weather/stocks topbar format is gone");

  assert.equal(fn("formatBytes")(0), "0 B");
  assert.equal(fn("formatBytes")(1536), "1.5 KB");
  assert.equal(fn("formatBytes")(5 * 1024 * 1024), "5.0 MB");
  assert.equal(fn("formatBytes")(-1), "n/a");
  assert.deepEqual(plain(fn("otaProgressVars")({ written: 512, total: 1024 })), { progress: "50", written: "512 B", total: "1.0 KB" });
  assert.equal(fn("otaProgressVars")({}).progress, "--");
});

test("stored appearance styles resolve like the firmware's ui_style_lookup()", () => {
  const { fn } = loadEditor();
  const normalize = fn("normalizeUiStyle");
  for (const style of ["flat", "glow", "glass", "matte"]) assert.equal(normalize(style), style);
  for (const style of ["removed_style", "neon", "", "Glow", undefined, null, "toString"]) {
    assert.equal(normalize(style), "flat", `unknown ${String(style)} falls back to flat`);
  }
});

test("sensor and gauge previews show the entity's value and unit", () => {
  const { fn, run } = loadEditor();
  run(`editor.entities = [{ id: "sensor.sg_load_power", unit: "W" }, { id: "sensor.t", unit: "°C" }];
       editor.states = new Map([["sensor.sg_load_power", "1250"], ["sensor.t", "unavailable"]]);`);
  const value = fn("previewEntityValue");
  assert.equal(value("sensor.sg_load_power", "23.4 °C"), "1250 W");
  assert.equal(value("sensor.t", "23.4 °C"), "-- °C", "no value yet keeps the unit");
  assert.equal(value("sensor.new", "23.4 °C"), "--", "an entity without a known unit");
  assert.equal(value("", "23.4 °C"), "23.4 °C", "the sample only without an entity");
  const markup = fn("tilePreviewMarkup")({ id: "s", type: "sensor", title: "Load", entity_id: "sensor.sg_load_power" });
  assert.match(markup, /1250 W/);
  assert.doesNotMatch(markup, /23\.4/);
});

test("device slug matches the firmware's device_slug() rules", () => {
  const { fn } = loadEditor();
  const slug = fn("deviceSlug");
  assert.equal(slug("Wohnzimmer"), "wohnzimmer");
  assert.equal(slug("Küche OG"), "kueche_og");
  assert.equal(slug("  Bad!! "), "bad");
  assert.equal(slug("Ärger-Zimmer"), "aerger_zimmer");
  assert.equal(slug("Straße"), "strasse");
  assert.equal(slug("A very long room name here"), "a_very_long_room");
  assert.equal(slug("Gäste WC"), "gaeste_wc");
});

test("the update check knows every project name the build uses", () => {
  const cmake = fs.readFileSync(path.join(ROOT, "CMakeLists.txt"), "utf8");
  const names = [...cmake.matchAll(/set\(_PROJECT_NAME "([^"]+)"\)/g)].map((m) => m[1]);
  // main/net/update_check_parse.c maps them to the release asset variants (host tests check the mapping).
  const parser = fs.readFileSync(path.join(ROOT, "main/net/update_check_parse.c"), "utf8");
  const known = [...parser.matchAll(/\{"(betta-ha-panel[^"]*)", "[^"]+"\}/g)].map((m) => m[1]);
  assert.deepEqual([...names].sort(), [...known].sort(), "CMakeLists.txt project names and update_check_parse.c differ");
});

test("theme colors snap to what the RGB565 display can show", () => {
  const { fn } = loadEditor();
  const snap = fn("themeSnapTo565");
  assert.equal(snap("#ffffff"), "#ffffff");
  assert.equal(snap("#000000"), "#000000");
  assert.equal(snap("#123456"), snap(snap("#123456")), "snapping is idempotent");
  assert.equal(snap("not a color"), "not a color");
});

test("every translation key the editor asks for exists", () => {
  const { run } = loadEditor();
  const en = run("WEB_I18N_BUILTIN.en");
  const de = run("WEB_I18N_BUILTIN.de");
  const dir = path.join(ROOT, "components/webui/www/js");
  const missing = [];
  const patterns = [
    /\bt\(\s*"([a-z][\w.]*)"/g,
    /set(?:Text|Placeholder)ById\([^,]+,\s*"([a-z][\w.]*)"/g,
    /setSelectOptionText\([^,]+,[^,]+,\s*"([a-z][\w.]*)"/g,
    /(?:titleKey|blankKey|widgetKey|itemsKey): "([\w.]+)"/g,
  ];
  for (const file of fs.readdirSync(dir).filter((name) => name.endsWith(".js") && name !== "i18n.js")) {
    const source = fs.readFileSync(path.join(dir, file), "utf8");
    for (const pattern of patterns) {
      for (const match of source.matchAll(pattern)) {
        if (!(match[1] in en)) missing.push(`${file}: ${match[1]}`);
        else if (!(match[1] in de)) missing.push(`${file}: ${match[1]} (de)`);
      }
    }
  }
  assert.deepEqual(missing, [], "t() returns the bare key for a missing one, which then shows in the UI");
  const stale = Object.keys(de).filter((key) => !(key in en));
  assert.deepEqual(stale, [], "de has keys the editor no longer uses");
});

test("a duplicated page gets new widget ids, unique across the layout", () => {
  const { run } = loadEditor();
  run(`renderAll = () => {}; setStatus = () => {};
    editor.layout = { pages: [{ id: "page_esp", title: "ESP", widgets: [
      { id: "page_esp_sensor0", type: "sensor", entity_id: "sensor.a", x: 0, y: 0, w: 200, h: 100 },
      { id: "page_esp_sensor1", type: "sensor", entity_id: "sensor.b", x: 200, y: 0, w: 200, h: 100 },
      { id: "page_esp_weather", type: "weather_tile", entity_id: "weather.home", x: 0, y: 100, w: 400, h: 200 },
    ] }] };
    duplicatePage(editor.layout.pages[0]);`);
  const pages = run("editor.layout.pages");
  assert.equal(pages.length, 2);
  const ids = pages.flatMap((page) => Array.from(page.widgets, (widget) => widget.id));
  assert.equal(new Set(ids).size, ids.length, `duplicate widget ids: ${ids.join(", ")}`);
  assert.ok(ids.every((id) => id.length < 32), "ids fit APP_MAX_WIDGET_ID_LEN");
  const [original, copy] = pages;
  assert.deepEqual(Array.from(original.widgets, (w) => w.id), ["page_esp_sensor0", "page_esp_sensor1", "page_esp_weather"],
    "the original page keeps its ids");
  assert.deepEqual(Array.from(copy.widgets, (w) => w.entity_id), ["sensor.a", "sensor.b", "weather.home"],
    "the copy keeps the tiles and their order");
});

test("a duplicated widget lands on the same page next to the original with a new id", () => {
  const { run } = loadEditor();
  run(`renderAll = () => {}; setStatus = () => {};
    editor.layout = { pages: [
      { id: "p1", title: "One", widgets: [
        { id: "p1_gauge", type: "gauge", entity_id: "sensor.load", style_variant: "horizontal",
          gauge_segment_count: 20, rect: { x: 10, y: 10, w: 200, h: 100 } },
        { id: "p1_blocker", type: "sensor", entity_id: "sensor.b", rect: { x: 220, y: 10, w: 200, h: 100 } },
      ] },
      { id: "p2", title: "Two", widgets: [] },
    ] };
    editor.selectedPageId = "p1"; editor.selectedWidgetId = "p1_gauge";
    duplicateWidget();`);
  const [first, second] = plain(run("editor.layout.pages"));
  assert.equal(second.widgets.length, 0, "nothing lands on another page");
  assert.equal(first.widgets.length, 3);
  const copy = first.widgets[1];
  assert.notEqual(copy.id, "p1_gauge");
  assert.equal(run("editor.selectedWidgetId"), copy.id, "the copy is selected");
  assert.equal(copy.style_variant, "horizontal");
  assert.equal(copy.gauge_segment_count, 20);
  // Right of the original is taken by the blocker, so the copy goes below it.
  assert.deepEqual(copy.rect, { x: 10, y: 120, w: 200, h: 100 });
  const ids = first.widgets.map((w) => w.id);
  assert.equal(new Set(ids).size, ids.length);
});

test("duplicating stops at the panel's widgets-per-page limit", () => {
  const { run } = loadEditor();
  const status = run(`renderAll = () => {}; const messages = []; setStatus = (m, err) => messages.push([m, !!err]);
    editor.layout = { pages: [{ id: "p", title: "P", widgets: Array.from({ length: MAX_WIDGETS_PER_PAGE }, (_, i) =>
      ({ id: "w" + i, type: "sensor", entity_id: "sensor.s", rect: { x: 0, y: 0, w: 100, h: 80 } })) }] };
    editor.selectedPageId = "p"; editor.selectedWidgetId = "w0";
    duplicateWidget(); messages`);
  assert.equal(run("editor.layout.pages[0].widgets.length"), 32);
  assert.equal(status.at(-1)[1], true, "reported as an error");
});


test("weather previews share condition icons and localize the low-temperature row", () => {
  const { fn, run } = loadEditor();
  run('editor.states.set("weather.test", "rainy"); editor.i18nLanguage = "en";');
  const tile = { type: "weather_tile", entity_id: "weather.test", rect: { w: 220, h: 240 } };
  const now = fn("tilePreviewMarkup")(tile);
  const forecast = fn("tilePreviewMarkup")({ ...tile, type: "weather_3day" });
  assert.ok(now.includes("🌧️") && forecast.includes("🌧️"));
  assert.ok(now.includes("Low 12°C") && now.includes("0.8 mm"));
  run('editor.i18nLanguage = "de"; editor.i18nMap = WEB_I18N_BUILTIN.de;');
  assert.ok(fn("tilePreviewMarkup")(tile).includes("Tief 12°C"));
  assert.ok(!fn("tilePreviewMarkup")({ ...tile, rect: { w: 160, h: 150 } }).includes("tp-weather-extras"));
});

test("shared name visibility preserves legacy sensors and explicit overrides", () => {
  const { fn } = loadEditor();
  const show = fn("widgetShowsName");
  assert.equal(show({type:"sensor",sensor_show_label:false}), false);
  assert.equal(show({type:"sensor",sensor_show_label:false,show_name:true}), true);
  assert.equal(show({type:"gauge",sensor_show_label:false}), true);
  const layout={pages:[{widgets:[{type:"sensor",sensor_show_label:false},
    {type:"sensor",sensor_show_label:false,show_name:true},{type:"gauge",show_name:false}]}]};
  fn("normalizeLayoutWidgets")(layout);
  assert.equal(layout.pages[0].widgets[0].show_name,false);
  assert.equal(layout.pages[0].widgets[1].show_name,undefined);
  assert.equal(layout.pages[0].widgets[2].show_name,false);
  for(const w of layout.pages[0].widgets) assert.equal(w.sensor_show_label,undefined);
});

test("weather tiles hide the place name by default and store only a deviation", () => {
  const { fn } = loadEditor();
  const show = fn("widgetShowsName");
  assert.equal(show({type:"weather_tile"}), false);
  assert.equal(show({type:"weather_3day"}), false);
  assert.equal(show({type:"weather_3day",show_name:true}), true);
  assert.equal(show({type:"light_tile"}), true);
  const layout={pages:[{widgets:[{type:"weather_tile"},{type:"weather_3day",show_name:true},
    {type:"weather_tile",show_name:false},{type:"light_tile",show_name:true}]}]};
  fn("normalizeLayoutWidgets")(layout);
  assert.equal(layout.pages[0].widgets[0].show_name,undefined);
  assert.equal(layout.pages[0].widgets[1].show_name,true);
  assert.equal(layout.pages[0].widgets[2].show_name,undefined);
  assert.equal(layout.pages[0].widgets[3].show_name,undefined);
});

test("all named widget previews hide only the display name in English and German", () => {
  const { fn,run } = loadEditor();
  run(`editor.entities=[{id:"sensor.power",unit:"W"}]; editor.states.set("sensor.power","1250");`);
  const types=Array.from(run("WIDGET_NAME_TYPES")).filter(t=>t!=="roborock_tile");
  for(const lang of ["en","de"]) {
    run(`editor.i18nMap=WEB_I18N_BUILTIN.${lang};`);
    assert.equal(fn("t")("layout.inspector.show_name"),lang==="de"?"Name anzeigen":"Show name");
    for(const type of types) {
      const w={type,id:"w",title:"NAME SENTINEL",entity_id:"sensor.power",rect:{w:400,h:300}};
      if(!fn("widgetNameDefault")(type)) w.show_name=true; // hidden unless asked for, like the weather tiles
      const renderer=type==="solar_forecast"?fn("solarPreviewMarkup"):type==="car_widget"?fn("carPreviewMarkup"):
        type==="bambu_printer"?fn("bambuPreviewMarkup"):type==="sonos_favorites"?fn("sonosFavoritesPreviewMarkup"):fn("tilePreviewMarkup");
      assert.ok(renderer(w).includes("NAME SENTINEL"),type+" shown");
      w.show_name=false;
      const hidden=renderer(w);
      assert.ok(!hidden.includes("NAME SENTINEL"),type+" hidden");
      if(type==="media_player") assert.ok(hidden.includes(lang==="de"?"Musiktitel":"Song title"));
      if(type==="weather_3day") assert.ok(hidden.includes("22°"));
      if(type==="sensor") assert.ok(hidden.includes("1250 W"));
    }
  }
});


test("the crash banner shows a stored dump until that dump is dismissed", () => {
  const { fn, run } = loadEditor();
  const banner = { hidden: true };
  const parts = { crashBannerTitle: { textContent: "" }, crashBannerHint: { textContent: "" } };
  run(`document.getElementById = (id) => id === "crashBanner" ? globalThis.__banner : globalThis.__parts[id] || null;
       const __store = {}; storageGet = (k) => __store[k] ?? null; storageSet = (k, v) => { __store[k] = v; };`);
  run("globalThis.__banner = null; globalThis.__parts = null;");
  const ctx = run("globalThis");
  ctx.__banner = banner; ctx.__parts = parts;
  const render = fn("renderCrashBanner");
  const dump = { present: true, task: "ui_watchdog", pc: "0x4000abcd", mcause: 7, mcause_name: "store fault" };
  render({ present: false });
  assert.equal(banner.hidden, true);                       // no dump, no banner
  render(dump);
  assert.equal(banner.hidden, false);
  assert.ok(parts.crashBannerHint.textContent.includes("ui_watchdog"));
  assert.ok(parts.crashBannerHint.textContent.includes("store fault"));
  run(`storageSet("betta.crashBannerDismissed", crashDumpId(${JSON.stringify(dump)}))`);
  render(dump);
  assert.equal(banner.hidden, true);                       // dismissed
  render({ ...dump, pc: "0x4000ffff" });
  assert.equal(banner.hidden, false);                      // a different dump shows it again
});

test("forecast readability is opt-in and keeps saved geometry and rain settings", () => {
  const { fn, run } = loadEditor({ width: 720, height: 600 });
  const original = { type: "weather_3day", rect: { x: 0, y: 0, w: 540, h: 220 }, show_precipitation: false };
  assert.equal(fn("forecastPreviewCount")(original), 6);
  assert.equal(fn("forecastPreviewCount")({ ...original, forecast_days: 3 }), 3);
  assert.equal(fn("forecastPreviewCount")({ ...original, forecast_days: 0 }), 3);
  run('document.getElementById = (id) => id === "fForecastDays" ? { value: "" } : { checked: false }');
  fn("saveReadabilityFields")(original);
  assert.equal(original.forecast_days, undefined);
  assert.equal(original.show_precipitation, false);
  assert.deepEqual(original.rect, { x: 0, y: 0, w: 540, h: 220 });
  run('document.getElementById = (id) => id === "fForecastDays" ? { value: "2" } : { checked: true }');
  const solar = { type: "solar_forecast" };
  fn("saveReadabilityFields")(solar);
  assert.equal(solar.forecast_days, 2);
  assert.equal(solar.align_forecast, true);
  assert.equal(fn("forecastPreviewCount")({ ...solar, forecast_days: 6 }, 5), 5);
});

test("unsaved changes are shown only when the layout differs from the loaded or saved one", () => {
  const { run } = loadEditor();
  run(`
    editor.layout = defaultLayout();
    normalizeLayoutWidgets(editor.layout);
    clearDirty();
  `);
  assert.equal(run("editor.dirty"), false, "a loaded layout is saved");
  run("markDirty()");
  assert.equal(run("editor.dirty"), false, "a render that changed nothing (page or widget selected) is not an edit");
  run(`editor.layout.pages[0].title = "Changed"; markDirty();`);
  assert.equal(run("editor.dirty"), true, "a real edit is unsaved");
  run(`editor.layout.pages[0].title = ${JSON.stringify("")}; `);
  run("clearDirty()");
  assert.equal(run("editor.dirty"), false, "saving clears it");
  run("markDirty()");
  assert.equal(run("editor.dirty"), false);
});


test("compact navigation asks for a distinct icon per page once the bar shows icons", () => {
  const { run } = loadEditor();
  const issue = (pages, index, full) => run(`navIconIssue({ pages: ${JSON.stringify(pages)} }, ${index}, ${full})`);
  const page = (id, icon) => (icon === undefined ? { id, title: id, widgets: [] } : { id, title: id, icon, widgets: [] });
  const five = [page("a"), page("b", "energy"), page("c", "house"), page("d", "bed"), page("e", "music")];
  assert.equal(issue(five, 1, 5), "", "within the full-tab range nothing is asked for");
  const six = [...five, page("f")];
  assert.equal(issue(six, 0, 5), "", "the first page's automatic room icon is an icon");
  assert.equal(issue(six, 5, 5), "missing", "a sixth page without an icon would show its number");
  const dup = [page("a"), page("b", "energy"), page("c", "energy"), page("d", "bed"), page("e", "music"), page("f", "none")];
  assert.equal(issue(dup, 1, 5), "shared");
  assert.equal(issue(dup, 2, 5), "shared");
  assert.equal(issue(dup, 3, 5), "");
  assert.equal(issue(dup, 5, 5), "missing", "none is no icon");
  const roomTwice = [page("a"), page("b", "room"), page("c", "energy"), page("d", "bed"), page("e", "music"), page("f", "house")];
  assert.equal(issue(roomTwice, 0, 5), "shared", "the automatic room icon of the first page collides with an explicit one");
  assert.equal(run("maxPages()"), run("CANVAS_WIDTH >= 1000 ? 12 : 8"));
});

test("a gauge hides its name when it is too narrow for it", () => {
  const { fn } = loadEditor();
  const auto = fn("autoGaugeName");
  const narrow = { type: "gauge", title: "Sungrow Heat", rect: { w: 80 } };
  auto(narrow);
  assert.equal(narrow.show_name, false);
  narrow.rect.w = 160; // wide enough again: back to the default, nothing stored
  auto(narrow);
  assert.equal("show_name" in narrow, false);
  const other = { type: "sensor", title: "A very long sensor title", rect: { w: 60 } };
  auto(other);
  assert.equal("show_name" in other, false, "only gauges are touched");
});
