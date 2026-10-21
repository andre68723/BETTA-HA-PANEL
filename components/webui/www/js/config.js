/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Constants: canvas, defaults, the entity picker configurations
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
const GRID = 10;
// The largest layout the panel stores (APP_LAYOUT_MAX_JSON_LEN in main/app_config.h): the default, replaced by
// /api/version's layout_max_bytes in applyCanvasGeometry() (the same 32 KB on every board).
let MAX_LAYOUT_BYTES = 32768;
// Canvas pixel dimensions default to the 4" panel (720x600 content area).
// applyCanvasGeometry() below overrides them at runtime with the values
// returned by /api/version, so the same editor works on both the 4"
// and 10.1" firmware variants.
let CANVAS_WIDTH = 720;
let CANVAS_HEIGHT = 600;
const MIN_WIDGET_SIZE = 60;
// The most widgets one page may hold (APP_MAX_WIDGETS_PER_PAGE in main/app_config.h).
const MAX_WIDGETS_PER_PAGE = 32;
// Appearance styles: the same IDs, order and rules as main/ui/theme/ui_style_registry.c, compared by
// tools/check_web.py. A new device starts with the default; an unknown ID, also one of a removed style,
// falls back to flat.
const UI_STYLES = ["flat", "glow", "glass", "matte"];
const UI_STYLE_DEFAULT = "glow";
// Chime volume of a new installation; equals out->sound_volume in runtime_settings_set_defaults().
const DEFAULT_SOUND_VOLUME = 60;
const UI_STYLE_FALLBACK = "flat";
const DEFAULT_SLIDER_DIRECTION = "auto";
const DEFAULT_BUTTON_MODE = "auto";
const DEFAULT_BUTTON_ACCENT_COLOR = "#6fe8ff";
const DEFAULT_SLIDER_ACCENT_COLOR = "#6fe8ff";
const DEFAULT_GRAPH_LINE_COLOR = "#6fe8ff";
const DEFAULT_GRAPH_TIME_WINDOW_MIN = 120;
// A graph that takes its history from Home Assistant shows the whole 24 hours the Recorder delivers; with the 2 h default it
// would draw a few isolated points.
const RECORDER_GRAPH_TIME_WINDOW_MIN = 1440;
// Linear gauge LED blocks, GAUGE_SEGMENT_COUNT_* in main/layout/widget_types.h.
// Gauge looks (style_variant), validated by main/layout/layout_validate.c.
const GAUGE_STYLES = ["linear", "horizontal", "radial"];
const GAUGE_SEGMENTS_MIN = 4;
const GAUGE_SEGMENTS_MAX = 30;
const GAUGE_SEGMENTS_DEFAULT = 10;
const GRAPH_POINTS_MIN = 16;
const GRAPH_POINTS_MAX = 64;
const GRAPH_TIME_WINDOW_MIN = 1;
const GRAPH_TIME_WINDOW_MAX = 1440;
const GRAPH_DISPLAY_MODES = ["line", "line_smooth_points", "line_smooth", "bars"];
const DEFAULT_GRAPH_DISPLAY_MODE = "line";
const GRAPH_BAR_BUCKET_MIN_OPTIONS = [5, 10, 15, 30];
const DEFAULT_GRAPH_BAR_BUCKET_MIN = 15;
const SOLAR_FORECAST_BAR_ORIENTATIONS = ["horizontal", "vertical"];
const DEFAULT_SOLAR_FORECAST_BAR_ORIENTATION = "horizontal";
const DEFAULT_SOLCAST_ENTITIES = {
  remaining: "sensor.solcast_pv_forecast_prognose_verbleibende_leistung_heute",
  today: "sensor.solcast_pv_forecast_prognose_heute",
  tomorrow: "sensor.solcast_pv_forecast_prognose_morgen",
  day3: "sensor.solcast_pv_forecast_prognose_tag_3",
  day4: "sensor.solcast_pv_forecast_prognose_tag_4",
  day5: "sensor.solcast_pv_forecast_prognose_tag_5",
};
const DEFAULT_SONOS_FAVORITES_SENSOR = "sensor.sonos_favorites";
const ENERGY_PAGE_TYPE = "energy_dashboard";
const ENERGY_SOURCE_HA = "ha_energy";
const ENERGY_SOURCE_MANUAL = "manual_live";
const ENERGY_SOURCES = new Set([ENERGY_SOURCE_HA, ENERGY_SOURCE_MANUAL]);
// Where the seven-day details of a node appear: a popup (the default, not stored) or a right-hand panel on wide displays.
const ENERGY_DETAILS_SIDE = "side";
const ENERGY_PREVIEW_COLORS = {
  grid: "#039bef",
  solar: "#ff9800",
  battery: "#26a69a",
  idle: "#435566",
};

const PAGE_ICONS = [
  { key: "room", emoji: "🚪" },
  { key: "floorplan", emoji: "📐" },
  { key: "house", emoji: "🏠" },
  { key: "energy", emoji: "⚡" },
  { key: "dashboard", emoji: "📊" },
  { key: "bed", emoji: "🛏️" },
  { key: "weather", emoji: "⛅" },
  { key: "music", emoji: "🎵" },
  { key: "climate", emoji: "🌡️" },
  { key: "sofa", emoji: "🛋️" },
  { key: "tv", emoji: "📺" },
  { key: "garage", emoji: "🚗" },
  { key: "light", emoji: "💡" },
];

// Effective navigation icon of a page (mirrors the firmware): own choice, else room for the first page, house / energy for those pages.
function pageIconKey(page) {
  const own = typeof page.icon === "string" ? page.icon : "";
  if (own === "none") return "";
  if (own) return own;
  if (isEnergyPage(page)) return "energy";
  if (isHousePage(page)) return "house";
  return editor.layout.pages[0] === page ? "room" : "";
}

function pageIconEmoji(page) {
  const entry = PAGE_ICONS.find((item) => item.key === pageIconKey(page));
  return entry ? entry.emoji : "";
}

const ENERGY_ENTITY_KEYS = [
  "home_power_entity_id",
  "solar_power_entity_id",
  "grid_power_entity_id",
  "grid_import_power_entity_id",
  "grid_export_power_entity_id",
  "battery_power_entity_id",
  "battery_charge_power_entity_id",
  "battery_discharge_power_entity_id",
  "battery_soc_entity_id",
  "gas_entity_id",
  "water_entity_id",
];
const ENTITY_AUTOCOMPLETE_DEBOUNCE_MS = 220;
const ENTITY_AUTOCOMPLETE_MAX_ITEMS = 24;
const LIGHT_ENTITY_PICKER_POLL_MS = 700;
const LIGHT_ENTITY_PICKER_MAX_POLLS = 90;
const ENTITY_PICKER_SEARCH_DEBOUNCE_MS = 350;
// Page limit of the connected panel (APP_MAX_PAGES in main/app_config.h): 12 on the 10", 8 on the 4"; /api/version's
// max_pages replaces the guess once it is loaded.
function maxPages() {
  return editor.appMaxPages > 0 ? editor.appMaxPages : (CANVAS_WIDTH >= 1000 ? 12 : CANVAS_WIDTH >= 700 ? 8 : 5);
}
// Pages with icon and title tabs (nav_layout_full_pages in firmware); beyond it the bottom bar shows icons.
function fullNavPages() {
  return editor.appFullNavPages > 0 ? editor.appFullNavPages : (CANVAS_WIDTH >= 1000 ? 10 : 5);
}
const ENTITY_PICKER_CONFIGS = {
  sensor: {
    domain: "sensor",
    titleKey: "entity_picker.title_sensor",
    blankKey: "entity_picker.blank_sensor",
    widgetKey: "entity_picker.widget_sensor",
    itemsKey: "entity_picker.items_sensor",
    titleFallback: "Choose Sensor",
    blankFallback: "Blank Sensor Tile",
    widgetFallback: "Sensor tile",
    itemsFallback: "sensors",
    minSearch: 2,
    liveSearch: false,
  },
  light_tile: {
    domain: "light",
    titleKey: "entity_picker.title_light",
    blankKey: "entity_picker.blank_light",
    widgetKey: "entity_picker.widget_light",
    itemsKey: "entity_picker.items_light",
    titleFallback: "Choose Light",
    blankFallback: "Blank Light Tile",
    widgetFallback: "Light tile",
    itemsFallback: "lights",
  },
  button: {
    domain: "switch",
    titleKey: "entity_picker.title_switch",
    blankKey: "entity_picker.blank_button",
    widgetKey: "entity_picker.widget_button",
    itemsKey: "entity_picker.items_switch",
    titleFallback: "Choose Switch",
    blankFallback: "Blank Button Tile",
    widgetFallback: "Button tile",
    itemsFallback: "switches",
  },
  heating_tile: {
    domain: "climate",
    titleKey: "entity_picker.title_heating",
    blankKey: "entity_picker.blank_heating",
    widgetKey: "entity_picker.widget_heating",
    itemsKey: "entity_picker.items_heating",
    titleFallback: "Choose Heating",
    blankFallback: "Blank Heating Tile",
    widgetFallback: "Heating tile",
    itemsFallback: "climate entities",
  },
  weather_tile: {
    domain: "weather",
    titleKey: "entity_picker.title_weather",
    blankKey: "entity_picker.blank_weather",
    widgetKey: "entity_picker.widget_weather",
    itemsKey: "entity_picker.items_weather",
    titleFallback: "Choose Weather",
    blankFallback: "Blank Weather Tile",
    widgetFallback: "Weather tile",
    itemsFallback: "weather entities",
  },
  weather_3day: {
    domain: "weather",
    titleKey: "entity_picker.title_weather",
    blankKey: "entity_picker.blank_weather_3day",
    widgetKey: "entity_picker.widget_weather_3day",
    itemsKey: "entity_picker.items_weather",
    titleFallback: "Choose Weather",
    blankFallback: "Blank Weather Forecast Tile",
    widgetFallback: "Weather Forecast tile",
    itemsFallback: "weather entities",
  },
  solar_forecast: {
    domain: "sensor",
    titleKey: "entity_picker.title_solar_forecast",
    blankKey: "entity_picker.blank_solar_forecast",
    widgetKey: "entity_picker.widget_solar_forecast",
    itemsKey: "entity_picker.items_sensor",
    titleFallback: "Choose Solar Forecast",
    blankFallback: "Blank Solar Forecast Tile",
    widgetFallback: "Solar Forecast tile",
    itemsFallback: "sensors",
    minSearch: 2,
    liveSearch: false,
  },
  car_widget: {
    domain: "sensor",
    titleKey: "entity_picker.title_car",
    blankKey: "entity_picker.blank_car",
    widgetKey: "entity_picker.widget_car",
    itemsKey: "entity_picker.items_sensor",
    titleFallback: "Choose battery sensor of the car",
    blankFallback: "Blank Car Widget",
    widgetFallback: "Car widget",
    itemsFallback: "sensors",
    minSearch: 2,
    liveSearch: false,
  },
  wallbox_evcc: {domain: "select", titleKey: "evcc.loadpoint", blankKey: "evcc.add", widgetKey: "evcc.wallbox", titleFallback: "Loadpoint", blankFallback: "Wallbox", widgetFallback: "Wallbox"},
  bambu_printer: {
    domain: "sensor",
    titleKey: "entity_picker.title_bambu",
    blankKey: "entity_picker.blank_bambu",
    widgetKey: "entity_picker.widget_bambu",
    itemsKey: "entity_picker.items_sensor",
    titleFallback: "Choose the print status sensor of the printer",
    blankFallback: "Blank Bambu printer widget",
    widgetFallback: "Bambu printer widget",
    itemsFallback: "sensors",
    minSearch: 2,
    liveSearch: false,
  },
  todo_list: {
    domain: "todo",
    titleKey: "entity_picker.title_todo",
    blankKey: "entity_picker.blank_todo",
    widgetKey: "entity_picker.widget_todo",
    itemsKey: "entity_picker.items_todo",
    titleFallback: "Choose Todo List",
    blankFallback: "Blank To-do List widget",
    widgetFallback: "To-do List widget",
    itemsFallback: "todo lists",
  },
  media_player: {
    domain: "media_player",
    titleKey: "entity_picker.title_media_player",
    blankKey: "entity_picker.blank_media_player",
    widgetKey: "entity_picker.widget_media_player",
    itemsKey: "entity_picker.items_media_player",
    titleFallback: "Choose Media Player",
    blankFallback: "Blank Media Player widget",
    widgetFallback: "Media Player widget",
    itemsFallback: "media players",
  },
  sonos_favorites: {
    domain: "media_player",
    titleKey: "entity_picker.title_sonos_favorites",
    blankKey: "entity_picker.blank_sonos_favorites",
    widgetKey: "entity_picker.widget_sonos_favorites",
    itemsKey: "entity_picker.items_media_player",
    titleFallback: "Choose Sonos Player",
    blankFallback: "Blank Sonos Favorites Tile",
    widgetFallback: "Sonos Favorites tile",
    itemsFallback: "media players",
  },
  vacuum_tile: {
    domain: "vacuum",
    titleKey: "entity_picker.title_vacuum",
    blankKey: "entity_picker.blank_vacuum",
    widgetKey: "entity_picker.widget_vacuum",
    itemsKey: "entity_picker.items_vacuum",
    titleFallback: "Choose Vacuum",
    blankFallback: "Blank Vacuum Tile",
    widgetFallback: "Vacuum tile",
    itemsFallback: "vacuum robots",
  },
  fan_tile: {
    domain: "fan",
    titleKey: "entity_picker.title_fan",
    blankKey: "entity_picker.blank_fan",
    widgetKey: "entity_picker.widget_fan",
    itemsKey: "entity_picker.items_fan",
    titleFallback: "Choose Fan",
    blankFallback: "Blank Fan Tile",
    widgetFallback: "Fan tile",
    itemsFallback: "fans",
  },
  camera_tile: {
    domain: "camera",
    titleKey: "entity_picker.title_camera",
    blankKey: "entity_picker.blank_camera",
    widgetKey: "entity_picker.widget_camera",
    itemsKey: "entity_picker.items_camera",
    titleFallback: "Choose Camera",
    blankFallback: "Blank Camera Tile",
    widgetFallback: "Camera tile",
    itemsFallback: "cameras",
  },
  contact_tile: {
    widgetType: "binary_tile",
    displayMode: "door",
    domain: "binary_sensor",
    titleKey: "entity_picker.title_contact",
    blankKey: "entity_picker.blank_contact",
    widgetKey: "entity_picker.widget_contact",
    itemsKey: "entity_picker.items_contact",
    titleFallback: "Choose Door/Window Contact",
    blankFallback: "Blank Contact Tile",
    widgetFallback: "Contact tile",
    itemsFallback: "contacts",
  },
  scene_tile: {
    domain: "scene",
    titleKey: "entity_picker.title_scene",
    blankKey: "entity_picker.blank_scene",
    widgetKey: "entity_picker.widget_scene",
    itemsKey: "entity_picker.items_scene",
    titleFallback: "Choose a scene",
    blankFallback: "Blank Scene Tile",
    widgetFallback: "Scene tile",
    itemsFallback: "scenes",
  },
  lock_tile: {
    domain: "lock",
    titleKey: "entity_picker.title_lock",
    blankKey: "entity_picker.blank_lock",
    widgetKey: "entity_picker.widget_lock",
    itemsKey: "entity_picker.items_lock",
    titleFallback: "Choose a lock",
    blankFallback: "Blank Lock widget",
    widgetFallback: "Lock widget",
    itemsFallback: "locks",
  },
  alarm_tile: {
    domain: "alarm_control_panel",
    titleKey: "entity_picker.title_alarm",
    blankKey: "entity_picker.blank_alarm",
    widgetKey: "entity_picker.widget_alarm",
    itemsKey: "entity_picker.items_alarm",
    titleFallback: "Choose an alarm panel",
    blankFallback: "Blank Alarm widget",
    widgetFallback: "Alarm widget",
    itemsFallback: "alarm panels",
  },
  stock_quotes: {
    domain: "sensor",
    titleKey: "entity_picker.title_stock_quotes",
    blankKey: "entity_picker.blank_stock_quotes",
    widgetKey: "entity_picker.widget_stock_quotes",
    itemsKey: "entity_picker.items_sensor",
    titleFallback: "Choose a quote sensor",
    blankFallback: "Blank Stocks & ETFs widget",
    widgetFallback: "Stocks & ETFs widget",
    itemsFallback: "sensors",
  },
  parqet_portfolio: {
    domain: "sensor",
    titleKey: "entity_picker.title_parqet_portfolio",
    blankKey: "entity_picker.blank_parqet_portfolio",
    widgetKey: "entity_picker.widget_parqet_portfolio",
    itemsKey: "entity_picker.items_sensor",
    titleFallback: "Choose the portfolio value sensor",
    blankFallback: "Blank Parqet Portfolio widget",
    widgetFallback: "Parqet Portfolio widget",
    itemsFallback: "sensors",
  },
  price_forecast: {
    domain: "sensor",
    titleKey: "entity_picker.title_price_forecast",
    blankKey: "entity_picker.blank_price_forecast",
    widgetKey: "entity_picker.widget_price_forecast",
    itemsKey: "entity_picker.items_sensor",
    titleFallback: "Choose the price sensor",
    blankFallback: "Blank Price Forecast widget",
    widgetFallback: "Price Forecast widget",
    itemsFallback: "sensors",
    minSearch: 2,
    liveSearch: false,
  },
  energy_timeline: {
    domain: "sensor",
    titleKey: "entity_picker.title_energy_timeline",
    blankKey: "entity_picker.blank_energy_timeline",
    widgetKey: "entity_picker.widget_energy_timeline",
    itemsKey: "entity_picker.items_sensor",
    titleFallback: "Choose the grid import energy counter",
    blankFallback: "Blank Energy timeline",
    widgetFallback: "Energy timeline",
    itemsFallback: "sensors",
    minSearch: 2,
    liveSearch: false,
  },
  select_tile: {
    domain: "select",
    titleKey: "entity_picker.title_select",
    blankKey: "entity_picker.blank_select",
    widgetKey: "entity_picker.widget_select",
    itemsKey: "entity_picker.items_select",
    titleFallback: "Choose a select entity",
    blankFallback: "Blank Select widget",
    widgetFallback: "Select widget",
    itemsFallback: "select entities",
  },
  datetime_tile: {
    domain: "input_datetime",
    titleKey: "entity_picker.title_datetime",
    blankKey: "entity_picker.blank_datetime",
    widgetKey: "entity_picker.widget_datetime",
    itemsKey: "entity_picker.items_datetime",
    titleFallback: "Choose a date or time entity",
    blankFallback: "Blank Date/Time widget",
    widgetFallback: "Date/Time widget",
    itemsFallback: "date and time entities",
  },
  script_tile: {
    widgetType: "scene_tile",
    domain: "script",
    titleKey: "entity_picker.title_script",
    blankKey: "entity_picker.blank_scene",
    widgetKey: "entity_picker.widget_scene",
    itemsKey: "entity_picker.items_script",
    titleFallback: "Choose a script",
    blankFallback: "Blank Scene Tile",
    widgetFallback: "Scene tile",
    itemsFallback: "scripts",
  },
  doorbell_tile: {
    widgetType: "binary_tile",
    domain: "event",
    titleKey: "entity_picker.title_doorbell",
    blankKey: "entity_picker.blank_binary",
    widgetKey: "entity_picker.widget_binary",
    itemsKey: "entity_picker.items_event",
    titleFallback: "Choose an event",
    blankFallback: "Blank Binary Sensor Tile",
    widgetFallback: "Binary sensor tile",
    itemsFallback: "events",
  },
  binary_tile: {
    domain: "binary_sensor",
    titleKey: "entity_picker.title_binary",
    blankKey: "entity_picker.blank_binary",
    widgetKey: "entity_picker.widget_binary",
    itemsKey: "entity_picker.items_binary",
    titleFallback: "Choose a binary sensor",
    blankFallback: "Blank Binary Sensor Tile",
    widgetFallback: "Binary sensor tile",
    itemsFallback: "binary sensors",
  },
  room_tile: {
    domain: "room",
    titleKey: "entity_picker.title_room",
    blankKey: "entity_picker.blank_room",
    widgetKey: "entity_picker.widget_room",
    itemsKey: "entity_picker.items_room",
    titleFallback: "Choose a room",
    blankFallback: "Blank Room Card",
    widgetFallback: "Room card",
    itemsFallback: "rooms",
  },
  climate_tile: {
    domain: "climate",
    titleKey: "entity_picker.title_climate",
    blankKey: "entity_picker.blank_climate",
    widgetKey: "entity_picker.widget_climate",
    itemsKey: "entity_picker.items_climate",
    titleFallback: "Choose Climate",
    blankFallback: "Blank Climate Tile",
    widgetFallback: "Climate tile",
    itemsFallback: "climate devices",
  },
  cover_tile: {
    domain: "cover",
    titleKey: "entity_picker.title_cover",
    blankKey: "entity_picker.blank_cover",
    widgetKey: "entity_picker.widget_cover",
    itemsKey: "entity_picker.items_cover",
    titleFallback: "Choose Cover",
    blankFallback: "Blank Cover Tile",
    widgetFallback: "Cover tile",
    itemsFallback: "covers",
  },
  image: {
    domain: "image",
    titleKey: "entity_picker.title_image",
    blankKey: "entity_picker.blank_image",
    widgetKey: "entity_picker.widget_image",
    itemsKey: "entity_picker.items_image",
    titleFallback: "Choose Image",
    blankFallback: "Blank Image",
    widgetFallback: "Image",
    itemsFallback: "image entities",
    minSearch: 2,
    liveSearch: false,
  },
  graph: {
    domain: "sensor",
    titleKey: "entity_picker.title_sensor",
    blankKey: "entity_picker.blank_graph",
    widgetKey: "entity_picker.widget_graph",
    itemsKey: "entity_picker.items_sensor",
    titleFallback: "Choose Sensor",
    blankFallback: "Blank Graph Tile",
    widgetFallback: "Graph tile",
    itemsFallback: "sensors",
    minSearch: 2,
    liveSearch: false,
  },
  gauge: {
    domain: "sensor",
    titleKey: "entity_picker.title_sensor",
    blankKey: "entity_picker.blank_gauge",
    widgetKey: "entity_picker.widget_gauge",
    itemsKey: "entity_picker.items_sensor",
    titleFallback: "Choose Sensor",
    blankFallback: "Blank Gauge Tile",
    widgetFallback: "Gauge tile",
    itemsFallback: "sensors",
    minSearch: 2,
    liveSearch: false,
  },
};
// Three groups, each with a heading in the navigation: the panel and how it connects, how it looks and speaks, and the
// system housekeeping.
const SETTINGS_NAV_ITEMS = [
  { group: "settings.nav.group_panel", sectionId: "settingsDeviceSection", headingId: "settingsDeviceHeading", labelKey: "settings.nav.device" },
  { group: "settings.nav.group_panel", sectionId: "settingsWifiSection", headingId: "settingsWifiHeading", labelKey: "settings.nav.network" },
  { group: "settings.nav.group_panel", sectionId: "settingsHaSection", headingId: "settingsHaHeading", labelKey: "settings.nav.ha" },
  { group: "settings.nav.group_look", sectionId: "settingsHardwareSection", headingId: "settingsHardwareHeading", labelKey: "settings.nav.display" },
  { group: "settings.nav.group_look", sectionId: "settingsThemeSection", headingId: "settingsAppearanceHeading", labelKey: "settings.nav.design" },
  { group: "settings.nav.group_look", sectionId: "settingsTopbarSection", headingId: "settingsTopbarHeading", labelKey: "settings.nav.topbar" },
  { group: "settings.nav.group_look", sectionId: "settingsSoundSection", headingId: "settingsSoundHeading", labelKey: "settings.sound.heading" },
  { group: "settings.nav.group_look", sectionId: "settingsAlertsSection", headingId: "settingsAlertsHeading", labelKey: "settings.nav.alerts" },
  { group: "settings.nav.group_system", sectionId: "settingsUiSection", headingId: "settingsLanguageHeading", labelKey: "settings.ui.heading" },
  { group: "settings.nav.group_system", sectionId: "settingsBackupSection", headingId: "settingsBackupHeading", labelKey: "settings.nav.backup" },
  { group: "settings.nav.group_system", sectionId: "settingsOtaSection", headingId: "settingsOtaHeading", labelKey: "settings.nav.system" },
];
// Common time zones for the Settings > Language & time list: the POSIX TZ string the panel stores. Anything else is entered
// as text ("custom").
const TIMEZONE_PRESETS = [
  { id: "central_europe", label: "Central Europe (Berlin, Paris, Rome, Madrid)", tz: "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { id: "uk", label: "UK, Ireland, Portugal (London, Lisbon)", tz: "GMT0BST,M3.5.0/1,M10.5.0" },
  { id: "eastern_europe", label: "Eastern Europe (Athens, Helsinki, Kyiv)", tz: "EET-2EEST,M3.5.0/3,M10.5.0/4" },
  { id: "moscow", label: "Moscow, Istanbul", tz: "MSK-3" },
  { id: "utc", label: "UTC", tz: "UTC0" },
  { id: "us_eastern", label: "US Eastern (New York)", tz: "EST5EDT,M3.2.0,M11.1.0" },
  { id: "us_central", label: "US Central (Chicago)", tz: "CST6CDT,M3.2.0,M11.1.0" },
  { id: "us_mountain", label: "US Mountain (Denver)", tz: "MST7MDT,M3.2.0,M11.1.0" },
  { id: "us_pacific", label: "US Pacific (Los Angeles)", tz: "PST8PDT,M3.2.0,M11.1.0" },
  { id: "india", label: "India (Mumbai)", tz: "IST-5:30" },
  { id: "china", label: "China, Singapore (Shanghai)", tz: "CST-8" },
  { id: "japan", label: "Japan (Tokyo)", tz: "JST-9" },
  { id: "sydney", label: "Australia East (Sydney)", tz: "AEST-10AEDT,M10.1.0,M4.1.0/3" },
  { id: "auckland", label: "New Zealand (Auckland)", tz: "NZST-12NZDT,M9.5.0,M4.1.0/3" },
];
const OTA_STATUS_POLL_MS = 900;
const DEFAULT_SLIDER_ENTITY_DOMAIN = "auto";
const SLIDER_DIRECTIONS = new Set([
  "auto",
  "left_to_right",
  "right_to_left",
  "bottom_to_top",
  "top_to_bottom",
]);
const SLIDER_ENTITY_DOMAINS = new Set([
  "auto",
  "light",
  "media_player",
  "cover",
]);
const BUTTON_MODES = new Set([
  "auto",
  "play_pause",
  "stop",
  "next",
  "previous",
]);
const LANGUAGE_CODE_RE = /^[a-z0-9][a-z0-9_-]{1,14}$/;
const DEFAULT_UI_LANGUAGE = "en";

// Widget name capability; checked against the firmware widget table.
// alarm_tile: Inspector checkbox -> action name of the layout's alarm_actions (firmware: ha_alarm_state.c)
const ALARM_ACTION_FIELDS = [
  ["fAlarmDisarm", "disarm"],
  ["fAlarmArmHome", "arm_home"],
  ["fAlarmArmAway", "arm_away"],
  ["fAlarmArmNight", "arm_night"],
  ["fAlarmArmVacation", "arm_vacation"],
  ["fAlarmArmCustom", "arm_custom_bypass"],
];
const WIDGET_NAME_TYPES = new Set(["wallbox_evcc", "sensor", "button", "slider", "gauge", "graph", "empty_tile", "light_tile", "heating_tile", "weather_tile", "weather_3day", "solar_forecast", "price_forecast", "parqet_portfolio", "stock_quotes", "car_widget", "bambu_printer", "todo_list", "media_player", "sonos_favorites", "cover_tile", "fan_tile", "contact_tile", "camera_tile", "climate_tile", "scene_tile", "select_tile", "energy_timeline", "datetime_tile", "lock_tile", "alarm_tile", "binary_tile", "room_tile", "vacuum_tile", "roborock_tile"]);
// Widget types whose name is hidden unless show_name is true: the weather tiles show the weather, not the place.
const WIDGET_NAME_HIDDEN_BY_DEFAULT = new Set(["weather_tile", "weather_3day"]);
