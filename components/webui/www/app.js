/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
const GRID = 10;
// Canvas pixel dimensions default to the 4" panel (720x600 content area).
// applyCanvasGeometry() below overrides them at runtime with the values
// returned by /api/version, so the same app.js works on both the 4"
// and 10.1" firmware variants.
let CANVAS_WIDTH = 720;
let CANVAS_HEIGHT = 600;
const MIN_WIDGET_SIZE = 60;
const DEFAULT_SLIDER_DIRECTION = "auto";
const DEFAULT_BUTTON_MODE = "auto";
const DEFAULT_BUTTON_ACCENT_COLOR = "#6fe8ff";
const DEFAULT_SLIDER_ACCENT_COLOR = "#6fe8ff";
const DEFAULT_GRAPH_LINE_COLOR = "#6fe8ff";
const DEFAULT_GRAPH_TIME_WINDOW_MIN = 120;
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
const ENERGY_PAGE_TYPE = "energy_dashboard";
const ENERGY_SOURCE_HA = "ha_energy";
const ENERGY_SOURCE_MANUAL = "manual_live";
const ENERGY_SOURCES = new Set([ENERGY_SOURCE_HA, ENERGY_SOURCE_MANUAL]);
const ENERGY_PREVIEW_COLORS = {
  grid: "#039bef",
  solar: "#ff9800",
  battery: "#26a69a",
  idle: "#435566",
};

function isCompactCanvas() {
  return CANVAS_WIDTH <= 480 || CANVAS_HEIGHT <= 380;
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
];
const ENTITY_AUTOCOMPLETE_DEBOUNCE_MS = 220;
const ENTITY_AUTOCOMPLETE_MAX_ITEMS = 24;
const LIGHT_ENTITY_PICKER_POLL_MS = 700;
const LIGHT_ENTITY_PICKER_MAX_POLLS = 90;
const ENTITY_PICKER_SEARCH_DEBOUNCE_MS = 350;
const SETUP_WIZARD_PENDING_STORAGE_KEY = "betta.setupWizard.pending";
const SETUP_WIZARD_DISMISSED_STORAGE_KEY = "betta.setupWizard.dismissed";
const OTA_RELEASE_REPO = "cptkirki/BETTA-HA-PANEL";
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
    titleKey: "entity_picker.title_climate",
    blankKey: "entity_picker.blank_heating",
    widgetKey: "entity_picker.widget_heating",
    itemsKey: "entity_picker.items_climate",
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
  todo_list: {
    domain: "todo",
    titleKey: "entity_picker.title_todo",
    blankKey: "entity_picker.blank_todo",
    widgetKey: "entity_picker.widget_todo",
    itemsKey: "entity_picker.items_todo",
    titleFallback: "Choose Todo List",
    blankFallback: "Blank Todo List Tile",
    widgetFallback: "Todo List tile",
    itemsFallback: "todo lists",
  },
  media_player: {
    domain: "media_player",
    titleKey: "entity_picker.title_media_player",
    blankKey: "entity_picker.blank_media_player",
    widgetKey: "entity_picker.widget_media_player",
    itemsKey: "entity_picker.items_media_player",
    titleFallback: "Choose Media Player",
    blankFallback: "Blank Media Player Tile",
    widgetFallback: "Media Player tile",
    itemsFallback: "media players",
  },
  roborock_tile: {
    domain: "vacuum",
    titleKey: "entity_picker.title_roborock",
    blankKey: "entity_picker.blank_roborock",
    widgetKey: "entity_picker.widget_roborock",
    itemsKey: "entity_picker.items_vacuum",
    titleFallback: "Choose Roborock",
    blankFallback: "Blank Roborock Tile",
    widgetFallback: "Roborock tile",
    itemsFallback: "vacuum robots",
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
};
const SETTINGS_NAV_ITEMS = [
  { sectionId: "settingsWifiSection", headingId: "settingsWifiHeading", labelKey: "settings.wifi.heading" },
  { sectionId: "settingsHaSection", headingId: "settingsHaHeading", labelKey: "settings.ha.heading" },
  { sectionId: "settingsWeatherSection", headingId: "settingsWeatherHeading", labelKey: "settings.weather.heading" },
  { sectionId: "settingsStocksSection", headingId: "settingsStocksHeading", labelKey: "settings.stocks.heading" },
  { sectionId: "settingsHardwareSection", headingId: "settingsHardwareHeading", labelKey: "settings.hardware.heading" },
  { sectionId: "settingsTimeSection", headingId: "settingsTimeHeading", labelKey: "settings.time.heading" },
  { sectionId: "settingsUiSection", headingId: "settingsUiHeading", labelKey: "settings.ui.heading" },
  { sectionId: "settingsThemeSection", headingId: "settingsThemeHeading", labelKey: "settings.theme.heading" },
  { sectionId: "settingsApSection", headingId: "settingsApHeading", labelKey: "settings.ap.heading" },
  { sectionId: "settingsOtaSection", headingId: "settingsOtaHeading", labelKey: "settings.ota.heading" },
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

const WEB_I18N_BUILTIN = {
  en: {
    "tabs.layout": "Layout",
    "tabs.settings": "Settings",
    "sidebar.title": "BETTA Editor",
    "sidebar.subtitle": "Layout source of truth: JSON",
    "layout.pages.heading": "Pages",
    "layout.pages.add": "+ Page",
    "layout.pages.add_energy": "+ Energy Page",
    "layout.pages.delete": "Delete",
    "layout.pages.confirm_delete": "Delete page \"{name}\"? This removes all of its widgets.",
    "layout.pages.title_label": "Page title",
    "layout.pages.title_placeholder": "Page name on the display",
    "layout.pages.apply_title": "Apply page title",
    "layout.pages.new_title": "Page {number}",
    "layout.pages.energy_title": "Energy",
    "layout.energy.heading": "Energy Page",
    "layout.energy.hint": "Choose whether the page mirrors Home Assistant Energy or uses manual live sensors.",
    "layout.energy.source": "Data source",
    "layout.energy.source_ha": "Home Assistant Energy",
    "layout.energy.source_manual": "Manual live sensors",
    "layout.energy.source_hint_ha": "Uses the Energy dashboard configured in Home Assistant.",
    "layout.energy.source_hint_manual": "Expert fallback: use explicit W/kW sensors from Home Assistant.",
    "layout.energy.home_power": "Home power",
    "layout.energy.solar_power": "Solar power",
    "layout.energy.grid_power": "Grid power (signed)",
    "layout.energy.grid_import": "Grid import",
    "layout.energy.grid_export": "Grid export",
    "layout.energy.battery_power": "Battery power (signed)",
    "layout.energy.battery_charge": "Battery charge",
    "layout.energy.battery_discharge": "Battery discharge",
    "layout.energy.battery_soc": "Battery state of charge",
    "layout.energy.apply": "Apply energy config",
    "layout.energy.no_widgets": "Energy pages render a dedicated dashboard and do not use widgets.",
    "layout.energy.preview_title": "Energy distribution",
    "layout.energy.sensor_count_one": "{count} sensor",
    "layout.energy.sensor_count_many": "{count} sensors",
    "layout.energy.no_sensor": "no sensor",
    "layout.energy.preview_source_ha": "HA Energy",
    "layout.energy.preview_source_manual": "Live sensors",
    "layout.energy.preview_auto": "automatic from HA",
    "layout.energy.low_carbon": "Low-carbon",
    "layout.energy.grid": "Grid",
    "layout.energy.solar": "Solar",
    "layout.energy.gas": "Gas",
    "layout.energy.home": "Home",
    "layout.energy.battery": "Battery",
    "layout.energy.water": "Water",
    "layout.status.energy_page_only": "Energy pages do not accept widgets.",
    "layout.widgets.heading": "Widgets",
    "layout.widgets.add_sensor": "+ Sensor",
    "layout.widgets.add_button": "+ Button",
    "layout.widgets.add_slider": "+ Slider",
    "layout.widgets.add_graph": "+ Graph",
    "layout.widgets.add_empty_tile": "+ Empty Tile",
    "layout.widgets.add_light_tile": "+ Light Tile",
    "layout.widgets.add_heating_tile": "+ Heating Tile",
    "layout.widgets.add_weather_tile": "+ Weather",
    "layout.widgets.add_weather_3day": "+ Weather Forecast",
    "layout.widgets.add_solar_forecast": "+ Solar Forecast",
    "layout.widgets.add_todo": "+ Todo List",
    "layout.widgets.add_media_player": "+ Media Player",
    "layout.widgets.add_roborock": "+ Roborock",
    "layout.widgets.quick_setup": "Quick Setup",
    "layout.widgets.delete": "Delete Widget",
    "layout.widgets.confirm_delete": "Delete widget \"{name}\"?",
    "entity_picker.title": "Choose Light",
    "entity_picker.title_sensor": "Choose Sensor",
    "entity_picker.title_light": "Choose Light",
    "entity_picker.title_switch": "Choose Switch",
    "entity_picker.title_weather": "Choose Weather",
    "entity_picker.title_solar_forecast": "Choose Solar Forecast",
    "entity_picker.title_climate": "Choose Heating",
    "entity_picker.title_roborock": "Choose Roborock",
    "entity_picker.title_image": "Choose Image",
    "entity_picker.title_entity": "Choose Entity",
    "entity_picker.refresh": "Refresh",
    "entity_picker.search": "Search",
    "entity_picker.close": "Close",
    "entity_picker.search_placeholder": "Search by name, entity ID, or room",
    "entity_picker.search_hint": "Type at least {count} characters to search {items}.",
    "entity_picker.search_ready": "Press Enter or Search to query {items}.",
    "entity_picker.blank": "Blank Light Tile",
    "entity_picker.blank_sensor": "Blank Sensor Tile",
    "entity_picker.blank_light": "Blank Light Tile",
    "entity_picker.blank_button": "Blank Button Tile",
    "entity_picker.blank_weather": "Blank Weather Tile",
    "entity_picker.blank_weather_3day": "Blank Weather Forecast Tile",
    "entity_picker.blank_solar_forecast": "Blank Solar Forecast Tile",
    "entity_picker.blank_graph": "Blank Graph Tile",
    "entity_picker.blank_heating": "Blank Heating Tile",
    "entity_picker.blank_roborock": "Blank Roborock Tile",
    "entity_picker.blank_image": "Blank Image",
    "entity_picker.loading": "Loading lights...",
    "entity_picker.loading_items": "Loading {items}...",
    "entity_picker.refreshing": "Refreshing lights...",
    "entity_picker.refreshing_items": "Refreshing {items}...",
    "entity_picker.pending": "Waiting for Home Assistant...",
    "entity_picker.disconnected": "Home Assistant is not connected.",
    "entity_picker.empty": "No light entities found.",
    "entity_picker.empty_items": "No {items} found.",
    "entity_picker.truncated": "List truncated by firmware limit.",
    "entity_picker.unassigned_room": "No room",
    "entity_picker.added": "Light tile added: {entity}",
    "entity_picker.added_widget": "{widget} added: {entity}",
    "entity_picker.fetch_failed": "Light discovery failed: {error}",
    "entity_picker.fetch_failed_items": "{items} discovery failed: {error}",
    "entity_picker.progress": "{loaded} / {target}",
    "entity_picker.progress_total": "{loaded} / {target} of {total}",
    "entity_picker.items_light": "lights",
    "entity_picker.items_sensor": "sensors",
    "entity_picker.items_switch": "switches",
    "entity_picker.items_weather": "weather entities",
    "entity_picker.items_climate": "climate entities",
    "entity_picker.items_vacuum": "vacuum robots",
    "entity_picker.items_image": "image entities",
    "entity_picker.items_entity": "entities",
    "entity_picker.widget_light": "Light tile",
    "entity_picker.widget_sensor": "Sensor tile",
    "entity_picker.widget_button": "Button tile",
    "entity_picker.widget_weather": "Weather tile",
    "entity_picker.widget_weather_3day": "Weather Forecast tile",
    "entity_picker.widget_solar_forecast": "Solar Forecast tile",
    "entity_picker.widget_graph": "Graph tile",
    "entity_picker.widget_heating": "Heating tile",
    "entity_picker.widget_roborock": "Roborock tile",
    "entity_picker.widget_image": "Image",
    "entity_picker.assigned": "Entity set: {entity}",
    "layout.inspector.heading": "Inspector",
    "layout.inspector.title": "Title",
    "layout.inspector.entity": "Entity",
    "layout.inspector.solar_remaining_entity": "Remaining today (sensor)",
    "layout.inspector.solar_today": "Today forecast",
    "layout.inspector.solar_tomorrow": "Tomorrow forecast",
    "layout.inspector.solar_day_3": "Day 3 forecast",
    "layout.inspector.solar_day_4": "Day 4 forecast",
    "layout.inspector.solar_day_5": "Day 5 forecast",
    "layout.inspector.solar_bar_max": "Bar max (kWh)",
    "layout.inspector.solar_bar_orientation": "Bar orientation",
    "layout.inspector.secondary_entity": "Actual entity (sensor)",
    "layout.inspector.secondary_entity_roborock": "Map entity (image, optional)",
    "layout.inspector.button_mode": "Button mode",
    "layout.inspector.button_accent_color": "Button accent color",
    "layout.inspector.slider_entity_domain": "Slider entity type",
    "layout.inspector.slider_direction": "Slider direction",
    "layout.inspector.slider_accent_color": "Slider accent color",
    "layout.inspector.graph_line_color": "Graph line color",
    "layout.inspector.graph_time_window_min": "Time window (minutes)",
    "layout.inspector.graph_point_count": "Render points (empty = auto)",
    "layout.inspector.graph_display_mode": "Display mode",
    "layout.inspector.graph_bar_bucket_min": "Bar interval (min)",
    "layout.option.graph_display_mode.line": "Line with points",
    "layout.option.graph_display_mode.line_smooth_points": "Smooth line with points",
    "layout.option.graph_display_mode.line_smooth": "Smooth line",
    "layout.option.graph_display_mode.bars": "Bars",
    "layout.option.solar_bar_orientation.horizontal": "Horizontal",
    "layout.option.solar_bar_orientation.vertical": "Vertical",
    "layout.inspector.apply": "Apply",
    "layout.option.button_mode.auto": "auto (default switch)",
    "layout.option.button_mode.play_pause": "play/pause (media_player)",
    "layout.option.button_mode.stop": "stop (media_player)",
    "layout.option.button_mode.next": "next (media_player)",
    "layout.option.button_mode.previous": "previous (media_player)",
    "layout.option.slider_entity_domain.auto": "auto (light, media_player, cover)",
    "layout.option.slider_entity_domain.light": "light",
    "layout.option.slider_entity_domain.media_player": "media_player",
    "layout.option.slider_entity_domain.cover": "cover",
    "layout.option.slider_direction.auto": "auto (width/height based)",
    "layout.option.slider_direction.left_to_right": "left_to_right (0% -> 100%)",
    "layout.option.slider_direction.right_to_left": "right_to_left (100% -> 0%)",
    "layout.option.slider_direction.bottom_to_top": "bottom_to_top (0% -> 100%)",
    "layout.option.slider_direction.top_to_bottom": "top_to_bottom (100% -> 0%)",
    "layout.actions.heading": "Actions",
    "layout.actions.reload": "Reload",
    "layout.actions.save": "Save",
    "layout.actions.export": "Export",
    "layout.actions.import": "Import JSON",
    "layout.actions.paste_placeholder": "Paste layout JSON here",
    "layout.canvas.title": "Canvas",
    "layout.default_page.title": "Living Room",
    "layout.status.loading": "Loading layout...",
    "layout.status.load_failed": "Layout load failed, using default: {error}",
    "layout.status.loaded": "Layout loaded",
    "layout.status.entity_fetch_failed": "Entity fetch failed: {error}",
    "layout.status.saving": "Saving layout...",
    "layout.status.saved": "Layout saved",
    "layout.status.imported": "Layout imported (not saved yet)",
    "layout.status.at_least_one_page": "At least one page is required",
    "layout.status.entity_domain_required": "Entity must use domain: {domains}",
    "layout.status.expected_domain": "the expected domain",
    "layout.status.secondary_sensor_required": "Actual entity must start with sensor.",
    "layout.status.secondary_image_required": "Map entity must start with image.",
    "layout.status.invalid_json": "Invalid layout JSON",
    "layout.status.save_failed": "Save failed: {error}",
    "layout.status.import_failed": "Import failed: {error}",
    "layout.status.file_import_failed": "File import failed: {error}",
    "setup.title": "Quick Setup",
    "setup.step_ha": "HA connected",
    "setup.step_tiles": "Add tiles",
    "setup.step_save": "Save layout",
    "setup.subtitle": "Pick a few Home Assistant entities for your first dashboard.",
    "setup.page_label": "First page title",
    "setup.page_placeholder": "Living Room",
    "setup.add_light": "+ Light",
    "setup.add_heating": "+ Heating",
    "setup.add_weather": "+ Weather",
    "setup.add_button": "+ Switch",
    "setup.add_sensor": "+ Sensor",
    "setup.close": "Close",
    "setup.skip": "Skip",
    "setup.done": "Save + Done",
    "setup.save": "Save Layout",
    "setup.count_none": "No tiles added yet.",
    "setup.count_one": "1 tile on this page.",
    "setup.count_many": "{count} tiles on this page.",
    "setup.added": "Added: {title}",
    "setup.saving": "Saving layout...",
    "setup.saved": "Layout saved. The panel can use this dashboard now.",
    "setup.save_failed": "Save failed: {error}",
    "provision.wifi.title": "Wi-Fi Provisioning",
    "provision.wifi.subtitle": "Connect the panel to your Wi-Fi.",
    "provision.wifi.ssid": "SSID",
    "provision.wifi.country_code": "Country Code",
    "provision.wifi.password": "Password",
    "provision.wifi.password_placeholder": "Wi-Fi password",
    "provision.wifi.show_password": "Show password",
    "provision.ha.title": "HA Provisioning",
    "provision.ha.subtitle": "Connect the panel to Home Assistant.",
    "provision.ha.ws_url": "WebSocket URL (ws:// or wss://)",
    "provision.ha.token": "Long-lived Access Token",
    "provision.ha.show_token": "Show token",
    "settings.wifi.heading": "Wi-Fi",
    "settings.wifi.ssid": "SSID",
    "settings.wifi.country_code": "Country Code",
    "settings.wifi.bssid": "BSSID lock (optional)",
    "settings.wifi.password": "Password",
    "settings.wifi.password_placeholder": "Leave empty to keep the stored password",
    "settings.ha.heading": "Home Assistant",
    "settings.ha.ws_url": "WebSocket URL (ws:// or wss://)",
    "settings.ha.token": "Long-lived Access Token",
    "settings.ha.token_placeholder": "Leave empty to keep the stored token",
    "settings.ha.rest_fallback": "Enable HA REST fallback (Default: Off, WS-only preferred)",
    "settings.weather.heading": "Weather",
    "settings.weather.topbar_enabled": "Show weather in top bar",
    "settings.weather.entity": "Weather entity",
    "settings.weather.info": "Uses a Home Assistant weather entity, for example weather.home.",
    "settings.stocks.heading": "Stocks",
    "settings.stocks.entity_1": "Stock sensor 1",
    "settings.stocks.entity_2": "Stock sensor 2",
    "settings.stocks.entity_3": "Stock sensor 3",
    "settings.stocks.info": "Shows up to 3 Home Assistant sensor values in the top bar. 4 inch panels show only the first value.",
    "settings.hardware.heading": "Hardware",
    "settings.hardware.active_brightness": "Normal brightness",
    "settings.hardware.dim_brightness": "Reduced brightness",
    "settings.hardware.dim_timeout": "Reduce after (seconds)",
    "settings.hardware.info": "Brightness values are 10% to 100%. The timeout controls when reduced brightness becomes active.",
    "settings.time.heading": "Time",
    "settings.time.ntp_server": "NTP Server",
    "settings.time.timezone": "Timezone (POSIX TZ)",
    "settings.ui.heading": "UI",
    "settings.theme.heading": "Theme",
    "settings.ui.language": "Language",
    "settings.ui.reload_languages": "Reload Languages",
    "settings.ui.download_json": "Download JSON",
    "settings.ui.upload_code": "Language Code",
    "settings.ui.upload_file": "Translation JSON File",
    "settings.ui.upload_button": "Upload / Add Language",
    "settings.ap.heading": "Setup AP",
    "settings.ap.hint": "If setup AP is active, connect to it and open <code>http://192.168.4.1</code>.",
    "settings.ota.heading": "Firmware Update",
    "settings.ota.url": "OTA URL",
    "settings.ota.url_placeholder": "https://github.com/cptkirki/BETTA-HA-PANEL/releases/latest/download/...",
    "settings.ota.flash_url": "Flash URL",
    "settings.ota.refresh": "Refresh Status",
    "settings.ota.file": "OTA .bin File",
    "settings.ota.upload": "Upload + Flash",
    "settings.ota.idle": "Ready for an OTA app image. Running: {running}, next slot: {next}, slot size: {size}.",
    "settings.ota.running": "OTA running: {progress}% ({written} / {total})",
    "settings.ota.downloading": "Downloading from URL: {progress}% ({written} / {total})",
    "settings.ota.uploading": "Upload received by panel: {progress}% ({written} / {total})",
    "settings.ota.success": "OTA image written. Rebooting now.",
    "settings.ota.error": "OTA failed: {error}",
    "settings.ota.rebooting": "Device is rebooting. Reopen the panel after it is back online.",
    "settings.ota.no_file": "Choose an OTA .bin file first.",
    "settings.ota.no_url": "Paste an OTA URL first.",
    "settings.ota.starting_url": "Starting OTA from URL...",
    "settings.ota.upload_progress": "Uploading to panel: {progress}% ({written} / {total})",
    "settings.ota.request_failed": "OTA request failed: {error}",
    "settings.ota.target_slot": "Target slot: {partition}",
    "settings.actions.heading": "Settings Actions",
    "settings.actions.reload": "Reload Settings",
    "settings.actions.save": "Save + Reboot",
    "settings.actions.hint": "After save, the device reboots and may switch from setup AP to your home Wi-Fi.",
    "settings.info.configured": "Configured",
    "settings.info.connected": "Connected",
    "settings.info.password_stored": "Password stored",
    "settings.info.country": "Country",
    "settings.info.rssi": "RSSI (connected AP)",
    "settings.info.connected_bssid": "Connected BSSID",
    "settings.info.channel": "Channel",
    "settings.info.token_stored": "Token stored",
    "settings.info.rest_fallback": "REST fallback",
    "common.yes": "yes",
    "common.no": "no",
    "common.scan": "Scan",
    "common.scan_wifi": "Scan Wi-Fi",
    "common.save_reboot": "Save + Reboot",
    "status.idle": "Idle",
    "status.loading_settings": "Loading settings...",
    "status.settings_loaded": "Settings loaded",
    "status.settings_load_failed": "Settings load failed: {error}",
    "status.settings_save_failed": "Settings save failed: {error}",
    "ha_diagnostics.missing_title": "Some entities in this layout were not found in Home Assistant",
    "ha_diagnostics.missing_title_more": "Some entities in this layout were not found in Home Assistant ({total} total, showing {listed})",
    "ha_diagnostics.missing_hint": "Open the affected widget, pick a valid entity and save the layout.",
    "ha_diagnostics.dismiss": "Dismiss",
    "status.saving_settings": "Saving settings...",
    "status.settings_saved_reboot": "Settings saved. Device reboots in ~2s. Reconnect and reopen the panel URL.",
    "status.wifi_scan_running": "Scanning Wi-Fi...",
    "status.wifi_scan_complete": "Wi-Fi scan complete ({count} networks)",
    "status.wifi_scan_failed": "Wi-Fi scan failed: {error}",
    "status.wifi_scan_timeout": "Wi-Fi scan request timed out",
    "common.unknown_error": "Unknown error",
    "wifi.scan_unavailable": "Wi-Fi scan is unavailable in setup AP mode on this hardware. Enter SSID manually.",
    "wifi.scan_click": "Click \"Scan Wi-Fi\" to list nearby networks.",
    "wifi.scan_click_short": "Click \"Scan\" to list nearby networks.",
    "wifi.scan_no_networks": "No networks found. Move closer to your router and scan again.",
    "wifi.scan_found": "{count} network(s) found. Select one to fill SSID.",
    "wifi.scan.connected_tag": "connected",
    "wifi.scan.option_unavailable": "Scan unavailable",
    "wifi.scan.option_scanning": "Scanning...",
    "wifi.scan.option_not_run": "No scan yet",
    "wifi.scan.option_no_networks": "No networks found",
    "wifi.scan.option_select": "Select network ({count} found)",
    "settings.time.info": "Applied after reboot. Time sync starts when Wi-Fi is connected.",
    "settings.ui.info": "Preview switches immediately. Saved language applies after reboot.",
    "settings.ap.active": "Setup AP active: {ssid}\\nOpen http://192.168.4.1 while connected to this AP.",
    "settings.ap.inactive": "Setup AP inactive.\\nUse the panel IP in your home Wi-Fi network.",
    "settings.translation.info": "Upload a JSON file to add or update a language.",
    "settings.translation.upload_ok": "Language \"{lang}\" uploaded.",
    "settings.translation.upload_fail": "Upload failed: {error}",
    "settings.translation.no_file": "Choose a JSON file first.",
    "settings.translation.invalid_json": "Invalid JSON",
    "settings.translation.object_required": "JSON must be an object",
    "settings.translation.invalid_code": "Language code must use [a-z0-9_-] and be 2-15 chars.",
    "settings.language.invalid_country": "Wi-Fi country code must be a 2-letter ISO code (e.g. US, DE)",
    "settings.language.invalid_bssid": "BSSID must be empty or in format AA:BB:CC:DD:EE:FF",
    "settings.language.invalid_ha_url": "HA URL must start with ws:// or wss://",
    "provision.wifi.required_ssid": "SSID is required.",
    "provision.wifi.required_country": "Country code must be 2 letters (e.g. US, DE).",
    "provision.ha.required_url": "WebSocket URL is required.",
    "provision.ha.invalid_url": "HA URL must start with ws:// or wss://.",
    "provision.ha.required_token": "Long-lived Access Token is required.",
    "provision.saving_reboot": "Saving settings and rebooting...",
    "provision.saved_reboot": "Settings saved. Device reboots in ~2s.",
    "provision.save_failed": "Save failed: {error}",
    "provision.wifi.hint": "Save reboots the panel. After reboot, HA provisioning is shown.",
    "provision.ha.hint": "Save reboots the panel. After reboot, the editor is unlocked.",
    "settings.language.option_de": "Deutsch",
    "settings.language.option_en": "English",
    "settings.language.option_es": "Espanol",
    "settings.language.option_fr": "Francais",
  },
  de: {
    "tabs.layout": "Layout",
    "tabs.settings": "Einstellungen",
    "sidebar.title": "BETTA Editor",
    "sidebar.subtitle": "Layout Quelle: JSON",
    "layout.pages.heading": "Seiten",
    "layout.pages.add": "+ Seite",
    "layout.pages.add_energy": "+ Energie-Seite",
    "layout.pages.delete": "Loeschen",
    "layout.pages.confirm_delete": "Seite \"{name}\" wirklich loeschen? Alle zugehoerigen Widgets werden entfernt.",
    "layout.pages.title_label": "Seitentitel",
    "layout.pages.title_placeholder": "Seitenname auf dem Display",
    "layout.pages.apply_title": "Seitentitel uebernehmen",
    "layout.pages.new_title": "Seite {number}",
    "layout.pages.energy_title": "Energie",
    "layout.energy.heading": "Energie-Seite",
    "layout.energy.hint": "Waehle, ob die Seite Home Assistant Energy spiegelt oder manuelle Live-Sensoren nutzt.",
    "layout.energy.source": "Datenquelle",
    "layout.energy.source_ha": "Home Assistant Energy",
    "layout.energy.source_manual": "Manuelle Live-Sensoren",
    "layout.energy.source_hint_ha": "Verwendet das in Home Assistant konfigurierte Energy Dashboard.",
    "layout.energy.source_hint_manual": "Expert-Fallback: explizite W/kW-Sensoren aus Home Assistant nutzen.",
    "layout.energy.home_power": "Hausleistung",
    "layout.energy.solar_power": "Solarleistung",
    "layout.energy.grid_power": "Netzleistung (signed)",
    "layout.energy.grid_import": "Netzbezug",
    "layout.energy.grid_export": "Netzeinspeisung",
    "layout.energy.battery_power": "Batterieleistung (signed)",
    "layout.energy.battery_charge": "Batterie laden",
    "layout.energy.battery_discharge": "Batterie entladen",
    "layout.energy.battery_soc": "Batterieladestand",
    "layout.energy.apply": "Energie-Konfig uebernehmen",
    "layout.energy.no_widgets": "Energie-Seiten rendern ein eigenes Dashboard und verwenden keine Widgets.",
    "layout.energy.preview_title": "Energieverteilung",
    "layout.energy.sensor_count_one": "{count} Sensor",
    "layout.energy.sensor_count_many": "{count} Sensoren",
    "layout.energy.no_sensor": "kein Sensor",
    "layout.energy.preview_source_ha": "HA Energy",
    "layout.energy.preview_source_manual": "Live-Sensoren",
    "layout.energy.preview_auto": "automatisch aus HA",
    "layout.energy.low_carbon": "Low-carbon",
    "layout.energy.grid": "Netz",
    "layout.energy.solar": "Solar",
    "layout.energy.gas": "Gas",
    "layout.energy.home": "Haus",
    "layout.energy.battery": "Batterie",
    "layout.energy.water": "Wasser",
    "layout.status.energy_page_only": "Energie-Seiten akzeptieren keine Widgets.",
    "layout.widgets.heading": "Widgets",
    "layout.widgets.add_sensor": "+ Sensor",
    "layout.widgets.add_button": "+ Button",
    "layout.widgets.add_slider": "+ Slider",
    "layout.widgets.add_graph": "+ Graph",
    "layout.widgets.add_empty_tile": "+ Empty Tile",
    "layout.widgets.add_light_tile": "+ Light Tile",
    "layout.widgets.add_heating_tile": "+ Heating Tile",
    "layout.widgets.add_weather_tile": "+ Weather",
    "layout.widgets.add_weather_3day": "+ Wetter Vorhersage",
    "layout.widgets.add_solar_forecast": "+ Solar Forecast",
    "layout.widgets.add_todo": "+ Todo Liste",
    "layout.widgets.add_media_player": "+ Media Player",
    "layout.widgets.add_roborock": "+ Roborock",
    "layout.widgets.quick_setup": "Quick Setup",
    "layout.widgets.delete": "Widget loeschen",
    "layout.widgets.confirm_delete": "Widget \"{name}\" wirklich loeschen?",
    "entity_picker.title": "Licht auswaehlen",
    "entity_picker.title_sensor": "Sensor auswaehlen",
    "entity_picker.title_light": "Licht auswaehlen",
    "entity_picker.title_switch": "Schalter auswaehlen",
    "entity_picker.title_weather": "Wetter auswaehlen",
    "entity_picker.title_solar_forecast": "Solar Forecast auswaehlen",
    "entity_picker.title_climate": "Heizung auswaehlen",
    "entity_picker.title_roborock": "Roborock auswaehlen",
    "entity_picker.title_image": "Bild auswaehlen",
    "entity_picker.title_entity": "Entitaet auswaehlen",
    "entity_picker.refresh": "Aktualisieren",
    "entity_picker.search": "Suchen",
    "entity_picker.close": "Schliessen",
    "entity_picker.search_placeholder": "Nach Name, Entitaet oder Raum suchen",
    "entity_picker.search_hint": "Mindestens {count} Zeichen eingeben, um {items} zu suchen.",
    "entity_picker.search_ready": "Enter druecken oder Suchen klicken, um {items} abzufragen.",
    "entity_picker.blank": "Leere Lichtkachel",
    "entity_picker.blank_sensor": "Leere Sensorkachel",
    "entity_picker.blank_light": "Leere Lichtkachel",
    "entity_picker.blank_button": "Leere Button-Kachel",
    "entity_picker.blank_weather": "Leere Wetterkachel",
    "entity_picker.blank_weather_3day": "Leere Wetter-Vorhersage-Kachel",
    "entity_picker.blank_solar_forecast": "Leere Solar-Forecast-Kachel",
    "entity_picker.blank_graph": "Leere Graph-Kachel",
    "entity_picker.blank_heating": "Leere Heizungskachel",
    "entity_picker.blank_roborock": "Leere Roborock-Kachel",
    "entity_picker.blank_image": "Leeres Bild",
    "entity_picker.loading": "Lichter werden geladen...",
    "entity_picker.loading_items": "{items} werden geladen...",
    "entity_picker.refreshing": "Lichter werden aktualisiert...",
    "entity_picker.refreshing_items": "{items} werden aktualisiert...",
    "entity_picker.pending": "Warte auf Home Assistant...",
    "entity_picker.disconnected": "Home Assistant ist nicht verbunden.",
    "entity_picker.empty": "Keine Licht-Entitaeten gefunden.",
    "entity_picker.empty_items": "Keine {items} gefunden.",
    "entity_picker.truncated": "Liste durch Firmware-Limit gekuerzt.",
    "entity_picker.unassigned_room": "Kein Raum",
    "entity_picker.added": "Lichtkachel hinzugefuegt: {entity}",
    "entity_picker.added_widget": "{widget} hinzugefuegt: {entity}",
    "entity_picker.fetch_failed": "Lichtsuche fehlgeschlagen: {error}",
    "entity_picker.fetch_failed_items": "Entitaetssuche fuer {items} fehlgeschlagen: {error}",
    "entity_picker.progress": "{loaded} / {target}",
    "entity_picker.progress_total": "{loaded} / {target} von {total}",
    "entity_picker.items_light": "Lichter",
    "entity_picker.items_sensor": "Sensoren",
    "entity_picker.items_switch": "Schalter",
    "entity_picker.items_weather": "Wetter-Entitaeten",
    "entity_picker.items_climate": "Climate-Entitaeten",
    "entity_picker.items_vacuum": "Saugroboter",
    "entity_picker.items_image": "Bild-Entitaeten",
    "entity_picker.items_entity": "Entitaeten",
    "entity_picker.widget_light": "Lichtkachel",
    "entity_picker.widget_sensor": "Sensorkachel",
    "entity_picker.widget_button": "Button-Kachel",
    "entity_picker.widget_weather": "Wetterkachel",
    "entity_picker.widget_weather_3day": "Wetter-Vorhersage-Kachel",
    "entity_picker.widget_solar_forecast": "Solar-Forecast-Kachel",
    "entity_picker.widget_graph": "Graph-Kachel",
    "entity_picker.widget_heating": "Heizungskachel",
    "entity_picker.widget_roborock": "Roborock-Kachel",
    "entity_picker.widget_image": "Bild",
    "entity_picker.assigned": "Entitaet gesetzt: {entity}",
    "layout.inspector.heading": "Inspektor",
    "layout.inspector.title": "Titel",
    "layout.inspector.entity": "Entitaet",
    "layout.inspector.solar_remaining_entity": "Verbleibend heute (Sensor)",
    "layout.inspector.solar_today": "Prognose heute",
    "layout.inspector.solar_tomorrow": "Prognose morgen",
    "layout.inspector.solar_day_3": "Prognose Tag 3",
    "layout.inspector.solar_day_4": "Prognose Tag 4",
    "layout.inspector.solar_day_5": "Prognose Tag 5",
    "layout.inspector.solar_bar_max": "Balken Maximum (kWh)",
    "layout.inspector.solar_bar_orientation": "Balken-Ausrichtung",
    "layout.inspector.secondary_entity": "Ist-Entitaet (Sensor)",
    "layout.inspector.secondary_entity_roborock": "Karten-Entitaet (image, optional)",
    "layout.inspector.button_mode": "Button Modus",
    "layout.inspector.button_accent_color": "Button Akzentfarbe",
    "layout.inspector.slider_entity_domain": "Slider Entitaetstyp",
    "layout.inspector.slider_direction": "Slider Richtung",
    "layout.inspector.slider_accent_color": "Slider Akzentfarbe",
    "layout.inspector.graph_line_color": "Graph Linienfarbe",
    "layout.inspector.graph_time_window_min": "Zeitfenster (Minuten)",
    "layout.inspector.graph_point_count": "Render Punkte (leer = auto)",
    "layout.inspector.graph_display_mode": "Anzeigeart",
    "layout.inspector.graph_bar_bucket_min": "Balken-Intervall (min)",
    "layout.option.graph_display_mode.line": "Linie mit Punkten",
    "layout.option.graph_display_mode.line_smooth_points": "Glatte Linie mit Punkten",
    "layout.option.graph_display_mode.line_smooth": "Glatte Linie",
    "layout.option.graph_display_mode.bars": "Balken",
    "layout.option.solar_bar_orientation.horizontal": "Horizontal",
    "layout.option.solar_bar_orientation.vertical": "Vertikal",
    "layout.inspector.apply": "Uebernehmen",
    "layout.option.button_mode.auto": "auto (Default switch)",
    "layout.option.button_mode.play_pause": "play/pause (media_player)",
    "layout.option.button_mode.stop": "stop (media_player)",
    "layout.option.button_mode.next": "next (media_player)",
    "layout.option.button_mode.previous": "previous (media_player)",
    "layout.option.slider_entity_domain.auto": "auto (light, media_player, cover)",
    "layout.option.slider_entity_domain.light": "light",
    "layout.option.slider_entity_domain.media_player": "media_player",
    "layout.option.slider_entity_domain.cover": "cover",
    "layout.option.slider_direction.auto": "auto (nach Breite/Hoehe)",
    "layout.option.slider_direction.left_to_right": "left_to_right (0% -> 100%)",
    "layout.option.slider_direction.right_to_left": "right_to_left (100% -> 0%)",
    "layout.option.slider_direction.bottom_to_top": "bottom_to_top (0% -> 100%)",
    "layout.option.slider_direction.top_to_bottom": "top_to_bottom (100% -> 0%)",
    "layout.actions.heading": "Aktionen",
    "layout.actions.reload": "Neu laden",
    "layout.actions.save": "Speichern",
    "layout.actions.export": "Export",
    "layout.actions.import": "JSON importieren",
    "layout.actions.paste_placeholder": "Layout JSON hier einfuegen",
    "layout.canvas.title": "Canvas",
    "layout.default_page.title": "Wohnzimmer",
    "layout.status.loading": "Layout wird geladen...",
    "layout.status.load_failed": "Layout laden fehlgeschlagen, nutze Default: {error}",
    "layout.status.loaded": "Layout geladen",
    "layout.status.entity_fetch_failed": "Entitaeten laden fehlgeschlagen: {error}",
    "layout.status.saving": "Layout wird gespeichert...",
    "layout.status.saved": "Layout gespeichert",
    "layout.status.imported": "Layout importiert (noch nicht gespeichert)",
    "layout.status.at_least_one_page": "Mindestens eine Seite ist erforderlich",
    "layout.status.entity_domain_required": "Entitaet muss diese Domain nutzen: {domains}",
    "layout.status.expected_domain": "die erwartete Domain",
    "layout.status.secondary_sensor_required": "Ist-Entitaet muss mit sensor. beginnen.",
    "layout.status.secondary_image_required": "Karten-Entitaet muss mit image. beginnen.",
    "layout.status.invalid_json": "Ungueltiges Layout JSON",
    "layout.status.save_failed": "Speichern fehlgeschlagen: {error}",
    "layout.status.import_failed": "Import fehlgeschlagen: {error}",
    "layout.status.file_import_failed": "Dateiimport fehlgeschlagen: {error}",
    "setup.title": "Quick Setup",
    "setup.step_ha": "HA verbunden",
    "setup.step_tiles": "Kacheln waehlen",
    "setup.step_save": "Layout speichern",
    "setup.subtitle": "Waehle ein paar Home Assistant Entitaeten fuer dein erstes Dashboard.",
    "setup.page_label": "Titel der ersten Seite",
    "setup.page_placeholder": "Wohnzimmer",
    "setup.add_light": "+ Licht",
    "setup.add_heating": "+ Heizung",
    "setup.add_weather": "+ Wetter",
    "setup.add_button": "+ Schalter",
    "setup.add_sensor": "+ Sensor",
    "setup.close": "Schliessen",
    "setup.skip": "Ueberspringen",
    "setup.done": "Speichern + Fertig",
    "setup.save": "Layout speichern",
    "setup.count_none": "Noch keine Kacheln hinzugefuegt.",
    "setup.count_one": "1 Kachel auf dieser Seite.",
    "setup.count_many": "{count} Kacheln auf dieser Seite.",
    "setup.added": "Hinzugefuegt: {title}",
    "setup.saving": "Layout wird gespeichert...",
    "setup.saved": "Layout gespeichert. Das Panel kann dieses Dashboard jetzt nutzen.",
    "setup.save_failed": "Speichern fehlgeschlagen: {error}",
    "provision.wifi.title": "WLAN Provisioning",
    "provision.wifi.subtitle": "Verbinde das Panel mit deinem WLAN.",
    "provision.wifi.ssid": "SSID",
    "provision.wifi.country_code": "Laendercode",
    "provision.wifi.password": "Passwort",
    "provision.wifi.password_placeholder": "WLAN Passwort",
    "provision.wifi.show_password": "Passwort anzeigen",
    "provision.ha.title": "HA Provisioning",
    "provision.ha.subtitle": "Verbinde das Panel mit Home Assistant.",
    "provision.ha.ws_url": "WebSocket URL (ws:// oder wss://)",
    "provision.ha.token": "Long-lived Access Token",
    "provision.ha.show_token": "Token anzeigen",
    "settings.wifi.heading": "WLAN",
    "settings.wifi.ssid": "SSID",
    "settings.wifi.country_code": "Laendercode",
    "settings.wifi.bssid": "BSSID Lock (optional)",
    "settings.wifi.password": "Passwort",
    "settings.wifi.password_placeholder": "Leer lassen, um vorhandenes Passwort zu behalten",
    "settings.ha.heading": "Home Assistant",
    "settings.ha.ws_url": "WebSocket URL (ws:// oder wss://)",
    "settings.ha.token": "Long-lived Access Token",
    "settings.ha.token_placeholder": "Leer lassen, um vorhandenen Token zu behalten",
    "settings.ha.rest_fallback": "HA REST Fallback aktivieren (Standard: Aus, WS bevorzugt)",
    "settings.weather.heading": "Wetter",
    "settings.weather.topbar_enabled": "Wetter in der Statuszeile anzeigen",
    "settings.weather.entity": "Wetter-Entitaet",
    "settings.weather.info": "Nutzt eine Home Assistant Wetter-Entitaet, zum Beispiel weather.home.",
    "settings.stocks.heading": "Stocks",
    "settings.stocks.entity_1": "Stock-Sensor 1",
    "settings.stocks.entity_2": "Stock-Sensor 2",
    "settings.stocks.entity_3": "Stock-Sensor 3",
    "settings.stocks.info": "Zeigt bis zu 3 Home Assistant Sensorwerte in der Statuszeile. 4 Zoll Panels zeigen nur den ersten Wert.",
    "settings.hardware.heading": "Hardware",
    "settings.hardware.active_brightness": "Normale Helligkeit",
    "settings.hardware.dim_brightness": "Reduzierte Helligkeit",
    "settings.hardware.dim_timeout": "Reduzieren nach (Sekunden)",
    "settings.hardware.info": "Helligkeit von 10% bis 100%. Der Timeout steuert, wann die reduzierte Helligkeit aktiv wird.",
    "settings.time.heading": "Zeit",
    "settings.time.ntp_server": "NTP Server",
    "settings.time.timezone": "Zeitzone (POSIX TZ)",
    "settings.ui.heading": "UI",
    "settings.theme.heading": "Theme",
    "settings.ui.language": "Sprache",
    "settings.ui.reload_languages": "Sprachen neu laden",
    "settings.ui.download_json": "JSON herunterladen",
    "settings.ui.upload_code": "Sprachcode",
    "settings.ui.upload_file": "Uebersetzungsdatei (JSON)",
    "settings.ui.upload_button": "Upload / Sprache hinzufuegen",
    "settings.ap.heading": "Setup AP",
    "settings.ap.hint": "Wenn Setup AP aktiv ist, verbinden und <code>http://192.168.4.1</code> oeffnen.",
    "settings.ota.heading": "Firmware Update",
    "settings.ota.url": "OTA URL",
    "settings.ota.url_placeholder": "https://github.com/cptkirki/BETTA-HA-PANEL/releases/latest/download/...",
    "settings.ota.flash_url": "URL flashen",
    "settings.ota.refresh": "Status aktualisieren",
    "settings.ota.file": "OTA .bin Datei",
    "settings.ota.upload": "Upload + Flash",
    "settings.ota.idle": "Bereit fuer ein OTA App-Image. Laufend: {running}, naechster Slot: {next}, Slotgroesse: {size}.",
    "settings.ota.running": "OTA laeuft: {progress}% ({written} / {total})",
    "settings.ota.downloading": "Download per URL: {progress}% ({written} / {total})",
    "settings.ota.uploading": "Upload vom Panel empfangen: {progress}% ({written} / {total})",
    "settings.ota.success": "OTA Image geschrieben. Neustart laeuft.",
    "settings.ota.error": "OTA fehlgeschlagen: {error}",
    "settings.ota.rebooting": "Geraet startet neu. Oeffne das Panel erneut, sobald es wieder online ist.",
    "settings.ota.no_file": "Bitte zuerst eine OTA .bin Datei waehlen.",
    "settings.ota.no_url": "Bitte zuerst eine OTA URL einfuegen.",
    "settings.ota.starting_url": "OTA per URL wird gestartet...",
    "settings.ota.upload_progress": "Upload zum Panel: {progress}% ({written} / {total})",
    "settings.ota.request_failed": "OTA Anfrage fehlgeschlagen: {error}",
    "settings.ota.target_slot": "Zielslot: {partition}",
    "settings.actions.heading": "Einstellungsaktionen",
    "settings.actions.reload": "Einstellungen neu laden",
    "settings.actions.save": "Speichern + Neustart",
    "settings.actions.hint": "Nach dem Speichern startet das Geraet neu und wechselt ggf. vom Setup AP ins Heim-WLAN.",
    "settings.info.configured": "Konfiguriert",
    "settings.info.connected": "Verbunden",
    "settings.info.password_stored": "Passwort gespeichert",
    "settings.info.country": "Land",
    "settings.info.rssi": "RSSI (verbundener AP)",
    "settings.info.connected_bssid": "Verbundener BSSID",
    "settings.info.channel": "Kanal",
    "settings.info.token_stored": "Token gespeichert",
    "settings.info.rest_fallback": "REST Fallback",
    "common.yes": "ja",
    "common.no": "nein",
    "common.scan": "Scan",
    "common.scan_wifi": "WLAN scannen",
    "common.save_reboot": "Speichern + Neustart",
    "status.idle": "Bereit",
    "status.loading_settings": "Einstellungen werden geladen...",
    "status.settings_loaded": "Einstellungen geladen",
    "status.settings_load_failed": "Einstellungen laden fehlgeschlagen: {error}",
    "status.settings_save_failed": "Einstellungen speichern fehlgeschlagen: {error}",
    "ha_diagnostics.missing_title": "Einige Entitäten aus diesem Layout wurden in Home Assistant nicht gefunden",
    "ha_diagnostics.missing_title_more": "Einige Entitäten aus diesem Layout wurden in Home Assistant nicht gefunden ({total} insgesamt, {listed} angezeigt)",
    "ha_diagnostics.missing_hint": "Öffne das betroffene Widget, wähle eine gültige Entität und speichere das Layout.",
    "ha_diagnostics.dismiss": "Schließen",
    "status.saving_settings": "Einstellungen werden gespeichert...",
    "status.settings_saved_reboot": "Einstellungen gespeichert. Das Geraet startet in ~2s neu.",
    "status.wifi_scan_running": "WLAN Suche laeuft...",
    "status.wifi_scan_complete": "WLAN Scan fertig ({count} Netze)",
    "status.wifi_scan_failed": "WLAN Scan fehlgeschlagen: {error}",
    "status.wifi_scan_timeout": "WLAN Scan Anfrage abgelaufen",
    "common.unknown_error": "Unbekannter Fehler",
    "wifi.scan_unavailable": "WLAN Scan ist im Setup AP Modus auf dieser Hardware nicht verfuegbar. SSID manuell eingeben.",
    "wifi.scan_click": "Auf \"WLAN scannen\" klicken, um Netze zu finden.",
    "wifi.scan_click_short": "Auf \"Scan\" klicken, um Netze zu finden.",
    "wifi.scan_no_networks": "Keine Netze gefunden. Gehe naeher an den Router und versuche es erneut.",
    "wifi.scan_found": "{count} Netzwerk(e) gefunden. SSID auswaehlen.",
    "wifi.scan.connected_tag": "verbunden",
    "wifi.scan.option_unavailable": "Scan nicht verfuegbar",
    "wifi.scan.option_scanning": "Suche laeuft...",
    "wifi.scan.option_not_run": "Noch kein Scan",
    "wifi.scan.option_no_networks": "Keine Netze gefunden",
    "wifi.scan.option_select": "Netz waehlen ({count} gefunden)",
    "settings.time.info": "Wird nach Neustart angewendet. Zeitsync startet bei WLAN Verbindung.",
    "settings.ui.info": "Vorschau wechselt sofort. Gespeicherte Sprache gilt nach Neustart.",
    "settings.ap.active": "Setup AP aktiv: {ssid}\\nhttp://192.168.4.1 im AP oeffnen.",
    "settings.ap.inactive": "Setup AP inaktiv.\\nNutze die Panel-IP im Heimnetz.",
    "settings.translation.info": "JSON hochladen, um eine Sprache hinzuzufuegen oder zu aktualisieren.",
    "settings.translation.upload_ok": "Sprache \"{lang}\" hochgeladen.",
    "settings.translation.upload_fail": "Upload fehlgeschlagen: {error}",
    "settings.translation.no_file": "Bitte zuerst eine JSON Datei auswaehlen.",
    "settings.translation.invalid_json": "Ungueltiges JSON",
    "settings.translation.object_required": "JSON muss ein Objekt sein",
    "settings.translation.invalid_code": "Sprachcode muss [a-z0-9_-] nutzen und 2-15 Zeichen haben.",
    "settings.language.invalid_country": "WLAN Laendercode muss ein 2-stelliger ISO Code sein (z.B. US, DE)",
    "settings.language.invalid_bssid": "BSSID muss leer sein oder Format AA:BB:CC:DD:EE:FF haben",
    "settings.language.invalid_ha_url": "HA URL muss mit ws:// oder wss:// beginnen",
    "provision.wifi.required_ssid": "SSID ist erforderlich.",
    "provision.wifi.required_country": "Laendercode muss 2 Buchstaben haben (z.B. US, DE).",
    "provision.ha.required_url": "WebSocket URL ist erforderlich.",
    "provision.ha.invalid_url": "HA URL muss mit ws:// oder wss:// beginnen.",
    "provision.ha.required_token": "Long-lived Access Token ist erforderlich.",
    "provision.saving_reboot": "Speichere Einstellungen und starte neu...",
    "provision.saved_reboot": "Einstellungen gespeichert. Geraet startet in ~2s neu.",
    "provision.save_failed": "Speichern fehlgeschlagen: {error}",
    "provision.wifi.hint": "Speichern startet das Panel neu. Danach folgt die HA Einrichtung.",
    "provision.ha.hint": "Speichern startet das Panel neu. Danach ist der Editor freigeschaltet.",
    "settings.language.option_de": "Deutsch",
    "settings.language.option_en": "Englisch",
    "settings.language.option_es": "Spanisch",
    "settings.language.option_fr": "Franzoesisch",
  },
  es: {
    "tabs.layout": "Diseno",
    "tabs.settings": "Configuracion",
    "sidebar.title": "BETTA Editor",
    "sidebar.subtitle": "Fuente de verdad del layout: JSON",
    "layout.pages.heading": "Paginas",
    "layout.pages.add": "+ Pagina",
    "layout.pages.delete": "Eliminar",
    "layout.pages.title_label": "Titulo de pagina",
    "layout.pages.title_placeholder": "Nombre de pagina en la pantalla",
    "layout.pages.apply_title": "Aplicar titulo de pagina",
    "layout.pages.new_title": "Pagina {number}",
    "layout.widgets.heading": "Widgets",
    "layout.widgets.add_sensor": "+ Sensor",
    "layout.widgets.add_button": "+ Boton",
    "layout.widgets.add_slider": "+ Slider",
    "layout.widgets.add_graph": "+ Grafico",
    "layout.widgets.add_empty_tile": "+ Tile vacio",
    "layout.widgets.add_light_tile": "+ Tile de luz",
    "layout.widgets.add_heating_tile": "+ Tile de calefaccion",
    "layout.widgets.add_weather_tile": "+ Clima",
    "layout.widgets.add_weather_3day": "+ Previsão do tempo",
    "layout.widgets.delete": "Eliminar Widget",
    "entity_picker.title": "Elegir luz",
    "entity_picker.refresh": "Actualizar",
    "entity_picker.close": "Cerrar",
    "entity_picker.blank": "Tile de luz vacio",
    "entity_picker.loading": "Cargando luces...",
    "entity_picker.refreshing": "Actualizando luces...",
    "entity_picker.pending": "Esperando Home Assistant...",
    "entity_picker.disconnected": "Home Assistant no esta conectado.",
    "entity_picker.empty": "No se encontraron luces.",
    "entity_picker.truncated": "Lista recortada por limite de firmware.",
    "entity_picker.unassigned_room": "Sin sala",
    "entity_picker.added": "Tile de luz agregado: {entity}",
    "entity_picker.fetch_failed": "Error al buscar luces: {error}",
    "layout.inspector.heading": "Inspector",
    "layout.inspector.title": "Titulo",
    "layout.inspector.entity": "Entidad",
    "layout.inspector.secondary_entity": "Entidad real (sensor)",
    "layout.inspector.button_mode": "Modo de boton",
    "layout.inspector.button_accent_color": "Color de acento del boton",
    "layout.inspector.slider_entity_domain": "Tipo de entidad del slider",
    "layout.inspector.slider_direction": "Direccion del slider",
    "layout.inspector.slider_accent_color": "Color de acento del slider",
    "layout.inspector.graph_line_color": "Color de linea del grafico",
    "layout.inspector.graph_time_window_min": "Ventana de tiempo (minutos)",
    "layout.inspector.graph_point_count": "Puntos de render (vacio = auto)",
    "layout.inspector.graph_display_mode": "Modo de visualizacion",
    "layout.inspector.graph_bar_bucket_min": "Intervalo de barras (min)",
    "layout.option.graph_display_mode.line": "Linea con puntos",
    "layout.option.graph_display_mode.line_smooth_points": "Linea suave con puntos",
    "layout.option.graph_display_mode.line_smooth": "Linea suave",
    "layout.option.graph_display_mode.bars": "Barras",
    "layout.inspector.apply": "Aplicar",
    "layout.option.button_mode.auto": "auto (switch por defecto)",
    "layout.option.button_mode.play_pause": "play/pause (media_player)",
    "layout.option.button_mode.stop": "stop (media_player)",
    "layout.option.button_mode.next": "next (media_player)",
    "layout.option.button_mode.previous": "previous (media_player)",
    "layout.option.slider_entity_domain.auto": "auto (light, media_player, cover)",
    "layout.option.slider_entity_domain.light": "light",
    "layout.option.slider_entity_domain.media_player": "media_player",
    "layout.option.slider_entity_domain.cover": "cover",
    "layout.option.slider_direction.auto": "auto (segun ancho/alto)",
    "layout.option.slider_direction.left_to_right": "left_to_right (0% -> 100%)",
    "layout.option.slider_direction.right_to_left": "right_to_left (100% -> 0%)",
    "layout.option.slider_direction.bottom_to_top": "bottom_to_top (0% -> 100%)",
    "layout.option.slider_direction.top_to_bottom": "top_to_bottom (100% -> 0%)",
    "layout.actions.heading": "Acciones",
    "layout.actions.reload": "Recargar",
    "layout.actions.save": "Guardar",
    "layout.actions.export": "Exportar",
    "layout.actions.import": "Importar JSON",
    "layout.actions.paste_placeholder": "Pega aqui el JSON del layout",
    "layout.canvas.title": "Canvas",
    "layout.default_page.title": "Sala",
    "layout.status.loading": "Cargando layout...",
    "layout.status.load_failed": "Error al cargar layout, usando default: {error}",
    "layout.status.loaded": "Layout cargado",
    "layout.status.entity_fetch_failed": "Error al cargar entidades: {error}",
    "layout.status.saving": "Guardando layout...",
    "layout.status.saved": "Layout guardado",
    "layout.status.imported": "Layout importado (aun no guardado)",
    "layout.status.at_least_one_page": "Se requiere al menos una pagina",
    "layout.status.entity_domain_required": "La entidad debe usar el dominio: {domains}",
    "layout.status.expected_domain": "el dominio esperado",
    "layout.status.secondary_sensor_required": "La entidad real debe empezar con sensor.",
    "layout.status.invalid_json": "JSON de layout invalido",
    "layout.status.save_failed": "Error al guardar: {error}",
    "layout.status.import_failed": "Error al importar: {error}",
    "layout.status.file_import_failed": "Error al importar archivo: {error}",
    "provision.wifi.title": "Provision Wi-Fi",
    "provision.wifi.subtitle": "Conecta el panel a tu red Wi-Fi.",
    "provision.wifi.ssid": "SSID",
    "provision.wifi.country_code": "Codigo de pais",
    "provision.wifi.password": "Contrasena",
    "provision.wifi.password_placeholder": "Contrasena Wi-Fi",
    "provision.wifi.show_password": "Mostrar contrasena",
    "provision.ha.title": "Provision HA",
    "provision.ha.subtitle": "Conecta el panel a Home Assistant.",
    "provision.ha.ws_url": "URL WebSocket (ws:// o wss://)",
    "provision.ha.token": "Token de acceso de larga duracion",
    "provision.ha.show_token": "Mostrar token",
    "settings.wifi.heading": "Wi-Fi",
    "settings.wifi.ssid": "SSID",
    "settings.wifi.country_code": "Codigo de pais",
    "settings.wifi.bssid": "Bloqueo BSSID (opcional)",
    "settings.wifi.password": "Contrasena",
    "settings.wifi.password_placeholder": "Dejar vacio para conservar la contrasena guardada",
    "settings.ha.heading": "Home Assistant",
    "settings.ha.ws_url": "URL WebSocket (ws:// o wss://)",
    "settings.ha.token": "Token de acceso de larga duracion",
    "settings.ha.token_placeholder": "Dejar vacio para conservar el token guardado",
    "settings.ha.rest_fallback": "Activar fallback REST de HA (por defecto: off, se prefiere WS)",
    "settings.weather.heading": "Tiempo",
    "settings.weather.topbar_enabled": "Mostrar tiempo en la barra superior",
    "settings.weather.entity": "Entidad de tiempo",
    "settings.weather.info": "Usa una entidad weather de Home Assistant, por ejemplo weather.home.",
    "settings.stocks.heading": "Acciones",
    "settings.stocks.entity_1": "Sensor de accion 1",
    "settings.stocks.entity_2": "Sensor de accion 2",
    "settings.stocks.entity_3": "Sensor de accion 3",
    "settings.stocks.info": "Muestra hasta 3 sensores de Home Assistant en la barra superior. Paneles de 4 pulgadas muestran solo el primer valor.",
    "settings.hardware.heading": "Hardware",
    "settings.hardware.active_brightness": "Brillo normal",
    "settings.hardware.dim_brightness": "Brillo reducido",
    "settings.hardware.dim_timeout": "Reducir despues de (segundos)",
    "settings.hardware.info": "Los valores de brillo son 10% a 100%. El timeout controla cuando se activa el brillo reducido.",
    "settings.time.heading": "Hora",
    "settings.time.ntp_server": "Servidor NTP",
    "settings.time.timezone": "Zona horaria (POSIX TZ)",
    "settings.ui.heading": "UI",
    "settings.theme.heading": "Tema",
    "settings.ui.language": "Idioma",
    "settings.ui.reload_languages": "Recargar idiomas",
    "settings.ui.download_json": "Descargar JSON",
    "settings.ui.upload_code": "Codigo de idioma",
    "settings.ui.upload_file": "Archivo JSON de traduccion",
    "settings.ui.upload_button": "Subir / Agregar idioma",
    "settings.ap.heading": "AP de setup",
    "settings.ap.hint": "Si el AP de setup esta activo, conectate y abre <code>http://192.168.4.1</code>.",
    "settings.ota.heading": "Actualizacion de firmware",
    "settings.ota.url": "URL OTA",
    "settings.ota.url_placeholder": "https://github.com/cptkirki/BETTA-HA-PANEL/releases/latest/download/...",
    "settings.ota.flash_url": "Flashear URL",
    "settings.ota.refresh": "Actualizar estado",
    "settings.ota.file": "Archivo OTA .bin",
    "settings.ota.upload": "Subir + Flashear",
    "settings.ota.idle": "Listo para una imagen OTA de app. Ejecutando: {running}, siguiente slot: {next}, tamano de slot: {size}.",
    "settings.ota.running": "OTA en curso: {progress}% ({written} / {total})",
    "settings.ota.downloading": "Descargando desde URL: {progress}% ({written} / {total})",
    "settings.ota.uploading": "Upload recibido por el panel: {progress}% ({written} / {total})",
    "settings.ota.success": "Imagen OTA escrita. Reiniciando ahora.",
    "settings.ota.error": "OTA fallo: {error}",
    "settings.ota.rebooting": "El dispositivo se esta reiniciando. Abre el panel de nuevo cuando vuelva online.",
    "settings.ota.no_file": "Elige primero un archivo OTA .bin.",
    "settings.ota.no_url": "Pega primero una URL OTA.",
    "settings.ota.starting_url": "Iniciando OTA desde URL...",
    "settings.ota.upload_progress": "Subiendo al panel: {progress}% ({written} / {total})",
    "settings.ota.request_failed": "Solicitud OTA fallida: {error}",
    "settings.ota.target_slot": "Slot destino: {partition}",
    "settings.actions.heading": "Acciones de configuracion",
    "settings.actions.reload": "Recargar configuracion",
    "settings.actions.save": "Guardar + Reiniciar",
    "settings.actions.hint": "Despues de guardar, el dispositivo reinicia y puede cambiar del AP de setup al Wi-Fi de casa.",
    "settings.info.configured": "Configurado",
    "settings.info.connected": "Conectado",
    "settings.info.password_stored": "Contrasena guardada",
    "settings.info.country": "Pais",
    "settings.info.rssi": "RSSI (AP conectado)",
    "settings.info.connected_bssid": "BSSID conectado",
    "settings.info.channel": "Canal",
    "settings.info.token_stored": "Token guardado",
    "settings.info.rest_fallback": "Fallback REST",
    "common.yes": "si",
    "common.no": "no",
    "common.scan": "Escanear",
    "common.scan_wifi": "Escanear Wi-Fi",
    "common.save_reboot": "Guardar + Reiniciar",
    "status.idle": "Listo",
    "status.loading_settings": "Cargando configuracion...",
    "status.settings_loaded": "Configuracion cargada",
    "status.settings_load_failed": "Error al cargar configuracion: {error}",
    "status.settings_save_failed": "Error al guardar configuracion: {error}",
    "status.saving_settings": "Guardando configuracion...",
    "status.settings_saved_reboot": "Configuracion guardada. El dispositivo reinicia en ~2s.",
    "status.wifi_scan_running": "Escaneo Wi-Fi en curso...",
    "status.wifi_scan_complete": "Escaneo Wi-Fi completo ({count} redes)",
    "status.wifi_scan_failed": "Error en escaneo Wi-Fi: {error}",
    "status.wifi_scan_timeout": "Tiempo de espera agotado para escaneo Wi-Fi",
    "common.unknown_error": "Error desconocido",
    "wifi.scan_unavailable": "El escaneo Wi-Fi no esta disponible en modo setup AP en este hardware. Ingresa SSID manualmente.",
    "wifi.scan_click": "Pulsa \"Escanear Wi-Fi\" para listar redes cercanas.",
    "wifi.scan_click_short": "Pulsa \"Escanear\" para listar redes cercanas.",
    "wifi.scan_no_networks": "No se encontraron redes. Acercate al router e intenta de nuevo.",
    "wifi.scan_found": "{count} red(es) encontradas. Selecciona una para llenar SSID.",
    "wifi.scan.connected_tag": "conectado",
    "wifi.scan.option_unavailable": "Escaneo no disponible",
    "wifi.scan.option_scanning": "Escaneando...",
    "wifi.scan.option_not_run": "Sin escaneo",
    "wifi.scan.option_no_networks": "No se encontraron redes",
    "wifi.scan.option_select": "Selecciona red ({count} encontradas)",
    "settings.time.info": "Se aplica despues de reiniciar. La sincronizacion inicia cuando Wi-Fi esta conectado.",
    "settings.ui.info": "La vista previa cambia al instante. El idioma guardado se aplica tras reiniciar.",
    "settings.ap.active": "AP de setup activo: {ssid}\\nAbre http://192.168.4.1 conectado a este AP.",
    "settings.ap.inactive": "AP de setup inactivo.\\nUsa la IP del panel en tu Wi-Fi.",
    "settings.translation.info": "Sube un JSON para agregar o actualizar un idioma.",
    "settings.translation.upload_ok": "Idioma \"{lang}\" subido.",
    "settings.translation.upload_fail": "Error de subida: {error}",
    "settings.translation.no_file": "Selecciona primero un archivo JSON.",
    "settings.translation.invalid_json": "JSON invalido",
    "settings.translation.object_required": "El JSON debe ser un objeto",
    "settings.translation.invalid_code": "El codigo de idioma debe usar [a-z0-9_-] y tener 2-15 caracteres.",
    "settings.language.invalid_country": "El codigo de pais Wi-Fi debe ser ISO de 2 letras (p.ej. US, DE)",
    "settings.language.invalid_bssid": "El BSSID debe estar vacio o en formato AA:BB:CC:DD:EE:FF",
    "settings.language.invalid_ha_url": "La URL HA debe empezar con ws:// o wss://",
    "provision.wifi.required_ssid": "SSID es obligatorio.",
    "provision.wifi.required_country": "El codigo de pais debe tener 2 letras (p.ej. US, DE).",
    "provision.ha.required_url": "La URL WebSocket es obligatoria.",
    "provision.ha.invalid_url": "La URL HA debe empezar con ws:// o wss://.",
    "provision.ha.required_token": "El token de acceso es obligatorio.",
    "provision.saving_reboot": "Guardando configuracion y reiniciando...",
    "provision.saved_reboot": "Configuracion guardada. El dispositivo reinicia en ~2s.",
    "provision.save_failed": "Error al guardar: {error}",
    "provision.wifi.hint": "Guardar reinicia el panel. Despues se muestra la provision de HA.",
    "provision.ha.hint": "Guardar reinicia el panel. Despues se desbloquea el editor.",
    "settings.language.option_de": "Aleman",
    "settings.language.option_en": "Ingles",
    "settings.language.option_es": "Espanol",
    "settings.language.option_fr": "Frances",
  },
  fr: {
    "tabs.layout": "Layout",
    "tabs.settings": "Parametres",
    "sidebar.title": "BETTA Editor",
    "sidebar.subtitle": "Source du layout: JSON",
    "layout.pages.heading": "Pages",
    "layout.pages.add": "+ Page",
    "layout.pages.delete": "Supprimer",
    "layout.pages.title_label": "Titre de page",
    "layout.pages.title_placeholder": "Nom de page sur l'ecran",
    "layout.pages.apply_title": "Appliquer le titre",
    "layout.pages.new_title": "Page {number}",
    "layout.widgets.heading": "Widgets",
    "layout.widgets.add_sensor": "+ Capteur",
    "layout.widgets.add_button": "+ Bouton",
    "layout.widgets.add_slider": "+ Curseur",
    "layout.widgets.add_graph": "+ Graphe",
    "layout.widgets.add_empty_tile": "+ Tuile vide",
    "layout.widgets.add_light_tile": "+ Tuile lumiere",
    "layout.widgets.add_heating_tile": "+ Tuile chauffage",
    "layout.widgets.add_weather_tile": "+ Meteo",
    "layout.widgets.add_weather_3day": "+ Prévision météo",
    "layout.widgets.delete": "Supprimer le widget",
    "entity_picker.title": "Choisir une lumiere",
    "entity_picker.refresh": "Actualiser",
    "entity_picker.close": "Fermer",
    "entity_picker.blank": "Tuile lumiere vide",
    "entity_picker.loading": "Chargement des lumieres...",
    "entity_picker.refreshing": "Actualisation des lumieres...",
    "entity_picker.pending": "En attente de Home Assistant...",
    "entity_picker.disconnected": "Home Assistant n'est pas connecte.",
    "entity_picker.empty": "Aucune entite lumiere trouvee.",
    "entity_picker.truncated": "Liste limitee par le firmware.",
    "entity_picker.unassigned_room": "Sans piece",
    "entity_picker.added": "Tuile lumiere ajoutee: {entity}",
    "entity_picker.fetch_failed": "Echec de la recherche de lumieres: {error}",
    "layout.inspector.heading": "Inspecteur",
    "layout.inspector.title": "Titre",
    "layout.inspector.entity": "Entite",
    "layout.inspector.secondary_entity": "Entite reelle (capteur)",
    "layout.inspector.button_mode": "Mode du bouton",
    "layout.inspector.button_accent_color": "Couleur d'accent du bouton",
    "layout.inspector.slider_entity_domain": "Type d'entite du curseur",
    "layout.inspector.slider_direction": "Direction du curseur",
    "layout.inspector.slider_accent_color": "Couleur d'accent du curseur",
    "layout.inspector.graph_line_color": "Couleur de ligne du graphe",
    "layout.inspector.graph_time_window_min": "Fenetre de temps (minutes)",
    "layout.inspector.graph_point_count": "Points de rendu (vide = auto)",
    "layout.inspector.graph_display_mode": "Mode d'affichage",
    "layout.inspector.graph_bar_bucket_min": "Intervalle de barres (min)",
    "layout.option.graph_display_mode.line": "Ligne avec points",
    "layout.option.graph_display_mode.line_smooth_points": "Ligne lissee avec points",
    "layout.option.graph_display_mode.line_smooth": "Ligne lissee",
    "layout.option.graph_display_mode.bars": "Barres",
    "layout.inspector.apply": "Appliquer",
    "layout.option.button_mode.auto": "auto (interrupteur par defaut)",
    "layout.option.button_mode.play_pause": "play/pause (media_player)",
    "layout.option.button_mode.stop": "stop (media_player)",
    "layout.option.button_mode.next": "next (media_player)",
    "layout.option.button_mode.previous": "previous (media_player)",
    "layout.option.slider_entity_domain.auto": "auto (light, media_player, cover)",
    "layout.option.slider_entity_domain.light": "light",
    "layout.option.slider_entity_domain.media_player": "media_player",
    "layout.option.slider_entity_domain.cover": "cover",
    "layout.option.slider_direction.auto": "auto (selon largeur/hauteur)",
    "layout.option.slider_direction.left_to_right": "left_to_right (0% -> 100%)",
    "layout.option.slider_direction.right_to_left": "right_to_left (100% -> 0%)",
    "layout.option.slider_direction.bottom_to_top": "bottom_to_top (0% -> 100%)",
    "layout.option.slider_direction.top_to_bottom": "top_to_bottom (100% -> 0%)",
    "layout.actions.heading": "Actions",
    "layout.actions.reload": "Recharger",
    "layout.actions.save": "Enregistrer",
    "layout.actions.export": "Exporter",
    "layout.actions.import": "Importer JSON",
    "layout.actions.paste_placeholder": "Coller le JSON du layout ici",
    "layout.canvas.title": "Canvas",
    "layout.default_page.title": "Salon",
    "layout.status.loading": "Chargement du layout...",
    "layout.status.load_failed": "Echec du chargement du layout, defaut utilise: {error}",
    "layout.status.loaded": "Layout charge",
    "layout.status.entity_fetch_failed": "Echec du chargement des entites: {error}",
    "layout.status.saving": "Enregistrement du layout...",
    "layout.status.saved": "Layout enregistre",
    "layout.status.imported": "Layout importe (pas encore enregistre)",
    "layout.status.at_least_one_page": "Au moins une page est requise",
    "layout.status.entity_domain_required": "L'entite doit utiliser le domaine: {domains}",
    "layout.status.expected_domain": "le domaine attendu",
    "layout.status.secondary_sensor_required": "L'entite reelle doit commencer par sensor.",
    "layout.status.invalid_json": "JSON de layout invalide",
    "layout.status.save_failed": "Echec de l'enregistrement: {error}",
    "layout.status.import_failed": "Echec de l'import: {error}",
    "layout.status.file_import_failed": "Echec de l'import du fichier: {error}",
    "provision.wifi.title": "Provision Wi-Fi",
    "provision.wifi.subtitle": "Connectez le panneau a votre Wi-Fi.",
    "provision.wifi.ssid": "SSID",
    "provision.wifi.country_code": "Code pays",
    "provision.wifi.password": "Mot de passe",
    "provision.wifi.password_placeholder": "Mot de passe Wi-Fi",
    "provision.wifi.show_password": "Afficher le mot de passe",
    "provision.ha.title": "Provision HA",
    "provision.ha.subtitle": "Connectez le panneau a Home Assistant.",
    "provision.ha.ws_url": "URL WebSocket (ws:// ou wss://)",
    "provision.ha.token": "Jeton d'acces longue duree",
    "provision.ha.show_token": "Afficher le token",
    "settings.wifi.heading": "Wi-Fi",
    "settings.wifi.ssid": "SSID",
    "settings.wifi.country_code": "Code pays",
    "settings.wifi.bssid": "Verrou BSSID (optionnel)",
    "settings.wifi.password": "Mot de passe",
    "settings.wifi.password_placeholder": "Laisser vide pour garder le mot de passe stocke",
    "settings.ha.heading": "Home Assistant",
    "settings.ha.ws_url": "URL WebSocket (ws:// ou wss://)",
    "settings.ha.token": "Jeton d'acces longue duree",
    "settings.ha.token_placeholder": "Laisser vide pour garder le token stocke",
    "settings.ha.rest_fallback": "Activer le fallback REST HA (defaut: off, WS prefere)",
    "settings.weather.heading": "Meteo",
    "settings.weather.topbar_enabled": "Afficher la meteo dans la barre superieure",
    "settings.weather.entity": "Entite meteo",
    "settings.weather.info": "Utilise une entite weather de Home Assistant, par exemple weather.home.",
    "settings.stocks.heading": "Actions",
    "settings.stocks.entity_1": "Capteur action 1",
    "settings.stocks.entity_2": "Capteur action 2",
    "settings.stocks.entity_3": "Capteur action 3",
    "settings.stocks.info": "Affiche jusqu'a 3 capteurs Home Assistant dans la barre superieure. Les panneaux 4 pouces affichent seulement la premiere valeur.",
    "settings.hardware.heading": "Hardware",
    "settings.hardware.active_brightness": "Luminosite normale",
    "settings.hardware.dim_brightness": "Luminosite reduite",
    "settings.hardware.dim_timeout": "Reduire apres (secondes)",
    "settings.hardware.info": "Les valeurs de luminosite vont de 10% a 100%. Le timeout controle quand la luminosite reduite devient active.",
    "settings.time.heading": "Temps",
    "settings.time.ntp_server": "Serveur NTP",
    "settings.time.timezone": "Fuseau horaire (POSIX TZ)",
    "settings.ui.heading": "UI",
    "settings.theme.heading": "Thème",
    "settings.ui.language": "Langue",
    "settings.ui.reload_languages": "Recharger les langues",
    "settings.ui.download_json": "Telecharger JSON",
    "settings.ui.upload_code": "Code langue",
    "settings.ui.upload_file": "Fichier JSON de traduction",
    "settings.ui.upload_button": "Uploader / Ajouter une langue",
    "settings.ap.heading": "Setup AP",
    "settings.ap.hint": "Si le setup AP est actif, connectez-vous et ouvrez <code>http://192.168.4.1</code>.",
    "settings.ota.heading": "Mise a jour firmware",
    "settings.ota.url": "URL OTA",
    "settings.ota.url_placeholder": "https://github.com/cptkirki/BETTA-HA-PANEL/releases/latest/download/...",
    "settings.ota.flash_url": "Flasher URL",
    "settings.ota.refresh": "Actualiser statut",
    "settings.ota.file": "Fichier OTA .bin",
    "settings.ota.upload": "Upload + Flash",
    "settings.ota.idle": "Pret pour une image OTA app. En cours: {running}, prochain slot: {next}, taille du slot: {size}.",
    "settings.ota.running": "OTA en cours: {progress}% ({written} / {total})",
    "settings.ota.downloading": "Telechargement depuis URL: {progress}% ({written} / {total})",
    "settings.ota.uploading": "Upload recu par le panneau: {progress}% ({written} / {total})",
    "settings.ota.success": "Image OTA ecrite. Redemarrage en cours.",
    "settings.ota.error": "OTA echouee: {error}",
    "settings.ota.rebooting": "L'appareil redemarre. Rouvrez le panneau lorsqu'il est de retour en ligne.",
    "settings.ota.no_file": "Choisissez d'abord un fichier OTA .bin.",
    "settings.ota.no_url": "Collez d'abord une URL OTA.",
    "settings.ota.starting_url": "Demarrage de l'OTA depuis l'URL...",
    "settings.ota.upload_progress": "Upload vers le panneau: {progress}% ({written} / {total})",
    "settings.ota.request_failed": "Requete OTA echouee: {error}",
    "settings.ota.target_slot": "Slot cible: {partition}",
    "settings.actions.heading": "Actions des parametres",
    "settings.actions.reload": "Recharger les parametres",
    "settings.actions.save": "Enregistrer + Redemarrer",
    "settings.actions.hint": "Apres enregistrement, l'appareil redemarre et peut passer du setup AP au Wi-Fi domestique.",
    "settings.info.configured": "Configure",
    "settings.info.connected": "Connecte",
    "settings.info.password_stored": "Mot de passe stocke",
    "settings.info.country": "Pays",
    "settings.info.rssi": "RSSI (AP connecte)",
    "settings.info.connected_bssid": "BSSID connecte",
    "settings.info.channel": "Canal",
    "settings.info.token_stored": "Token stocke",
    "settings.info.rest_fallback": "Fallback REST",
    "common.yes": "oui",
    "common.no": "non",
    "common.scan": "Scanner",
    "common.scan_wifi": "Scanner Wi-Fi",
    "common.save_reboot": "Enregistrer + Redemarrer",
    "status.idle": "Pret",
    "status.loading_settings": "Chargement des parametres...",
    "status.settings_loaded": "Parametres charges",
    "status.settings_load_failed": "Echec du chargement des parametres: {error}",
    "status.settings_save_failed": "Echec de l'enregistrement des parametres: {error}",
    "status.saving_settings": "Enregistrement des parametres...",
    "status.settings_saved_reboot": "Parametres enregistres. L'appareil redemarre dans ~2s.",
    "status.wifi_scan_running": "Scan Wi-Fi en cours...",
    "status.wifi_scan_complete": "Scan Wi-Fi termine ({count} reseaux)",
    "status.wifi_scan_failed": "Echec du scan Wi-Fi: {error}",
    "status.wifi_scan_timeout": "Delai du scan Wi-Fi depasse",
    "common.unknown_error": "Erreur inconnue",
    "wifi.scan_unavailable": "Le scan Wi-Fi est indisponible en mode setup AP sur ce materiel. Entrez le SSID manuellement.",
    "wifi.scan_click": "Cliquez sur \"Scanner Wi-Fi\" pour lister les reseaux proches.",
    "wifi.scan_click_short": "Cliquez sur \"Scanner\" pour lister les reseaux proches.",
    "wifi.scan_no_networks": "Aucun reseau trouve. Rapprochez-vous du routeur et reessayez.",
    "wifi.scan_found": "{count} reseau(x) trouve(s). Selectionnez-en un pour remplir le SSID.",
    "wifi.scan.connected_tag": "connecte",
    "wifi.scan.option_unavailable": "Scan indisponible",
    "wifi.scan.option_scanning": "Scan en cours...",
    "wifi.scan.option_not_run": "Aucun scan",
    "wifi.scan.option_no_networks": "Aucun reseau trouve",
    "wifi.scan.option_select": "Selectionner reseau ({count} trouves)",
    "settings.time.info": "Applique apres redemarrage. La synchronisation demarre quand le Wi-Fi est connecte.",
    "settings.ui.info": "L'apercu change immediatement. La langue enregistree s'applique apres redemarrage.",
    "settings.ap.active": "Setup AP actif: {ssid}\\nOuvrez http://192.168.4.1 en etant connecte a cet AP.",
    "settings.ap.inactive": "Setup AP inactif.\\nUtilisez l'IP du panneau sur votre Wi-Fi.",
    "settings.translation.info": "Uploadez un JSON pour ajouter ou mettre a jour une langue.",
    "settings.translation.upload_ok": "Langue \"{lang}\" uploadee.",
    "settings.translation.upload_fail": "Echec de l'upload: {error}",
    "settings.translation.no_file": "Choisissez d'abord un fichier JSON.",
    "settings.translation.invalid_json": "JSON invalide",
    "settings.translation.object_required": "Le JSON doit etre un objet",
    "settings.translation.invalid_code": "Le code langue doit utiliser [a-z0-9_-] et avoir 2-15 caracteres.",
    "settings.language.invalid_country": "Le code pays Wi-Fi doit etre un code ISO a 2 lettres (ex: US, DE)",
    "settings.language.invalid_bssid": "Le BSSID doit etre vide ou au format AA:BB:CC:DD:EE:FF",
    "settings.language.invalid_ha_url": "L'URL HA doit commencer par ws:// ou wss://",
    "provision.wifi.required_ssid": "SSID requis.",
    "provision.wifi.required_country": "Le code pays doit avoir 2 lettres (ex: US, DE).",
    "provision.ha.required_url": "URL WebSocket requise.",
    "provision.ha.invalid_url": "L'URL HA doit commencer par ws:// ou wss://.",
    "provision.ha.required_token": "Jeton d'acces longue duree requis.",
    "provision.saving_reboot": "Enregistrement des parametres et redemarrage...",
    "provision.saved_reboot": "Parametres enregistres. L'appareil redemarre dans ~2s.",
    "provision.save_failed": "Echec de l'enregistrement: {error}",
    "provision.wifi.hint": "Enregistrer redemarre le panneau. Apres redemarrage, la provision HA s'affiche.",
    "provision.ha.hint": "Enregistrer redemarre le panneau. Apres redemarrage, l'editeur est debloque.",
    "settings.language.option_de": "Allemand",
    "settings.language.option_en": "Anglais",
    "settings.language.option_es": "Espagnol",
    "settings.language.option_fr": "Francais",
  },
};

function widgetSizeLimits(type) {
  const compact = isCompactCanvas();
  const fallback = {
    minW: MIN_WIDGET_SIZE,
    minH: MIN_WIDGET_SIZE,
    maxW: CANVAS_WIDTH,
    maxH: CANVAS_HEIGHT,
  };

  switch (type) {
    case "sensor":
      return compact
        ? { minW: 90, minH: 60, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT }
        : { minW: 120, minH: 80, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT };
    case "button":
      return compact
        ? { minW: 82, minH: 82, maxW: 320, maxH: 260 }
        : { minW: 100, minH: 100, maxW: 480, maxH: 320 };
    case "slider":
      return compact
        ? { minW: 100, minH: 80, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT }
        : { minW: 100, minH: 100, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT };
    case "graph":
      return compact
        ? { minW: 150, minH: 100, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT }
        : { minW: 220, minH: 140, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT };
    case "empty_tile":
      return compact
        ? { minW: 100, minH: 70, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT }
        : { minW: 120, minH: 80, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT };
    case "light_tile":
      return compact
        ? { minW: 140, minH: 140, maxW: 480, maxH: 480 }
        : { minW: 180, minH: 180, maxW: 480, maxH: 480 };
    case "heating_tile":
      return compact
        ? { minW: 150, minH: 150, maxW: 480, maxH: 480 }
        : { minW: 220, minH: 200, maxW: 480, maxH: 480 };
    case "weather_tile":
      return compact
        ? { minW: 160, minH: 150, maxW: 480, maxH: 480 }
        : { minW: 220, minH: 200, maxW: 480, maxH: 480 };
    case "weather_3day":
      return compact
        ? { minW: 280, minH: 180, maxW: 640, maxH: 480 }
        : { minW: 260, minH: 220, maxW: 640, maxH: 480 };
    case "solar_forecast":
      return compact
        ? { minW: 280, minH: 180, maxW: 640, maxH: 480 }
        : { minW: 260, minH: 220, maxW: 640, maxH: 480 };
    case "todo_list":
      return compact
        ? { minW: 180, minH: 160, maxW: 640, maxH: 640 }
        : { minW: 220, minH: 200, maxW: 640, maxH: 640 };
    case "media_player":
      return compact
        ? { minW: 200, minH: 170, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT }
        : { minW: 260, minH: 220, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT };
    case "roborock_tile":
      return compact
        ? { minW: 220, minH: 190, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT }
        : { minW: 240, minH: 220, maxW: CANVAS_WIDTH, maxH: CANVAS_HEIGHT };
    default:
      return fallback;
  }
}

function clampRectToCanvas(rect, type) {
  const limits = widgetSizeLimits(type);
  const maxW = Math.min(limits.maxW, CANVAS_WIDTH);
  const maxH = Math.min(limits.maxH, CANVAS_HEIGHT);
  const minW = Math.min(limits.minW, maxW);
  const minH = Math.min(limits.minH, maxH);

  const w = clamp(snap(Number(rect.w || minW)), minW, maxW);
  const h = clamp(snap(Number(rect.h || minH)), minH, maxH);
  const x = clamp(snap(Number(rect.x || 0)), 0, CANVAS_WIDTH - w);
  const y = clamp(snap(Number(rect.y || 0)), 0, CANVAS_HEIGHT - h);

  return { x, y, w, h };
}

const editor = {
  layout: null,
  entities: [],
  states: new Map(),
  energySnapshot: null,
  selectedPageId: null,
  selectedWidgetId: null,
  activePane: "layout",
  activeSettingsSection: "settingsWifiSection",
  provisioningStage: null,
  editorStarted: false,
  settings: null,
  appVersion: "",
  appProject: "",
  appScreenW: 0,
  appScreenH: 0,
  haDiagnostics: { total: 0, listed: 0, updatedUnixMs: 0, names: [], dismissedSignature: "" },
  wifiScanItems: [],
  wifiScanHasRun: false,
  wifiScanInProgress: false,
  wifiScanSupported: true,
  lightPicker: {
    items: [],
    itemsByDomain: {},
    loadedByDomain: {},
    domain: "light",
    widgetType: "light_tile",
    search: "",
    searchByDomain: {},
    searchDebounceId: null,
    loading: false,
    hasLoaded: false,
    pollTimerId: null,
    requestSeq: 0,
    lastStatus: "",
    inspectorTarget: null,
    configOverride: null,
  },
  setupWizard: {
    active: false,
    openedManually: false,
    addedSinceOpen: 0,
  },
  ota: {
    status: null,
    pollTimerId: null,
    uploadInProgress: false,
    latestUrl: "",
    autoFilledUrl: "",
    latestUrlLoading: false,
  },
  languageCatalog: [],
  i18nLanguage: DEFAULT_UI_LANGUAGE,
  i18nMap: { ...(WEB_I18N_BUILTIN.en || {}) },
  i18nEffective: {},
  sectionCollapsed: {
    pages: false,
    widgets: false,
    inspector: false,
  },
};

const el = {
  provisioningRoot: document.getElementById("provisioningRoot"),
  provisioningWifiPage: document.getElementById("provisioningWifiPage"),
  provisioningHaPage: document.getElementById("provisioningHaPage"),
  editorShell: document.getElementById("editorShell"),
  sidebarTitleText: document.getElementById("sidebarTitleText"),
  appVersionLabel: document.getElementById("appVersionLabel"),
  layoutTabBtn: document.getElementById("layoutTabBtn"),
  settingsTabBtn: document.getElementById("settingsTabBtn"),
  layoutPane: document.getElementById("layoutPane"),
  settingsPane: document.getElementById("settingsPane"),
  settingsContentPane: document.getElementById("settingsContentPane"),
  settingsNavButtons: [],
  settingsContentSections: [],
  canvasWrap: document.querySelector(".canvas-wrap"),
  actionsPanel: document.querySelector("aside.actions-panel"),
  pagesList: document.getElementById("pagesList"),
  pagesMiniList: document.getElementById("pagesMiniList"),
  widgetsList: document.getElementById("widgetsList"),
  canvas: document.getElementById("canvas"),
  canvasTitle: document.getElementById("canvasTitle"),
  status: document.getElementById("status"),
  haDiagnosticsBanner: document.getElementById("haDiagnosticsBanner"),
  haDiagnosticsTitle: document.getElementById("haDiagnosticsTitle"),
  haDiagnosticsList: document.getElementById("haDiagnosticsList"),
  haDiagnosticsHint: document.getElementById("haDiagnosticsHint"),
  haDiagnosticsDismiss: document.getElementById("haDiagnosticsDismiss"),
  pagesSection: document.getElementById("pagesSection"),
  widgetsSection: document.getElementById("widgetsSection"),
  inspectorSection: document.getElementById("inspectorSection"),
  togglePagesSection: document.getElementById("togglePagesSection"),
  toggleWidgetsSection: document.getElementById("toggleWidgetsSection"),
  toggleInspectorSection: document.getElementById("toggleInspectorSection"),
  addPageBtn: document.getElementById("addPageBtn"),
  addEnergyPageBtn: document.getElementById("addEnergyPageBtn"),
  deletePageBtn: document.getElementById("deletePageBtn"),
  pageTitleInput: document.getElementById("pageTitleInput"),
  applyPageBtn: document.getElementById("applyPageBtn"),
  energyPageOptions: document.getElementById("energyPageOptions"),
  energySource: document.getElementById("energySource"),
  energySourceHint: document.getElementById("energySourceHint"),
  energyManualOptions: document.getElementById("energyManualOptions"),
  energyHomePower: document.getElementById("energyHomePower"),
  energySolarPower: document.getElementById("energySolarPower"),
  energyGridPower: document.getElementById("energyGridPower"),
  energyGridImport: document.getElementById("energyGridImport"),
  energyGridExport: document.getElementById("energyGridExport"),
  energyBatteryPower: document.getElementById("energyBatteryPower"),
  energyBatteryCharge: document.getElementById("energyBatteryCharge"),
  energyBatteryDischarge: document.getElementById("energyBatteryDischarge"),
  energyBatterySoc: document.getElementById("energyBatterySoc"),
  applyEnergyPageBtn: document.getElementById("applyEnergyPageBtn"),
  addSensorBtn: document.getElementById("addSensorBtn"),
  addButtonBtn: document.getElementById("addButtonBtn"),
  addSliderBtn: document.getElementById("addSliderBtn"),
  addGraphBtn: document.getElementById("addGraphBtn"),
  addEmptyTileBtn: document.getElementById("addEmptyTileBtn"),
  addLightTileBtn: document.getElementById("addLightTileBtn"),
  openSetupWizardBtn: document.getElementById("openSetupWizardBtn"),
  lightEntityPickerOverlay: document.getElementById("lightEntityPickerOverlay"),
  lightEntityPickerTitle: document.getElementById("lightEntityPickerTitle"),
  lightEntityPickerRefreshBtn: document.getElementById("lightEntityPickerRefreshBtn"),
  lightEntityPickerCloseBtn: document.getElementById("lightEntityPickerCloseBtn"),
  lightEntityPickerBlankBtn: document.getElementById("lightEntityPickerBlankBtn"),
  lightEntityPickerSearch: document.getElementById("lightEntityPickerSearch"),
  lightEntityPickerStatus: document.getElementById("lightEntityPickerStatus"),
  lightEntityPickerProgress: document.getElementById("lightEntityPickerProgress"),
  lightEntityPickerProgressBar: document.getElementById("lightEntityPickerProgressBar"),
  lightEntityPickerProgressText: document.getElementById("lightEntityPickerProgressText"),
  lightEntityPickerRooms: document.getElementById("lightEntityPickerRooms"),
  addHeatingTileBtn: document.getElementById("addHeatingTileBtn"),
  addWeatherTileBtn: document.getElementById("addWeatherTileBtn"),
  addWeather3DayBtn: document.getElementById("addWeather3DayBtn"),
  addSolarForecastBtn: document.getElementById("addSolarForecastBtn"),
  addTodoListBtn: document.getElementById("addTodoListBtn"),
  addMediaPlayerBtn: document.getElementById("addMediaPlayerBtn"),
  addRoborockTileBtn: document.getElementById("addRoborockTileBtn"),
  deleteWidgetBtn: document.getElementById("deleteWidgetBtn"),
  reloadBtn: document.getElementById("reloadBtn"),
  saveBtn: document.getElementById("saveBtn"),
  exportBtn: document.getElementById("exportBtn"),
  importBtn: document.getElementById("importBtn"),
  importFile: document.getElementById("importFile"),
  jsonPaste: document.getElementById("jsonPaste"),
  fTitle: document.getElementById("fTitle"),
  fType: document.getElementById("fType"),
  fEntityWrap: document.getElementById("fEntityWrap"),
  fEntity: document.getElementById("fEntity"),
  fSecondaryEntityWrap: document.getElementById("fSecondaryEntityWrap"),
  fSecondaryEntityLabel: document.getElementById("fSecondaryEntityLabel"),
  fSecondaryEntity: document.getElementById("fSecondaryEntity"),
  solarForecastFields: document.getElementById("solarForecastFields"),
  solarForecastStyleOptions: document.getElementById("solarForecastStyleOptions"),
  fSolarForecastTodayLabel: document.getElementById("fSolarForecastTodayLabel"),
  fSolarForecastToday: document.getElementById("fSolarForecastToday"),
  fSolarForecastTomorrowLabel: document.getElementById("fSolarForecastTomorrowLabel"),
  fSolarForecastTomorrow: document.getElementById("fSolarForecastTomorrow"),
  fSolarForecastDay3Label: document.getElementById("fSolarForecastDay3Label"),
  fSolarForecastDay3: document.getElementById("fSolarForecastDay3"),
  fSolarForecastDay4Label: document.getElementById("fSolarForecastDay4Label"),
  fSolarForecastDay4: document.getElementById("fSolarForecastDay4"),
  fSolarForecastDay5Label: document.getElementById("fSolarForecastDay5Label"),
  fSolarForecastDay5: document.getElementById("fSolarForecastDay5"),
  fSolarForecastBarMaxLabel: document.getElementById("fSolarForecastBarMaxLabel"),
  fSolarForecastBarMax: document.getElementById("fSolarForecastBarMax"),
  fSolarForecastBarOrientationLabel: document.getElementById("fSolarForecastBarOrientationLabel"),
  fSolarForecastBarOrientation: document.getElementById("fSolarForecastBarOrientation"),
  buttonOptions: document.getElementById("buttonOptions"),
  fButtonMode: document.getElementById("fButtonMode"),
  fSliderEntityDomain: document.getElementById("fSliderEntityDomain"),
  fButtonAccentColor: document.getElementById("fButtonAccentColor"),
  sliderOptions: document.getElementById("sliderOptions"),
  fSliderDirection: document.getElementById("fSliderDirection"),
  fSliderAccentColor: document.getElementById("fSliderAccentColor"),
  graphOptions: document.getElementById("graphOptions"),
  fGraphLineColor: document.getElementById("fGraphLineColor"),
  fGraphTimeWindowMin: document.getElementById("fGraphTimeWindowMin"),
  fGraphPointCount: document.getElementById("fGraphPointCount"),
  fGraphPointCountWrap: document.getElementById("fGraphPointCountWrap"),
  fGraphDisplayMode: document.getElementById("fGraphDisplayMode"),
  fGraphDisplayModeLabel: document.getElementById("fGraphDisplayModeLabel"),
  fGraphBarBucketMin: document.getElementById("fGraphBarBucketMin"),
  fGraphBarBucketMinLabel: document.getElementById("fGraphBarBucketMinLabel"),
  fGraphBarBucketMinWrap: document.getElementById("fGraphBarBucketMinWrap"),
  heatingOptions: document.getElementById("heatingOptions"),
  fHeatingStyleVariant: document.getElementById("fHeatingStyleVariant"),
  fHeatingArcOpening: document.getElementById("fHeatingArcOpening"),
  fHeatingArcOpeningWrap: document.getElementById("fHeatingArcOpeningWrap"),
  fX: document.getElementById("fX"),
  fY: document.getElementById("fY"),
  fW: document.getElementById("fW"),
  fH: document.getElementById("fH"),
  applyInspectorBtn: document.getElementById("applyInspectorBtn"),
  entityOptions: document.getElementById("entityOptions"),
  energyEntityOptions: document.getElementById("energyEntityOptions"),
  sensorEntityOptions: document.getElementById("sensorEntityOptions"),
  settingsWifiSsid: document.getElementById("settingsWifiSsid"),
  settingsWifiCountryCode: document.getElementById("settingsWifiCountryCode"),
  settingsWifiBssid: document.getElementById("settingsWifiBssid"),
  scanWifiBtn: document.getElementById("scanWifiBtn"),
  settingsWifiScanResults: document.getElementById("settingsWifiScanResults"),
  settingsWifiScanInfo: document.getElementById("settingsWifiScanInfo"),
  settingsWifiPassword: document.getElementById("settingsWifiPassword"),
  settingsHaUrl: document.getElementById("settingsHaUrl"),
  settingsHaToken: document.getElementById("settingsHaToken"),
  settingsHaRestEnabled: document.getElementById("settingsHaRestEnabled"),
  settingsTopbarWeatherEnabled: document.getElementById("settingsTopbarWeatherEnabled"),
  settingsTopbarWeatherEntity: document.getElementById("settingsTopbarWeatherEntity"),
  settingsWeatherEntityOptions: document.getElementById("settingsWeatherEntityOptions"),
  settingsStockEntity1: document.getElementById("settingsStockEntity1"),
  settingsStockEntity2: document.getElementById("settingsStockEntity2"),
  settingsStockEntity3: document.getElementById("settingsStockEntity3"),
  settingsStockEntityOptions: document.getElementById("settingsStockEntityOptions"),
  settingsHardwareActiveBrightness: document.getElementById("settingsHardwareActiveBrightness"),
  settingsHardwareActiveBrightnessValue: document.getElementById("settingsHardwareActiveBrightnessValue"),
  settingsHardwareDimBrightness: document.getElementById("settingsHardwareDimBrightness"),
  settingsHardwareDimBrightnessValue: document.getElementById("settingsHardwareDimBrightnessValue"),
  settingsHardwareDimTimeout: document.getElementById("settingsHardwareDimTimeout"),
  settingsNtpServer: document.getElementById("settingsNtpServer"),
  settingsTimezone: document.getElementById("settingsTimezone"),
  settingsLanguage: document.getElementById("settingsLanguage"),
  reloadLanguagesBtn: document.getElementById("reloadLanguagesBtn"),
  downloadLanguageBtn: document.getElementById("downloadLanguageBtn"),
  uploadLanguageCode: document.getElementById("uploadLanguageCode"),
  uploadLanguageFile: document.getElementById("uploadLanguageFile"),
  uploadLanguageBtn: document.getElementById("uploadLanguageBtn"),
  settingsTranslationInfo: document.getElementById("settingsTranslationInfo"),
  settingsWifiInfo: document.getElementById("settingsWifiInfo"),
  settingsHaInfo: document.getElementById("settingsHaInfo"),
  settingsWeatherInfo: document.getElementById("settingsWeatherInfo"),
  settingsStocksInfo: document.getElementById("settingsStocksInfo"),
  settingsHardwareInfo: document.getElementById("settingsHardwareInfo"),
  settingsTimeInfo: document.getElementById("settingsTimeInfo"),
  settingsUiInfo: document.getElementById("settingsUiInfo"),
  settingsApInfo: document.getElementById("settingsApInfo"),
  settingsOtaUrl: document.getElementById("settingsOtaUrl"),
  startOtaUrlBtn: document.getElementById("startOtaUrlBtn"),
  refreshOtaStatusBtn: document.getElementById("refreshOtaStatusBtn"),
  settingsOtaFile: document.getElementById("settingsOtaFile"),
  uploadOtaBtn: document.getElementById("uploadOtaBtn"),
  settingsOtaProgressBar: document.getElementById("settingsOtaProgressBar"),
  settingsOtaInfo: document.getElementById("settingsOtaInfo"),
  reloadSettingsBtn: document.getElementById("reloadSettingsBtn"),
  saveSettingsBtn: document.getElementById("saveSettingsBtn"),
  provWifiSsid: document.getElementById("provWifiSsid"),
  provWifiCountryCode: document.getElementById("provWifiCountryCode"),
  provScanWifiBtn: document.getElementById("provScanWifiBtn"),
  provWifiScanResults: document.getElementById("provWifiScanResults"),
  provWifiScanInfo: document.getElementById("provWifiScanInfo"),
  provWifiPassword: document.getElementById("provWifiPassword"),
  provWifiShowPassword: document.getElementById("provWifiShowPassword"),
  provWifiInfo: document.getElementById("provWifiInfo"),
  provWifiSaveBtn: document.getElementById("provWifiSaveBtn"),
  provHaUrl: document.getElementById("provHaUrl"),
  provHaToken: document.getElementById("provHaToken"),
  provHaShowToken: document.getElementById("provHaShowToken"),
  provHaInfo: document.getElementById("provHaInfo"),
  provHaSaveBtn: document.getElementById("provHaSaveBtn"),
  setupWizardOverlay: document.getElementById("setupWizardOverlay"),
  setupWizardTitle: document.getElementById("setupWizardTitle"),
  setupWizardCloseBtn: document.getElementById("setupWizardCloseBtn"),
  setupWizardStepHa: document.getElementById("setupWizardStepHa"),
  setupWizardStepTiles: document.getElementById("setupWizardStepTiles"),
  setupWizardStepSave: document.getElementById("setupWizardStepSave"),
  setupWizardSubtitle: document.getElementById("setupWizardSubtitle"),
  setupWizardPageTitle: document.getElementById("setupWizardPageTitle"),
  setupWizardCount: document.getElementById("setupWizardCount"),
  setupWizardStatus: document.getElementById("setupWizardStatus"),
  setupWizardAddLightBtn: document.getElementById("setupWizardAddLightBtn"),
  setupWizardAddHeatingBtn: document.getElementById("setupWizardAddHeatingBtn"),
  setupWizardAddWeatherBtn: document.getElementById("setupWizardAddWeatherBtn"),
  setupWizardAddButtonBtn: document.getElementById("setupWizardAddButtonBtn"),
  setupWizardAddSensorBtn: document.getElementById("setupWizardAddSensorBtn"),
  setupWizardSkipBtn: document.getElementById("setupWizardSkipBtn"),
  setupWizardSaveBtn: document.getElementById("setupWizardSaveBtn"),
  setupWizardDoneBtn: document.getElementById("setupWizardDoneBtn"),
};

const entityAutocomplete = {
  primary: {
    timerId: null,
    requestSeq: 0,
  },
  secondary: {
    timerId: null,
    requestSeq: 0,
  },
  settingsWeather: {
    timerId: null,
    requestSeq: 0,
  },
  settingsStocks: {
    timerId: null,
    requestSeq: 0,
  },
};

function normalizeSliderDirection(value) {
  return SLIDER_DIRECTIONS.has(value) ? value : DEFAULT_SLIDER_DIRECTION;
}

function normalizeSliderEntityDomain(value) {
  return SLIDER_ENTITY_DOMAINS.has(value) ? value : DEFAULT_SLIDER_ENTITY_DOMAIN;
}

function normalizeButtonMode(value) {
  return BUTTON_MODES.has(value) ? value : DEFAULT_BUTTON_MODE;
}

function buttonModeRequiresMediaPlayer(value) {
  const mode = normalizeButtonMode(value);
  return mode === "play_pause" || mode === "stop" || mode === "next" || mode === "previous";
}

function normalizeHexColor(value, fallback = DEFAULT_SLIDER_ACCENT_COLOR) {
  const source = (typeof value === "string" ? value : "").trim();
  const fallbackNorm = typeof fallback === "string" ? fallback.trim().toLowerCase() : DEFAULT_SLIDER_ACCENT_COLOR;
  if (!source) return fallbackNorm;

  let hex = source.toLowerCase();
  if (hex.startsWith("0x")) {
    hex = `#${hex.slice(2)}`;
  }
  if (!hex.startsWith("#")) {
    hex = `#${hex}`;
  }

  if (/^#[0-9a-f]{6}$/.test(hex)) {
    return hex;
  }
  return fallbackNorm;
}

function normalizeGraphPointCount(value) {
  if (value === null || value === undefined) return 0;
  if (typeof value === "string" && value.trim() === "") return 0;

  const parsed = Number(value);
  if (!Number.isFinite(parsed)) return 0;

  const rounded = Math.round(parsed);
  if (rounded <= 0) return 0;
  return clamp(rounded, GRAPH_POINTS_MIN, GRAPH_POINTS_MAX);
}

function normalizeGraphTimeWindowMin(value) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) return DEFAULT_GRAPH_TIME_WINDOW_MIN;
  const rounded = Math.round(parsed);
  if (rounded <= 0) return DEFAULT_GRAPH_TIME_WINDOW_MIN;
  return clamp(rounded, GRAPH_TIME_WINDOW_MIN, GRAPH_TIME_WINDOW_MAX);
}

function normalizeGraphDisplayMode(value) {
  if (typeof value === "string" && GRAPH_DISPLAY_MODES.includes(value)) {
    return value;
  }
  return DEFAULT_GRAPH_DISPLAY_MODE;
}

function normalizeGraphBarBucketMin(value) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) return DEFAULT_GRAPH_BAR_BUCKET_MIN;
  const rounded = Math.round(parsed);
  if (GRAPH_BAR_BUCKET_MIN_OPTIONS.includes(rounded)) return rounded;
  return DEFAULT_GRAPH_BAR_BUCKET_MIN;
}

function normalizeSolarForecastBarOrientation(value) {
  return SOLAR_FORECAST_BAR_ORIENTATIONS.includes(value) ? value : DEFAULT_SOLAR_FORECAST_BAR_ORIENTATION;
}

function normalizeLayoutWidgets(layout) {
  if (!layout || !Array.isArray(layout.pages)) return;
  for (const page of layout.pages) {
    if (isEnergyPage(page)) {
      normalizeEnergyConfig(page);
      continue;
    }
    if (!page || !Array.isArray(page.widgets)) continue;
    for (const widget of page.widgets) {
      if (!widget || typeof widget !== "object") continue;
      if (widget.type === "button") {
        widget.button_accent_color = normalizeHexColor(widget.button_accent_color, DEFAULT_BUTTON_ACCENT_COLOR);
        const buttonMode = normalizeButtonMode(widget.button_mode);
        if (buttonModeRequiresMediaPlayer(buttonMode) && !String(widget.entity_id || "").startsWith("media_player.")) {
          widget.button_mode = DEFAULT_BUTTON_MODE;
        } else {
          widget.button_mode = buttonMode;
        }
      }
      if (widget.type === "slider") {
        widget.slider_direction = normalizeSliderDirection(widget.slider_direction);
        widget.slider_accent_color = normalizeHexColor(widget.slider_accent_color, DEFAULT_SLIDER_ACCENT_COLOR);
        widget.slider_entity_domain = normalizeSliderEntityDomain(widget.slider_entity_domain);
      }
      if (widget.type === "graph") {
        widget.graph_line_color = normalizeHexColor(widget.graph_line_color, DEFAULT_GRAPH_LINE_COLOR);
        widget.graph_time_window_min = normalizeGraphTimeWindowMin(widget.graph_time_window_min);
        widget.graph_display_mode = normalizeGraphDisplayMode(widget.graph_display_mode);
        widget.graph_bar_bucket_min = normalizeGraphBarBucketMin(widget.graph_bar_bucket_min);
        const normalizedGraphPoints = normalizeGraphPointCount(widget.graph_point_count);
        if (normalizedGraphPoints > 0) {
          widget.graph_point_count = normalizedGraphPoints;
        } else {
          delete widget.graph_point_count;
        }
      }
      if (widget.type === "solar_forecast") {
        widget.entity_id = widget.entity_id || DEFAULT_SOLCAST_ENTITIES.remaining;
        widget.forecast_today_entity_id = widget.forecast_today_entity_id || DEFAULT_SOLCAST_ENTITIES.today;
        widget.forecast_tomorrow_entity_id = widget.forecast_tomorrow_entity_id || DEFAULT_SOLCAST_ENTITIES.tomorrow;
        widget.forecast_day_3_entity_id = widget.forecast_day_3_entity_id || DEFAULT_SOLCAST_ENTITIES.day3;
        widget.forecast_day_4_entity_id = widget.forecast_day_4_entity_id || DEFAULT_SOLCAST_ENTITIES.day4;
        widget.forecast_day_5_entity_id = widget.forecast_day_5_entity_id || DEFAULT_SOLCAST_ENTITIES.day5;
        widget.solar_forecast_bar_orientation = normalizeSolarForecastBarOrientation(widget.solar_forecast_bar_orientation);
        const barMax = Number(widget.solar_forecast_bar_max_kwh);
        if (Number.isFinite(barMax) && barMax > 0) {
          widget.solar_forecast_bar_max_kwh = barMax;
        } else {
          delete widget.solar_forecast_bar_max_kwh;
        }
        delete widget.secondary_entity_id;
      }
    }
  }
}

function setStatus(text, isError = false) {
  el.status.textContent = text;
  el.status.style.color = isError ? "#ff8f94" : "#9db0c3";
}

function getWifiScanUi(scope = "settings") {
  if (scope === "provisioning") {
    return {
      scanButton: el.provScanWifiBtn,
      ssidInput: el.provWifiSsid,
      resultsSelect: el.provWifiScanResults,
      info: el.provWifiScanInfo,
    };
  }

  return {
    scanButton: el.scanWifiBtn,
    ssidInput: el.settingsWifiSsid,
    resultsSelect: el.settingsWifiScanResults,
    info: el.settingsWifiScanInfo,
  };
}

function setWifiScanInfo(text, isError = false, scope = "settings") {
  const ui = getWifiScanUi(scope);
  if (!ui.info) return;
  ui.info.textContent = text;
  ui.info.classList.toggle("error", isError);
}

function setProvisioningInfo(stage, text, isError = false) {
  const target = stage === "wifi" ? el.provWifiInfo : el.provHaInfo;
  if (!target) return;
  target.textContent = text;
  target.classList.toggle("error", isError);
}

function normalizeCountryCode(value) {
  const normalized = (value || "").trim().toUpperCase();
  return /^[A-Z]{2}$/.test(normalized) ? normalized : "";
}

function normalizeBssid(value) {
  const normalized = (value || "").trim().toUpperCase().replace(/-/g, ":");
  if (!normalized) return "";
  return /^[0-9A-F]{2}(:[0-9A-F]{2}){5}$/.test(normalized) ? normalized : "";
}

function normalizeLanguageCode(value, fallback = "") {
  const normalized = (typeof value === "string" ? value : "").trim().toLowerCase();
  if (!normalized) return fallback;
  return LANGUAGE_CODE_RE.test(normalized) ? normalized : fallback;
}

function normalizeUiLanguage(value) {
  return normalizeLanguageCode(value, DEFAULT_UI_LANGUAGE);
}

function templateString(text, vars = {}) {
  return String(text).replace(/\{([a-zA-Z0-9_]+)\}/g, (match, key) => {
    if (!Object.prototype.hasOwnProperty.call(vars, key)) return match;
    return String(vars[key]);
  });
}

function t(key, vars = {}, fallbackText = null) {
  const source = editor.i18nMap || {};
  const fallback = WEB_I18N_BUILTIN.en || {};
  const raw = source[key] ?? fallback[key] ?? fallbackText ?? key;
  return templateString(raw, vars);
}

function storageGet(key) {
  try {
    return window.localStorage?.getItem(key) || "";
  } catch (_) {
    return "";
  }
}

function storageSet(key, value) {
  try {
    window.localStorage?.setItem(key, value);
  } catch (_) {}
}

function storageRemove(key) {
  try {
    window.localStorage?.removeItem(key);
  } catch (_) {}
}

function flattenTranslationObject(obj, prefix = "", out = {}) {
  if (!obj || typeof obj !== "object") return out;
  for (const [key, value] of Object.entries(obj)) {
    const path = prefix ? `${prefix}.${key}` : key;
    if (value && typeof value === "object" && !Array.isArray(value)) {
      flattenTranslationObject(value, path, out);
    } else if (typeof value === "string") {
      out[path] = value;
    }
  }
  return out;
}

function setTextById(id, key, vars) {
  const node = document.getElementById(id);
  if (!node) return;
  node.textContent = t(key, vars);
}

function setPlaceholderById(id, key, vars) {
  const node = document.getElementById(id);
  if (!node) return;
  node.placeholder = t(key, vars);
}

function renderAppVersion() {
  const version = typeof editor.appVersion === "string" ? editor.appVersion.trim() : "";
  if (el.appVersionLabel) {
    el.appVersionLabel.textContent = version;
    el.appVersionLabel.hidden = version.length === 0;
  }
  document.title = version
    ? `BETTA HA Panel - BETTA Editor ${version}`
    : "BETTA HA Panel - BETTA Editor";
}

async function loadAppVersion() {
  try {
    const payload = await apiGet("/api/version");
    editor.appVersion = typeof payload?.version === "string" ? payload.version : "";
    editor.appProject = typeof payload?.project === "string" ? payload.project : "";
    editor.appScreenW = Number(payload?.screen_w) || 0;
    editor.appScreenH = Number(payload?.screen_h) || 0;
    applyCanvasGeometry(payload);
  } catch (_) {
    editor.appVersion = "";
    editor.appProject = "";
    editor.appScreenW = 0;
    editor.appScreenH = 0;
  }
  renderAppVersion();
  void refreshLatestOtaUrl();
}

function otaPanelVariant() {
  const project = (editor.appProject || "").toLowerCase();
  if (project.includes("paneljc") || project.includes("panel-jc") || project.includes("jc8012")) {
    return "paneljc";
  }
  if (project.includes("s3") || editor.appScreenW === 480) {
    return "panels3";
  }
  if (project.includes("10.1") || project.includes("panel10") || editor.appScreenW >= 1000) {
    return "panel10";
  }
  return "panel4";
}

function otaCurrentVersionTag() {
  const version = (editor.appVersion || "").trim();
  const match = version.match(/^v\d+\.\d+\.\d+/);
  return match ? match[0] : "";
}

function otaLatestFallbackUrl() {
  const version = otaCurrentVersionTag();
  if (!version) return "";
  const variant = otaPanelVariant();
  return `https://github.com/${OTA_RELEASE_REPO}/releases/latest/download/betta86-ha-panel-${version}-${variant}.ota.bin`;
}

function applyLatestOtaUrl(url) {
  if (!url) return;
  const previousAuto = editor.ota.autoFilledUrl || "";
  editor.ota.latestUrl = url;
  if (el.settingsOtaUrl) {
    el.settingsOtaUrl.placeholder = url;
    const current = el.settingsOtaUrl.value.trim();
    if (!current || current === previousAuto) {
      el.settingsOtaUrl.value = url;
      editor.ota.autoFilledUrl = url;
    }
  }
}

async function refreshLatestOtaUrl() {
  if (editor.ota.latestUrlLoading) return;

  const fallback = otaLatestFallbackUrl();
  if (fallback) {
    applyLatestOtaUrl(fallback);
  }

  editor.ota.latestUrlLoading = true;
  try {
    const response = await fetch(`https://api.github.com/repos/${OTA_RELEASE_REPO}/releases/latest`, {
      cache: "no-store",
      headers: { Accept: "application/vnd.github+json" },
    });
    if (!response.ok) return;
    const payload = await response.json();
    const assets = Array.isArray(payload?.assets) ? payload.assets : [];
    const variant = otaPanelVariant();
    const suffix = `-${variant}.ota.bin`;
    const asset = assets.find((item) =>
      typeof item?.name === "string" &&
      item.name.startsWith("betta86-ha-panel-") &&
      item.name.endsWith(suffix)
    );
    if (asset?.name) {
      applyLatestOtaUrl(`https://github.com/${OTA_RELEASE_REPO}/releases/latest/download/${asset.name}`);
    } else if (typeof asset?.browser_download_url === "string") {
      applyLatestOtaUrl(asset.browser_download_url);
    }
  } catch (_) {
    /* Keep the firmware-version fallback URL. */
  } finally {
    editor.ota.latestUrlLoading = false;
  }
}

async function loadHaDiagnostics() {
  try {
    const payload = await apiGet("/api/ha/diagnostics");
    if (!payload || typeof payload !== "object") return;
    const names = Array.isArray(payload.missing_entities)
      ? payload.missing_entities.filter((n) => typeof n === "string")
      : [];
    editor.haDiagnostics.total = Number(payload.missing_total) || 0;
    editor.haDiagnostics.listed = Number(payload.missing_listed) || names.length;
    editor.haDiagnostics.updatedUnixMs = Number(payload.updated_unix_ms) || 0;
    editor.haDiagnostics.names = names;
  } catch (_) {
    /* keep previous state */
  }
  renderHaDiagnosticsBanner();
}

function haDiagnosticsSignature() {
  const d = editor.haDiagnostics;
  if (!d || !d.total) return "";
  const names = Array.isArray(d.names) ? [...d.names].sort().join("|") : "";
  return `${d.total}:${names}`;
}

function renderHaDiagnosticsBanner() {
  const banner = el.haDiagnosticsBanner;
  if (!banner) return;
  const d = editor.haDiagnostics;
  const signature = haDiagnosticsSignature();
  if (!d || !d.total || !signature) {
    banner.hidden = true;
    return;
  }
  if (d.dismissedSignature && d.dismissedSignature === signature) {
    banner.hidden = true;
    return;
  }
  if (el.haDiagnosticsTitle) {
    if (d.total > d.listed && d.listed > 0) {
      el.haDiagnosticsTitle.textContent = t("ha_diagnostics.missing_title_more", {
        total: d.total,
        listed: d.listed,
      });
    } else {
      el.haDiagnosticsTitle.textContent = t("ha_diagnostics.missing_title");
    }
  }
  if (el.haDiagnosticsHint) {
    el.haDiagnosticsHint.textContent = t("ha_diagnostics.missing_hint");
  }
  if (el.haDiagnosticsDismiss) {
    el.haDiagnosticsDismiss.setAttribute("aria-label", t("ha_diagnostics.dismiss"));
    el.haDiagnosticsDismiss.title = t("ha_diagnostics.dismiss");
  }
  if (el.haDiagnosticsList) {
    el.haDiagnosticsList.innerHTML = "";
    for (const name of Array.isArray(d.names) ? d.names : []) {
      const li = document.createElement("li");
      li.textContent = name;
      el.haDiagnosticsList.appendChild(li);
    }
  }
  banner.hidden = false;
}

function applyCanvasGeometry(payload) {
  if (!payload || typeof payload !== "object") return;
  const w = Number(payload.canvas_w);
  const h = Number(payload.canvas_h);
  if (!Number.isFinite(w) || !Number.isFinite(h) || w <= 0 || h <= 0) return;
  if (w === CANVAS_WIDTH && h === CANVAS_HEIGHT) return;
  CANVAS_WIDTH = Math.round(w);
  CANVAS_HEIGHT = Math.round(h);
  if (el.canvas) {
    el.canvas.style.width = `${CANVAS_WIDTH}px`;
    el.canvas.style.height = `${CANVAS_HEIGHT}px`;
  }
}

function setSelectOptionText(select, value, key, vars) {
  if (!select || !select.options) return;
  for (const option of select.options) {
    if (option.value === value) {
      option.textContent = t(key, vars);
      return;
    }
  }
}

function renderLanguageOptions() {
  if (!el.settingsLanguage) return;

  const optionCodes = new Set();
  optionCodes.add(DEFAULT_UI_LANGUAGE);
  optionCodes.add("en");
  optionCodes.add("de");
  optionCodes.add("es");
  optionCodes.add("fr");
  optionCodes.add(editor.i18nLanguage || DEFAULT_UI_LANGUAGE);
  for (const language of editor.languageCatalog || []) {
    const code = normalizeLanguageCode(language?.code, "");
    if (code) optionCodes.add(code);
  }

  const current = normalizeUiLanguage(editor.i18nLanguage || el.settingsLanguage.value);
  const sorted = Array.from(optionCodes).sort((a, b) => a.localeCompare(b));
  el.settingsLanguage.innerHTML = "";
  for (const code of sorted) {
    const option = document.createElement("option");
    option.value = code;
    const optionKey = `settings.language.option_${code}`;
    option.textContent = (editor.i18nMap && editor.i18nMap[optionKey]) || code.toUpperCase();
    el.settingsLanguage.appendChild(option);
  }
  el.settingsLanguage.value = sorted.includes(current) ? current : (sorted[0] || DEFAULT_UI_LANGUAGE);

  if (el.uploadLanguageCode) {
    el.uploadLanguageCode.value = el.settingsLanguage.value;
  }
}

async function loadLanguageCatalog() {
  const payload = await apiGet("/api/i18n/languages");
  if (!payload || !Array.isArray(payload.languages)) {
    throw new Error("Invalid language catalog");
  }
  editor.languageCatalog = payload.languages;
  return payload;
}

async function loadEffectiveTranslation(language) {
  const lang = normalizeUiLanguage(language);
  const response = await fetch(`/api/i18n/effective?lang=${encodeURIComponent(lang)}`, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`${response.status} ${response.statusText}`);
  }
  return response.json();
}

async function loadI18nLanguage(language, refreshCatalog = false) {
  const lang = normalizeUiLanguage(language);

  if (refreshCatalog || !Array.isArray(editor.languageCatalog) || editor.languageCatalog.length === 0) {
    try {
      await loadLanguageCatalog();
    } catch (_) {}
  }

  let effective = {};
  try {
    effective = await loadEffectiveTranslation(lang);
  } catch (_) {
    effective = {};
  }

  const builtin = WEB_I18N_BUILTIN[lang] || {};
  const webCustom = flattenTranslationObject(effective?.web || {});
  editor.i18nMap = {
    ...(WEB_I18N_BUILTIN.en || {}),
    ...builtin,
    ...webCustom,
  };
  editor.i18nEffective = effective || {};
  editor.i18nLanguage = lang;
  document.documentElement.lang = lang;

  applyWebTranslations();
  renderLanguageOptions();
  if (editor.layout) {
    renderCanvas();
  }
}

function applyWebTranslations() {
  setTextById("layoutTabBtn", "tabs.layout");
  setTextById("settingsTabBtn", "tabs.settings");
  setTextById("sidebarTitleText", "sidebar.title");
  setTextById("sidebarSubtitle", "sidebar.subtitle");
  renderAppVersion();

  setTextById("pagesHeading", "layout.pages.heading");
  setTextById("addPageBtn", "layout.pages.add");
  setTextById("addEnergyPageBtn", "layout.pages.add_energy");
  setTextById("deletePageBtn", "layout.pages.delete");
  setTextById("pageTitleLabel", "layout.pages.title_label");
  setPlaceholderById("pageTitleInput", "layout.pages.title_placeholder");
  setTextById("applyPageBtn", "layout.pages.apply_title");
  setTextById("energyPageHeading", "layout.energy.heading");
  setTextById("energyPageHint", "layout.energy.hint");
  setTextById("energySourceLabel", "layout.energy.source");
  setTextById("energySourceHaOption", "layout.energy.source_ha");
  setTextById("energySourceManualOption", "layout.energy.source_manual");
  setTextById("energyHomePowerLabel", "layout.energy.home_power");
  setTextById("energySolarPowerLabel", "layout.energy.solar_power");
  setTextById("energyGridPowerLabel", "layout.energy.grid_power");
  setTextById("energyGridImportLabel", "layout.energy.grid_import");
  setTextById("energyGridExportLabel", "layout.energy.grid_export");
  setTextById("energyBatteryPowerLabel", "layout.energy.battery_power");
  setTextById("energyBatteryChargeLabel", "layout.energy.battery_charge");
  setTextById("energyBatteryDischargeLabel", "layout.energy.battery_discharge");
  setTextById("energyBatterySocLabel", "layout.energy.battery_soc");
  setTextById("applyEnergyPageBtn", "layout.energy.apply");

  setTextById("widgetsHeading", "layout.widgets.heading");
  setTextById("addSensorBtn", "layout.widgets.add_sensor");
  setTextById("addButtonBtn", "layout.widgets.add_button");
  setTextById("addSliderBtn", "layout.widgets.add_slider");
  setTextById("addGraphBtn", "layout.widgets.add_graph");
  setTextById("addEmptyTileBtn", "layout.widgets.add_empty_tile");
  setTextById("addLightTileBtn", "layout.widgets.add_light_tile");
  setTextById("openSetupWizardBtn", "layout.widgets.quick_setup");
  setTextById("addHeatingTileBtn", "layout.widgets.add_heating_tile");
  setTextById("addWeatherTileBtn", "layout.widgets.add_weather_tile");
  setTextById("addWeather3DayBtn", "layout.widgets.add_weather_3day");
  setTextById("addSolarForecastBtn", "layout.widgets.add_solar_forecast");
  setTextById("addTodoListBtn", "layout.widgets.add_todo");
  setTextById("addMediaPlayerBtn", "layout.widgets.add_media_player");
  setTextById("addRoborockTileBtn", "layout.widgets.add_roborock");
  setTextById("deleteWidgetBtn", "layout.widgets.delete");
  setTextById("lightEntityPickerTitle", "entity_picker.title");
  setTextById("lightEntityPickerRefreshBtn", "entity_picker.refresh");
  setTextById("lightEntityPickerCloseBtn", "entity_picker.close");
  setTextById("lightEntityPickerBlankBtn", "entity_picker.blank");
  setPlaceholderById("lightEntityPickerSearch", "entity_picker.search_placeholder");

  setTextById("inspectorHeading", "layout.inspector.heading");
  setTextById("fTitleLabel", "layout.inspector.title");
  setTextById("fEntityLabel", "layout.inspector.entity");
  setTextById("fSecondaryEntityLabel", "layout.inspector.secondary_entity");
  setTextById("fSolarForecastTodayLabel", "layout.inspector.solar_today");
  setTextById("fSolarForecastTomorrowLabel", "layout.inspector.solar_tomorrow");
  setTextById("fSolarForecastDay3Label", "layout.inspector.solar_day_3");
  setTextById("fSolarForecastDay4Label", "layout.inspector.solar_day_4");
  setTextById("fSolarForecastDay5Label", "layout.inspector.solar_day_5");
  setTextById("fSolarForecastBarMaxLabel", "layout.inspector.solar_bar_max");
  setTextById("fSolarForecastBarOrientationLabel", "layout.inspector.solar_bar_orientation");
  document.querySelectorAll("[data-inspector-entity-target]").forEach((button) => {
    button.textContent = t("entity_picker.search");
    button.setAttribute("aria-label", t("entity_picker.search"));
  });
  setTextById("fButtonModeLabel", "layout.inspector.button_mode");
  setTextById("fButtonAccentColorLabel", "layout.inspector.button_accent_color");
  setTextById("fSliderEntityDomainLabel", "layout.inspector.slider_entity_domain");
  setTextById("fSliderDirectionLabel", "layout.inspector.slider_direction");
  setTextById("fSliderAccentColorLabel", "layout.inspector.slider_accent_color");
  setTextById("fGraphLineColorLabel", "layout.inspector.graph_line_color");
  setTextById("fGraphTimeWindowMinLabel", "layout.inspector.graph_time_window_min");
  setTextById("fGraphPointCountLabel", "layout.inspector.graph_point_count");
  setTextById("fGraphDisplayModeLabel", "layout.inspector.graph_display_mode");
  setTextById("fGraphBarBucketMinLabel", "layout.inspector.graph_bar_bucket_min");
  setSelectOptionText(el.fGraphDisplayMode, "line", "layout.option.graph_display_mode.line");
  setSelectOptionText(el.fGraphDisplayMode, "line_smooth_points", "layout.option.graph_display_mode.line_smooth_points");
  setSelectOptionText(el.fGraphDisplayMode, "line_smooth", "layout.option.graph_display_mode.line_smooth");
  setSelectOptionText(el.fGraphDisplayMode, "bars", "layout.option.graph_display_mode.bars");
  setSelectOptionText(el.fSolarForecastBarOrientation, "horizontal", "layout.option.solar_bar_orientation.horizontal");
  setSelectOptionText(el.fSolarForecastBarOrientation, "vertical", "layout.option.solar_bar_orientation.vertical");
  setTextById("applyInspectorBtn", "layout.inspector.apply");
  setSelectOptionText(el.fButtonMode, "auto", "layout.option.button_mode.auto");
  setSelectOptionText(el.fButtonMode, "play_pause", "layout.option.button_mode.play_pause");
  setSelectOptionText(el.fButtonMode, "stop", "layout.option.button_mode.stop");
  setSelectOptionText(el.fButtonMode, "next", "layout.option.button_mode.next");
  setSelectOptionText(el.fButtonMode, "previous", "layout.option.button_mode.previous");
  setSelectOptionText(el.fSliderEntityDomain, "auto", "layout.option.slider_entity_domain.auto");
  setSelectOptionText(el.fSliderEntityDomain, "light", "layout.option.slider_entity_domain.light");
  setSelectOptionText(el.fSliderEntityDomain, "media_player", "layout.option.slider_entity_domain.media_player");
  setSelectOptionText(el.fSliderEntityDomain, "cover", "layout.option.slider_entity_domain.cover");
  setSelectOptionText(el.fSliderDirection, "auto", "layout.option.slider_direction.auto");
  setSelectOptionText(el.fSliderDirection, "left_to_right", "layout.option.slider_direction.left_to_right");
  setSelectOptionText(el.fSliderDirection, "right_to_left", "layout.option.slider_direction.right_to_left");
  setSelectOptionText(el.fSliderDirection, "bottom_to_top", "layout.option.slider_direction.bottom_to_top");
  setSelectOptionText(el.fSliderDirection, "top_to_bottom", "layout.option.slider_direction.top_to_bottom");

  setTextById("layoutActionsHeading", "layout.actions.heading");
  setTextById("reloadBtn", "layout.actions.reload");
  setTextById("saveBtn", "layout.actions.save");
  setTextById("exportBtn", "layout.actions.export");
  setTextById("importBtn", "layout.actions.import");
  setPlaceholderById("jsonPaste", "layout.actions.paste_placeholder");

  setTextById("provWifiTitle", "provision.wifi.title");
  setTextById("provWifiSubtitle", "provision.wifi.subtitle");
  setTextById("provWifiSsidLabel", "provision.wifi.ssid");
  setTextById("provWifiCountryCodeLabel", "provision.wifi.country_code");
  setTextById("provWifiPasswordLabel", "provision.wifi.password");
  setPlaceholderById("provWifiPassword", "provision.wifi.password_placeholder");
  setTextById("provWifiShowPasswordLabel", "provision.wifi.show_password");
  setTextById("provScanWifiBtn", "common.scan");
  setTextById("provHaTitle", "provision.ha.title");
  setTextById("provHaSubtitle", "provision.ha.subtitle");
  setTextById("provHaUrlLabel", "provision.ha.ws_url");
  setTextById("provHaTokenLabel", "provision.ha.token");
  setTextById("provHaShowTokenLabel", "provision.ha.show_token");
  setTextById("provWifiSaveBtn", "common.save_reboot");
  setTextById("provHaSaveBtn", "common.save_reboot");

  setTextById("settingsWifiHeading", "settings.wifi.heading");
  setTextById("settingsWifiSsidLabel", "settings.wifi.ssid");
  setTextById("settingsWifiCountryCodeLabel", "settings.wifi.country_code");
  setTextById("settingsWifiBssidLabel", "settings.wifi.bssid");
  setTextById("settingsWifiPasswordLabel", "settings.wifi.password");
  setPlaceholderById("settingsWifiPassword", "settings.wifi.password_placeholder");
  setTextById("scanWifiBtn", "common.scan_wifi");

  setTextById("settingsHaHeading", "settings.ha.heading");
  setTextById("settingsHaUrlLabel", "settings.ha.ws_url");
  setTextById("settingsHaTokenLabel", "settings.ha.token");
  setPlaceholderById("settingsHaToken", "settings.ha.token_placeholder");
  setTextById("settingsHaRestEnabledLabel", "settings.ha.rest_fallback");

  setTextById("settingsWeatherHeading", "settings.weather.heading");
  setTextById("settingsTopbarWeatherEnabledLabel", "settings.weather.topbar_enabled");
  setTextById("settingsTopbarWeatherEntityLabel", "settings.weather.entity");

  setTextById("settingsStocksHeading", "settings.stocks.heading");
  setTextById("settingsStockEntity1Label", "settings.stocks.entity_1");
  setTextById("settingsStockEntity2Label", "settings.stocks.entity_2");
  setTextById("settingsStockEntity3Label", "settings.stocks.entity_3");

  setTextById("settingsHardwareHeading", "settings.hardware.heading");
  setTextById("settingsHardwareActiveBrightnessLabel", "settings.hardware.active_brightness");
  setTextById("settingsHardwareDimBrightnessLabel", "settings.hardware.dim_brightness");
  setTextById("settingsHardwareDimTimeoutLabel", "settings.hardware.dim_timeout");

  setTextById("settingsTimeHeading", "settings.time.heading");
  setTextById("settingsNtpServerLabel", "settings.time.ntp_server");
  setTextById("settingsTimezoneLabel", "settings.time.timezone");

  setTextById("settingsUiHeading", "settings.ui.heading");
  setTextById("settingsLanguageLabel", "settings.ui.language");
  setTextById("reloadLanguagesBtn", "settings.ui.reload_languages");
  setTextById("downloadLanguageBtn", "settings.ui.download_json");
  setTextById("uploadLanguageCodeLabel", "settings.ui.upload_code");
  setTextById("uploadLanguageFileLabel", "settings.ui.upload_file");
  setTextById("uploadLanguageBtn", "settings.ui.upload_button");

  setTextById("settingsApHeading", "settings.ap.heading");
  const apHint = document.getElementById("settingsApHint");
  if (apHint) apHint.innerHTML = t("settings.ap.hint");

  setTextById("settingsOtaHeading", "settings.ota.heading");
  setTextById("settingsOtaUrlLabel", "settings.ota.url");
  setPlaceholderById("settingsOtaUrl", "settings.ota.url_placeholder");
  setTextById("startOtaUrlBtn", "settings.ota.flash_url");
  setTextById("refreshOtaStatusBtn", "settings.ota.refresh");
  setTextById("settingsOtaFileLabel", "settings.ota.file");
  setTextById("uploadOtaBtn", "settings.ota.upload");

  setTextById("settingsActionsHeading", "settings.actions.heading");
  setTextById("reloadSettingsBtn", "settings.actions.reload");
  setTextById("saveSettingsBtn", "settings.actions.save");
  setTextById("settingsActionsHint", "settings.actions.hint");
  setTextById("setupWizardTitle", "setup.title");
  setTextById("setupWizardCloseBtn", "setup.close");
  setTextById("setupWizardStepHa", "setup.step_ha");
  setTextById("setupWizardStepTiles", "setup.step_tiles");
  setTextById("setupWizardStepSave", "setup.step_save");
  setTextById("setupWizardSubtitle", "setup.subtitle");
  setTextById("setupWizardPageLabel", "setup.page_label");
  setPlaceholderById("setupWizardPageTitle", "setup.page_placeholder");
  setTextById("setupWizardAddLightBtn", "setup.add_light");
  setTextById("setupWizardAddHeatingBtn", "setup.add_heating");
  setTextById("setupWizardAddWeatherBtn", "setup.add_weather");
  setTextById("setupWizardAddButtonBtn", "setup.add_button");
  setTextById("setupWizardAddSensorBtn", "setup.add_sensor");
  setTextById("setupWizardSkipBtn", "setup.skip");
  setTextById("setupWizardDoneBtn", "setup.done");
  setTextById("setupWizardSaveBtn", "setup.save");
  applySettingsNavTranslations();

  if (el.settingsTranslationInfo && !el.settingsTranslationInfo.classList.contains("error")) {
    el.settingsTranslationInfo.textContent = t("settings.translation.info");
  }

  if (el.uploadLanguageCode) {
    el.uploadLanguageCode.placeholder = "fr";
  }
  if (editor.setupWizard.active) {
    renderSetupWizard();
  }
}

function downloadLanguageJson() {
  const lang = normalizeUiLanguage(el.settingsLanguage?.value || editor.i18nLanguage);
  const web = {};
  for (const [key, value] of Object.entries(editor.i18nMap || {})) {
    web[key] = value;
  }

  const payload = {
    meta: {
      code: lang,
      exported_at: new Date().toISOString(),
    },
    web,
    lvgl: editor.i18nEffective?.lvgl || {},
  };

  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `betta-i18n-${lang}.json`;
  a.click();
  URL.revokeObjectURL(url);
}

async function uploadLanguageJson() {
  const lang = normalizeLanguageCode(el.uploadLanguageCode?.value, "");
  if (!lang) {
    throw new Error(t("settings.translation.invalid_code"));
  }
  const file = el.uploadLanguageFile?.files?.[0];
  if (!file) {
    throw new Error(t("settings.translation.no_file"));
  }

  const text = await file.text();
  let parsed = null;
  try {
    parsed = JSON.parse(text);
  } catch (_) {
    throw new Error(t("settings.translation.invalid_json"));
  }
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
    throw new Error(t("settings.translation.object_required"));
  }

  const response = await fetch(`/api/i18n/custom?lang=${encodeURIComponent(lang)}`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(parsed),
  });
  if (!response.ok) {
    let detail = await response.text();
    try {
      const json = JSON.parse(detail);
      detail = json.error || detail;
    } catch (_) {}
    throw new Error(detail);
  }
}

function setProvisioningVisible(visible) {
  if (el.provisioningRoot) {
    el.provisioningRoot.classList.toggle("hidden", !visible);
  }
  if (el.editorShell) {
    el.editorShell.classList.toggle("hidden", visible);
  }
}

function provisioningStageForSettings(settings) {
  const wifiConfigured = Boolean(settings?.wifi?.configured);
  const wifiSetupApActive = Boolean(settings?.wifi?.setup_ap_active);
  const haConfigured = Boolean(settings?.ha?.configured);
  if (wifiSetupApActive) return "wifi";
  if (!wifiConfigured) return "wifi";
  if (!haConfigured) return "ha";
  return null;
}

function showProvisioningStage(stage, settings) {
  if (!stage) {
    editor.provisioningStage = null;
    setProvisioningVisible(false);
    return false;
  }

  editor.provisioningStage = stage;
  const wifi = settings?.wifi || {};
  const ha = settings?.ha || {};
  editor.wifiScanSupported = wifi.scan_supported !== false;

  if (el.provisioningWifiPage) {
    el.provisioningWifiPage.classList.toggle("hidden", stage !== "wifi");
  }
  if (el.provisioningHaPage) {
    el.provisioningHaPage.classList.toggle("hidden", stage !== "ha");
  }
  setProvisioningVisible(true);

  if (el.provWifiSsid) {
    el.provWifiSsid.value = wifi.ssid || "";
  }
  if (el.provWifiCountryCode) {
    el.provWifiCountryCode.value = normalizeCountryCode(wifi.country_code) || "US";
  }
  if (el.provWifiPassword) {
    el.provWifiPassword.value = "";
    el.provWifiPassword.type = "password";
  }
  if (el.provWifiShowPassword) {
    el.provWifiShowPassword.checked = false;
  }
  if (el.provHaUrl) {
    el.provHaUrl.value = ha.ws_url || "";
  }
  if (el.provHaToken) {
    el.provHaToken.value = "";
    el.provHaToken.type = "password";
  }
  if (el.provHaShowToken) {
    el.provHaShowToken.checked = false;
  }

  if (el.provScanWifiBtn) {
    el.provScanWifiBtn.disabled = !editor.wifiScanSupported || editor.wifiScanInProgress;
  }
  renderWifiScanResults(editor.wifiScanItems, "provisioning");

  if (!editor.wifiScanSupported) {
    setWifiScanInfo(t("wifi.scan_unavailable"), false, "provisioning");
  } else if (!editor.wifiScanHasRun && !editor.wifiScanInProgress) {
    setWifiScanInfo(t("wifi.scan_click_short"), false, "provisioning");
  }

  setProvisioningInfo(
    stage,
    stage === "wifi"
      ? t("provision.wifi.hint", {}, "Save reboots the panel. After reboot, HA provisioning is shown.")
      : t("provision.ha.hint", {}, "Save reboots the panel. After reboot, the editor is unlocked.")
  );
  return true;
}

function setupSettingsWorkspace() {
  if (el.settingsContentPane || !el.settingsPane) return;

  const workspaceBody = document.querySelector(".workspace-body");
  el.canvasWrap = el.canvasWrap || document.querySelector(".canvas-wrap");
  el.actionsPanel = el.actionsPanel || document.querySelector("aside.actions-panel");
  if (!workspaceBody || !el.canvasWrap) return;

  const contentPane = document.createElement("div");
  contentPane.id = "settingsContentPane";
  contentPane.className = "settings-content hidden";
  workspaceBody.insertBefore(contentPane, el.canvasWrap);

  const navSection = document.createElement("section");
  navSection.className = "settings-nav-section";

  const navHeading = document.createElement("h2");
  navHeading.id = "settingsNavHeading";
  navHeading.textContent = t("tabs.settings");
  navSection.appendChild(navHeading);

  const nav = document.createElement("div");
  nav.id = "settingsNav";
  nav.className = "settings-nav";
  const actionsHeading = document.getElementById("settingsActionsHeading");
  const actionsSection = actionsHeading?.closest("section") || null;

  for (const item of SETTINGS_NAV_ITEMS) {
    const heading = document.getElementById(item.headingId);
    const section = heading?.closest("section");
    if (!section) continue;

    section.id = item.sectionId;
    section.classList.add("settings-content-section", "hidden");
    contentPane.appendChild(section);

    const button = document.createElement("button");
    button.type = "button";
    button.className = "settings-nav-btn";
    button.dataset.settingsSection = item.sectionId;
    button.dataset.i18nKey = item.labelKey;
    button.textContent = t(item.labelKey);
    nav.appendChild(button);
  }

  navSection.appendChild(nav);
  if (actionsSection) {
    actionsSection.id = "settingsSidebarActions";
    actionsSection.classList.add("settings-sidebar-actions");
    actionsHeading.classList.add("hidden");
    el.settingsPane.replaceChildren(navSection, actionsSection);
  } else {
    el.settingsPane.replaceChildren(navSection);
  }
  el.settingsContentPane = contentPane;
  el.settingsNavButtons = Array.from(nav.querySelectorAll(".settings-nav-btn"));
  el.settingsContentSections = Array.from(contentPane.querySelectorAll(".settings-content-section"));
  setActiveSettingsSection(editor.activeSettingsSection);
}

function applySettingsNavTranslations() {
  setTextById("settingsNavHeading", "tabs.settings");
  for (const button of el.settingsNavButtons || []) {
    const key = button.dataset.i18nKey;
    if (key) {
      button.textContent = t(key);
    }
  }
  if (editor.activePane === "settings") {
    setActiveSettingsSection(editor.activeSettingsSection);
  }
}

function activeSettingsNavItem(sectionId = editor.activeSettingsSection) {
  return SETTINGS_NAV_ITEMS.find((item) => item.sectionId === sectionId) || SETTINGS_NAV_ITEMS[0];
}

function setActiveSettingsSection(sectionId) {
  setupSettingsWorkspace();
  const item = activeSettingsNavItem(sectionId);
  if (!item) return;

  editor.activeSettingsSection = item.sectionId;
  for (const section of el.settingsContentSections || []) {
    section.classList.toggle("hidden", section.id !== item.sectionId);
  }
  for (const button of el.settingsNavButtons || []) {
    const active = button.dataset.settingsSection === item.sectionId;
    button.classList.toggle("active", active);
    button.setAttribute("aria-current", active ? "page" : "false");
  }
  if (editor.activePane === "settings" && el.canvasTitle) {
    el.canvasTitle.textContent = t(item.labelKey);
  }
}

function setActivePane(pane) {
  setupSettingsWorkspace();
  editor.activePane = pane === "settings" ? "settings" : "layout";
  const showLayout = editor.activePane === "layout";
  el.layoutPane.classList.toggle("hidden", !showLayout);
  el.settingsPane.classList.toggle("hidden", showLayout);
  if (el.settingsContentPane) {
    el.settingsContentPane.classList.toggle("hidden", showLayout);
  }
  if (el.canvasWrap) {
    el.canvasWrap.classList.toggle("hidden", !showLayout);
  }
  if (el.actionsPanel) {
    el.actionsPanel.classList.toggle("hidden", !showLayout);
  }
  el.layoutTabBtn.classList.toggle("active", showLayout);
  el.settingsTabBtn.classList.toggle("active", !showLayout);
  if (showLayout) {
    clearOtaStatusPoll();
    renderCanvas();
  } else if (editor.ota.status?.running || editor.ota.status?.rebooting) {
    setActiveSettingsSection(editor.activeSettingsSection);
    scheduleOtaStatusPoll();
  } else {
    setActiveSettingsSection(editor.activeSettingsSection);
  }
}

function setSectionCollapsed(sectionKey, collapsed) {
  const map = {
    pages: { section: el.pagesSection, toggle: el.togglePagesSection },
    widgets: { section: el.widgetsSection, toggle: el.toggleWidgetsSection },
    inspector: { section: el.inspectorSection, toggle: el.toggleInspectorSection },
  };
  const entry = map[sectionKey];
  if (!entry || !entry.section || !entry.toggle) return;

  const nextCollapsed = Boolean(collapsed);
  editor.sectionCollapsed[sectionKey] = nextCollapsed;
  entry.section.classList.toggle("collapsed", nextCollapsed);
  entry.toggle.textContent = nextCollapsed ? "+" : "-";
  entry.toggle.setAttribute("aria-expanded", nextCollapsed ? "false" : "true");
}

function toggleSection(sectionKey) {
  setSectionCollapsed(sectionKey, !editor.sectionCollapsed[sectionKey]);
}

function setPercentSlider(input, output, value) {
  const percent = clamp(Number(value), 10, 100);
  if (input) {
    input.value = String(percent);
  }
  if (output) {
    output.textContent = `${percent}%`;
  }
  return percent;
}

function applySectionCollapseState() {
  setSectionCollapsed("pages", editor.sectionCollapsed.pages);
  setSectionCollapsed("widgets", editor.sectionCollapsed.widgets);
  setSectionCollapsed("inspector", editor.sectionCollapsed.inspector);
}

function renderSettings() {
  const settings = editor.settings || {};
  const wifi = settings.wifi || {};
  const ha = settings.ha || {};
  const weather = settings.weather || {};
  const stocks = settings.stocks || {};
  const hardware = settings.hardware || {};
  const time = settings.time || {};
  const ui = settings.ui || {};
  const scanSupported = wifi.scan_supported !== false;
  editor.wifiScanSupported = scanSupported;

  el.settingsWifiSsid.value = wifi.ssid || "";
  if (el.settingsWifiCountryCode) {
    el.settingsWifiCountryCode.value = normalizeCountryCode(wifi.country_code) || "US";
  }
  if (el.settingsWifiBssid) {
    el.settingsWifiBssid.value = normalizeBssid(wifi.bssid || "");
  }
  el.settingsWifiPassword.value = "";
  el.settingsHaUrl.value = ha.ws_url || "";
  el.settingsHaToken.value = "";
  if (el.settingsHaRestEnabled) {
    el.settingsHaRestEnabled.checked = ha.rest_enabled === true;
  }
  if (el.settingsTopbarWeatherEnabled) {
    el.settingsTopbarWeatherEnabled.checked = weather.topbar_enabled === true;
  }
  if (el.settingsTopbarWeatherEntity) {
    el.settingsTopbarWeatherEntity.value = weather.entity_id || "";
  }
  const stockEntityIds = Array.isArray(stocks.entity_ids) ? stocks.entity_ids : [];
  if (el.settingsStockEntity1) el.settingsStockEntity1.value = stockEntityIds[0] || "";
  if (el.settingsStockEntity2) el.settingsStockEntity2.value = stockEntityIds[1] || "";
  if (el.settingsStockEntity3) el.settingsStockEntity3.value = stockEntityIds[2] || "";
  setPercentSlider(
    el.settingsHardwareActiveBrightness,
    el.settingsHardwareActiveBrightnessValue,
    hardware.active_brightness ?? 100,
  );
  setPercentSlider(
    el.settingsHardwareDimBrightness,
    el.settingsHardwareDimBrightnessValue,
    hardware.dim_brightness ?? 10,
  );
  if (el.settingsHardwareDimTimeout) {
    el.settingsHardwareDimTimeout.value = String(clamp(Number(hardware.dim_timeout_seconds ?? 180), 5, 3600));
  }
  el.settingsNtpServer.value = time.ntp_server || "";
  el.settingsTimezone.value = time.timezone || "";
  if (el.settingsLanguage) {
    el.settingsLanguage.value = normalizeUiLanguage(ui.language);
  }
  renderLanguageOptions();

  const connectedRssiText = Number.isFinite(Number(wifi.rssi_dbm))
    ? `${Math.round(Number(wifi.rssi_dbm))} dBm`
    : "n/a";
  const connectedBssid = normalizeBssid(wifi.connected_bssid || "");
  const connectedChannel = Number.isFinite(Number(wifi.connected_channel))
    ? String(Math.round(Number(wifi.connected_channel)))
    : "n/a";

  el.settingsWifiInfo.textContent = [
    `${t("settings.info.configured")}: ${wifi.configured ? t("common.yes") : t("common.no")}`,
    `${t("settings.info.connected")}: ${wifi.connected ? t("common.yes") : t("common.no")}`,
    `${t("settings.info.password_stored")}: ${wifi.password_set ? t("common.yes") : t("common.no")}`,
    `${t("settings.info.country")}: ${normalizeCountryCode(wifi.country_code) || "US"}`,
    `${t("settings.info.rssi")}: ${connectedRssiText}`,
    `${t("settings.info.connected_bssid")}: ${connectedBssid || "n/a"}`,
    `${t("settings.info.channel")}: ${connectedChannel}`,
  ].join(" | ");

  el.settingsHaInfo.textContent = [
    `${t("settings.info.configured")}: ${ha.configured ? t("common.yes") : t("common.no")}`,
    `${t("settings.info.connected")}: ${ha.connected ? t("common.yes") : t("common.no")}`,
    `${t("settings.info.token_stored")}: ${ha.access_token_set ? t("common.yes") : t("common.no")}`,
    `${t("settings.info.rest_fallback")}: ${ha.rest_enabled ? t("common.yes") : t("common.no")}`,
  ].join(" | ");

  if (el.settingsWeatherInfo) {
    el.settingsWeatherInfo.textContent = t("settings.weather.info");
  }
  if (el.settingsStocksInfo) {
    el.settingsStocksInfo.textContent = t("settings.stocks.info");
  }
  if (el.settingsHardwareInfo) {
    el.settingsHardwareInfo.textContent = t("settings.hardware.info");
  }
  el.settingsTimeInfo.textContent = t("settings.time.info");
  if (el.settingsUiInfo) {
    el.settingsUiInfo.textContent = t("settings.ui.info");
  }
  if (el.settingsTranslationInfo && !el.settingsTranslationInfo.classList.contains("error")) {
    el.settingsTranslationInfo.textContent = t("settings.translation.info");
  }

  if (wifi.setup_ap_active) {
    const ssid = wifi.setup_ap_ssid || "(unknown)";
    el.settingsApInfo.textContent = t("settings.ap.active", { ssid });
  } else {
    el.settingsApInfo.textContent = t("settings.ap.inactive");
  }

  if (!scanSupported) {
    setWifiScanInfo(t("wifi.scan_unavailable"));
  } else if (!editor.wifiScanHasRun && !editor.wifiScanInProgress) {
    setWifiScanInfo(t("wifi.scan_click"));
  }
  if (el.scanWifiBtn) {
    el.scanWifiBtn.disabled = !scanSupported || editor.wifiScanInProgress;
  }
  renderOtaStatus(editor.ota.status);
  if (editor.ota.latestUrl) {
    applyLatestOtaUrl(editor.ota.latestUrl);
  } else {
    void refreshLatestOtaUrl();
  }
  renderWifiScanResults(editor.wifiScanItems);
}

function renderWifiScanResults(items, scope = "settings") {
  const ui = getWifiScanUi(scope);
  const select = ui.resultsSelect;
  if (!select) return;

  const currentSsid = ui.ssidInput ? ui.ssidInput.value.trim() : "";
  select.innerHTML = "";
  if (!editor.wifiScanSupported) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = t("wifi.scan.option_unavailable", {}, "Scan unavailable");
    select.appendChild(option);
    return;
  }

  if (editor.wifiScanInProgress) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = t("wifi.scan.option_scanning", {}, "Scanning...");
    select.appendChild(option);
    return;
  }

  if (!editor.wifiScanHasRun) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = t("wifi.scan.option_not_run", {}, "No scan yet");
    select.appendChild(option);
    return;
  }

  if (!Array.isArray(items) || items.length === 0) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = t("wifi.scan.option_no_networks", {}, "No networks found");
    select.appendChild(option);
    return;
  }

  const placeholder = document.createElement("option");
  placeholder.value = "";
  placeholder.textContent = t("wifi.scan.option_select", { count: items.length }, `Select network (${items.length} found)`);
  select.appendChild(placeholder);

  let firstMatchingSsidValue = "";
  for (let idx = 0; idx < items.length; idx++) {
    const net = items[idx];
    if (!net || typeof net.ssid !== "string" || !net.ssid.length) continue;

    const bssid = normalizeBssid(net.bssid || "");
    const rssiText = Number.isFinite(Number(net.rssi)) ? `${Math.round(Number(net.rssi))} dBm` : "n/a";
    const authmodeText = (typeof net.authmode === "string" && net.authmode.length) ? net.authmode : "unknown";
    const channelText = Number.isFinite(Number(net.channel)) ? `ch ${Math.round(Number(net.channel))}` : "ch ?";
    const details = [rssiText, authmodeText, channelText];
    if (bssid) {
      details.push(bssid);
    }
    if (net.connected === true) {
      details.push(t("wifi.scan.connected_tag", {}, "connected"));
    }

    const option = document.createElement("option");
    option.value = bssid || `${net.ssid}#${idx}`;
    option.dataset.ssid = net.ssid;
    option.dataset.bssid = bssid;
    option.textContent = `${net.ssid} (${details.join(", ")})`;
    select.appendChild(option);

    if (!firstMatchingSsidValue && currentSsid && net.ssid === currentSsid) {
      firstMatchingSsidValue = option.value;
    }
  }

  if (firstMatchingSsidValue) {
    select.value = firstMatchingSsidValue;
  }
}

async function scanWifiNetworks(scope = "settings") {
  const ui = getWifiScanUi(scope);
  if (!ui.scanButton) return;
  if (!editor.wifiScanSupported) {
    setWifiScanInfo(
      t("wifi.scan_unavailable"),
      false,
      scope
    );
    return;
  }
  if (editor.wifiScanInProgress) return;

  editor.wifiScanInProgress = true;
  ui.scanButton.disabled = true;
  renderWifiScanResults([], scope);
  setWifiScanInfo(t("status.wifi_scan_running"), false, scope);
  setStatus(t("status.wifi_scan_running"));

  const controller = new AbortController();
  const timeoutId = window.setTimeout(() => controller.abort(), 15000);
  try {
    const response = await fetch("/api/wifi/scan", {
      cache: "no-store",
      signal: controller.signal,
    });
    const body = await response.text();
    let data = null;
    if (body) {
      try {
        data = JSON.parse(body);
      } catch (_) {
        data = null;
      }
    }

    if (!response.ok) {
      const detail = data?.message || data?.error || `${response.status} ${response.statusText}`;
      throw new Error(detail);
    }

    data = data || {};
    editor.wifiScanItems = Array.isArray(data.items) ? data.items : [];
    editor.wifiScanHasRun = true;
    renderWifiScanResults(editor.wifiScanItems, scope);

    if (editor.wifiScanItems.length > 0) {
      setWifiScanInfo(
        t("wifi.scan_found", { count: editor.wifiScanItems.length }),
        false,
        scope
      );
    } else {
      setWifiScanInfo(t("wifi.scan_no_networks"), false, scope);
    }
    setStatus(t("status.wifi_scan_complete", { count: editor.wifiScanItems.length }));
  } catch (err) {
    editor.wifiScanHasRun = true;
    renderWifiScanResults(editor.wifiScanItems, scope);
    const detail = err?.name === "AbortError"
      ? t("status.wifi_scan_timeout")
      : (err?.message || t("common.unknown_error"));
    setWifiScanInfo(detail, true, scope);
    setStatus(t("status.wifi_scan_failed", { error: detail }), true);
  } finally {
    window.clearTimeout(timeoutId);
    editor.wifiScanInProgress = false;
    renderWifiScanResults(editor.wifiScanItems, scope);
    ui.scanButton.disabled = false;
  }
}

async function loadSettings(silent = false) {
  if (!silent) {
    setStatus(t("status.loading_settings"));
  }
  try {
    editor.settings = await apiGet("/api/settings");
    await loadI18nLanguage(editor.settings?.ui?.language || DEFAULT_UI_LANGUAGE, true);
    renderSettings();
    if (!silent) {
      setStatus(t("status.settings_loaded"));
    }
    return editor.settings;
  } catch (err) {
    if (!silent) {
      setStatus(t("status.settings_load_failed", { error: err.message }), true);
    }
    return null;
  }
}

function formatBytes(bytes) {
  const n = Number(bytes);
  if (!Number.isFinite(n) || n < 0) return "n/a";
  if (n === 0) return "0 B";
  const units = ["B", "KB", "MB"];
  let value = n;
  let unitIndex = 0;
  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }
  const decimals = unitIndex === 0 || value >= 100 ? 0 : 1;
  return `${value.toFixed(decimals)} ${units[unitIndex]}`;
}

function parseJsonMaybe(text) {
  if (!text) return null;
  try {
    return JSON.parse(text);
  } catch (_) {
    return null;
  }
}

function otaProgressVars(status) {
  const written = Math.max(0, Number(status?.written) || 0);
  const total = Math.max(0, Number(status?.total) || 0);
  const rawProgress = Number(status?.progress);
  const progress = total > 0
    ? Math.min(100, Math.max(0, Number.isFinite(rawProgress) ? rawProgress : (written * 100) / total))
    : 0;
  return {
    progress: total > 0 ? String(Math.round(progress)) : "--",
    written: formatBytes(written),
    total: total > 0 ? formatBytes(total) : "n/a",
  };
}

function clearOtaStatusPoll() {
  if (editor.ota.pollTimerId) {
    window.clearTimeout(editor.ota.pollTimerId);
    editor.ota.pollTimerId = null;
  }
}

function scheduleOtaStatusPoll() {
  clearOtaStatusPoll();
  if (editor.activePane !== "settings") return;
  editor.ota.pollTimerId = window.setTimeout(() => {
    void loadOtaStatus(true);
  }, OTA_STATUS_POLL_MS);
}

function setOtaInfo(text, isError = false) {
  if (!el.settingsOtaInfo) return;
  el.settingsOtaInfo.textContent = text;
  el.settingsOtaInfo.classList.toggle("error", isError);
}

function setOtaProgress(percent) {
  if (!el.settingsOtaProgressBar) return;
  const n = Number(percent);
  const clamped = Number.isFinite(n) ? Math.min(100, Math.max(0, n)) : 0;
  el.settingsOtaProgressBar.style.width = `${clamped}%`;
}

function setOtaControlsDisabled(disabled) {
  const busy = Boolean(disabled);
  if (el.startOtaUrlBtn) {
    el.startOtaUrlBtn.disabled = busy;
  }
  if (el.uploadOtaBtn) {
    el.uploadOtaBtn.disabled = busy;
  }
  if (el.settingsOtaUrl) {
    el.settingsOtaUrl.disabled = busy;
  }
  if (el.settingsOtaFile) {
    el.settingsOtaFile.disabled = busy;
  }
}

function renderOtaStatus(status) {
  if (!el.settingsOtaInfo) return;

  const hasStatus = status && typeof status === "object";
  if (!hasStatus) {
    setOtaProgress(0);
    setOtaControlsDisabled(editor.ota.uploadInProgress);
    return;
  }

  const vars = otaProgressVars(status);
  const state = typeof status.state === "string" ? status.state : "idle";
  const running = Boolean(status.running);
  const rebooting = Boolean(status.rebooting);
  const progress = Number(vars.progress);
  setOtaProgress(Number.isFinite(progress) ? progress : 0);

  let text = "";
  let isError = false;
  if (state === "error") {
    text = t("settings.ota.error", { error: status.error || t("common.unknown_error") });
    isError = true;
  } else if (rebooting) {
    text = t("settings.ota.rebooting");
  } else if (state === "success") {
    text = t("settings.ota.success");
  } else if (running && state === "url") {
    text = t("settings.ota.downloading", vars);
  } else if (running && state === "upload") {
    text = t("settings.ota.uploading", vars);
  } else if (running) {
    text = t("settings.ota.running", vars);
  } else {
    text = t("settings.ota.idle", {
      running: status.running_partition || "n/a",
      next: status.next_partition || "n/a",
      size: status.slot_size ? formatBytes(status.slot_size) : "n/a",
    });
  }

  const imageInfo = [
    status.project_name || "",
    status.version || "",
  ].filter(Boolean).join(" ");
  if (imageInfo) {
    text += `\n${imageInfo}`;
  }
  const targetPartition = status.partition || (running ? status.next_partition : "");
  if (targetPartition) {
    text += `\n${t("settings.ota.target_slot", { partition: targetPartition })}`;
  }

  setOtaInfo(text, isError);
  setOtaControlsDisabled(editor.ota.uploadInProgress || running || rebooting);
}

async function loadOtaStatus(silent = false) {
  if (!silent && el.settingsOtaInfo) {
    setOtaInfo(t("settings.ota.refresh"));
  }
  try {
    const status = await apiGet("/api/ota/status");
    editor.ota.status = status;
    renderOtaStatus(status);
    if (status?.running || status?.rebooting) {
      scheduleOtaStatusPoll();
    } else {
      clearOtaStatusPoll();
    }
    return status;
  } catch (err) {
    const wasRebooting = Boolean(editor.ota.status?.rebooting);
    setOtaInfo(
      wasRebooting ? t("settings.ota.rebooting") : t("settings.ota.request_failed", { error: err.message }),
      !wasRebooting
    );
    if (!wasRebooting) {
      clearOtaStatusPoll();
    }
    return null;
  }
}

async function startOtaFromUrl() {
  const url = el.settingsOtaUrl?.value.trim() || "";
  if (!url) {
    setOtaInfo(t("settings.ota.no_url"), true);
    return;
  }

  clearOtaStatusPoll();
  setOtaProgress(0);
  setOtaControlsDisabled(true);
  setOtaInfo(t("settings.ota.starting_url"));

  try {
    const response = await fetch("/api/ota/url", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ url }),
    });
    const body = await response.text();
    const payload = parseJsonMaybe(body) || {};
    if (!response.ok || payload.ok === false) {
      throw new Error(payload.error || `${response.status} ${response.statusText}`);
    }
    editor.ota.status = payload;
    renderOtaStatus(payload);
    if (payload.running || payload.rebooting) {
      scheduleOtaStatusPoll();
    }
  } catch (err) {
    setOtaInfo(t("settings.ota.request_failed", { error: err.message }), true);
    setOtaControlsDisabled(false);
  }
}

async function uploadOtaFile() {
  const file = el.settingsOtaFile?.files?.[0];
  if (!file) {
    setOtaInfo(t("settings.ota.no_file"), true);
    return;
  }

  clearOtaStatusPoll();
  editor.ota.uploadInProgress = true;
  setOtaProgress(0);
  setOtaControlsDisabled(true);
  setOtaInfo(t("settings.ota.upload_progress", {
    progress: "0",
    written: "0 B",
    total: formatBytes(file.size),
  }));

  try {
    const payload = await new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open("POST", "/api/ota/upload");
      xhr.setRequestHeader("Content-Type", "application/octet-stream");
      xhr.upload.onprogress = (event) => {
        if (!event.lengthComputable) return;
        const progress = Math.min(100, Math.max(0, (event.loaded * 100) / event.total));
        setOtaProgress(progress);
        setOtaInfo(t("settings.ota.upload_progress", {
          progress: String(Math.round(progress)),
          written: formatBytes(event.loaded),
          total: formatBytes(event.total),
        }));
      };
      xhr.onload = () => {
        const data = parseJsonMaybe(xhr.responseText) || {};
        if (xhr.status < 200 || xhr.status >= 300 || data.ok === false) {
          reject(new Error(data.error || `${xhr.status} ${xhr.statusText}`));
          return;
        }
        resolve(data);
      };
      xhr.onerror = () => reject(new Error(t("common.unknown_error")));
      xhr.onabort = () => reject(new Error("aborted"));
      xhr.send(file);
    });

    editor.ota.status = payload;
    renderOtaStatus(payload);
    if (el.settingsOtaFile) {
      el.settingsOtaFile.value = "";
    }
    if (payload?.running || payload?.rebooting) {
      scheduleOtaStatusPoll();
    }
  } catch (err) {
    setOtaInfo(t("settings.ota.request_failed", { error: err.message }), true);
    setOtaProgress(0);
  } finally {
    editor.ota.uploadInProgress = false;
    setOtaControlsDisabled(Boolean(editor.ota.status?.running || editor.ota.status?.rebooting));
  }
}

async function putSettings(payload) {
  const response = await fetch("/api/settings", {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (!response.ok) {
    let detail = await response.text();
    try {
      const json = JSON.parse(detail);
      detail = json.error || detail;
    } catch (_) {}
    throw new Error(detail);
  }
}

async function saveWifiProvisioning() {
  const ssid = el.provWifiSsid?.value.trim() || "";
  const password = el.provWifiPassword?.value || "";
  const countryCode = normalizeCountryCode(el.provWifiCountryCode?.value) || "";

  if (!ssid) {
    setProvisioningInfo("wifi", t("provision.wifi.required_ssid"), true);
    return;
  }
  if (!countryCode) {
    setProvisioningInfo("wifi", t("provision.wifi.required_country"), true);
    return;
  }

  const payload = {
    wifi: {
      ssid,
      country_code: countryCode,
      bssid: null,
    },
    reboot: true,
  };
  if (password.length > 0) {
    payload.wifi.password = password;
  }

  setProvisioningInfo("wifi", t("provision.saving_reboot"));
  await putSettings(payload);
  setProvisioningInfo("wifi", t("provision.saved_reboot"));
}

async function saveHaProvisioning() {
  const wsUrl = el.provHaUrl?.value.trim() || "";
  const accessToken = el.provHaToken?.value.trim() || "";

  if (!wsUrl) {
    setProvisioningInfo("ha", t("provision.ha.required_url"), true);
    return;
  }
  if (!wsUrl.startsWith("ws://") && !wsUrl.startsWith("wss://")) {
    setProvisioningInfo("ha", t("provision.ha.invalid_url"), true);
    return;
  }
  if (!accessToken) {
    setProvisioningInfo("ha", t("provision.ha.required_token"), true);
    return;
  }

  const payload = {
    ha: {
      ws_url: wsUrl,
      access_token: accessToken,
    },
    reboot: true,
  };

  setProvisioningInfo("ha", t("provision.saving_reboot"));
  markSetupWizardPending();
  try {
    await putSettings(payload);
  } catch (err) {
    storageRemove(SETUP_WIZARD_PENDING_STORAGE_KEY);
    throw err;
  }
  setProvisioningInfo("ha", t("provision.saved_reboot"));
}

async function saveSettings() {
  const wifiSsid = el.settingsWifiSsid.value.trim();
  const wifiPassword = el.settingsWifiPassword.value;
  const wifiCountryCode = normalizeCountryCode(el.settingsWifiCountryCode?.value) || "";
  const wifiBssidRaw = el.settingsWifiBssid?.value || "";
  const wifiBssid = normalizeBssid(wifiBssidRaw);
  const haUrl = el.settingsHaUrl.value.trim();
  const haToken = el.settingsHaToken.value.trim();
  const haRestEnabled = Boolean(el.settingsHaRestEnabled?.checked);
  const weatherTopbarEnabled = Boolean(el.settingsTopbarWeatherEnabled?.checked);
  const weatherEntity = el.settingsTopbarWeatherEntity?.value.trim() || "";
  const stockEntityIds = [
    el.settingsStockEntity1?.value.trim() || "",
    el.settingsStockEntity2?.value.trim() || "",
    el.settingsStockEntity3?.value.trim() || "",
  ];
  const hardwareActiveBrightness = clamp(Number(el.settingsHardwareActiveBrightness?.value || 100), 10, 100);
  const hardwareDimBrightness = clamp(Number(el.settingsHardwareDimBrightness?.value || 10), 10, 100);
  const hardwareDimTimeout = clamp(Number(el.settingsHardwareDimTimeout?.value || 180), 5, 3600);
  const ntpServer = el.settingsNtpServer.value.trim();
  const timezone = el.settingsTimezone.value.trim();
  const language = normalizeUiLanguage(el.settingsLanguage?.value);

  if (!wifiCountryCode) {
    setStatus(t("settings.language.invalid_country"), true);
    return;
  }
  if (wifiBssidRaw.trim().length > 0 && !wifiBssid) {
    setStatus(t("settings.language.invalid_bssid"), true);
    return;
  }
  if (haUrl && !haUrl.startsWith("ws://") && !haUrl.startsWith("wss://")) {
    setStatus(t("settings.language.invalid_ha_url"), true);
    return;
  }

  const payload = {
    wifi: {
      ssid: wifiSsid,
      country_code: wifiCountryCode,
      bssid: wifiBssid || null,
    },
    ha: {
      ws_url: haUrl,
      rest_enabled: haRestEnabled,
    },
    weather: {
      topbar_enabled: weatherTopbarEnabled,
      entity_id: weatherEntity || null,
    },
    stocks: {
      entity_ids: stockEntityIds,
    },
    hardware: {
      active_brightness: hardwareActiveBrightness,
      dim_brightness: hardwareDimBrightness,
      dim_timeout_seconds: hardwareDimTimeout,
    },
    time: {
      ntp_server: ntpServer,
      timezone,
    },
    ui: {
      language,
    },
    reboot: true,
  };
  if (wifiPassword.length > 0) {
    payload.wifi.password = wifiPassword;
  }
  if (haToken.length > 0) {
    payload.ha.access_token = haToken;
  }

  setStatus(t("status.saving_settings"));
  await putSettings(payload);
  setStatus(t("status.settings_saved_reboot"));
}

function defaultLayout() {
  return {
    version: 1,
    pages: [
      {
        id: "living",
        title: t("layout.default_page.title"),
        widgets: [],
      },
    ],
  };
}

function defaultEnergyConfig() {
  return ENERGY_ENTITY_KEYS.reduce((config, key) => {
    config[key] = "";
    return config;
  }, { source: ENERGY_SOURCE_HA });
}

function isEnergyPage(page) {
  return page?.type === ENERGY_PAGE_TYPE;
}

function energyPageUsesHaSource(page) {
  if (!isEnergyPage(page)) return false;
  const source = typeof page.energy?.source === "string" ? page.energy.source.trim() : "";
  return source !== ENERGY_SOURCE_MANUAL;
}

function layoutHasHaEnergyPage() {
  return (editor.layout?.pages || []).some((page) => energyPageUsesHaSource(page));
}

function normalizeEnergyConfig(page) {
  if (!page || !isEnergyPage(page)) return;
  if (!page.energy || typeof page.energy !== "object" || Array.isArray(page.energy)) {
    page.energy = defaultEnergyConfig();
  }
  const source = typeof page.energy.source === "string" ? page.energy.source.trim() : "";
  const hasManualSensors = ENERGY_ENTITY_KEYS.some((key) => typeof page.energy[key] === "string" && page.energy[key].trim());
  page.energy.source = ENERGY_SOURCES.has(source) ? source : (hasManualSensors ? ENERGY_SOURCE_MANUAL : ENERGY_SOURCE_HA);
  for (const key of ENERGY_ENTITY_KEYS) {
    page.energy[key] = typeof page.energy[key] === "string" ? page.energy[key].trim() : "";
  }
  page.widgets = [];
}

function getEnergyInputs() {
  return {
    home_power_entity_id: el.energyHomePower,
    solar_power_entity_id: el.energySolarPower,
    grid_power_entity_id: el.energyGridPower,
    grid_import_power_entity_id: el.energyGridImport,
    grid_export_power_entity_id: el.energyGridExport,
    battery_power_entity_id: el.energyBatteryPower,
    battery_charge_power_entity_id: el.energyBatteryCharge,
    battery_discharge_power_entity_id: el.energyBatteryDischarge,
    battery_soc_entity_id: el.energyBatterySoc,
  };
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function snap(value) {
  return Math.round(value / GRID) * GRID;
}

function selectedPage() {
  if (!editor.layout) return null;
  return editor.layout.pages.find((p) => p.id === editor.selectedPageId) || null;
}

function selectedWidget() {
  const page = selectedPage();
  if (!page) return null;
  if (!Array.isArray(page.widgets)) return null;
  return page.widgets.find((w) => w.id === editor.selectedWidgetId) || null;
}

function inspectorWidgetType() {
  return selectedWidget()?.type || el.fType?.value || "sensor";
}

function inspectorSliderEntityDomain() {
  if (el.fSliderEntityDomain) {
    return normalizeSliderEntityDomain(el.fSliderEntityDomain.value);
  }
  return normalizeSliderEntityDomain(selectedWidget()?.slider_entity_domain);
}

function inspectorButtonMode() {
  if (el.fButtonMode) {
    return normalizeButtonMode(el.fButtonMode.value);
  }
  return normalizeButtonMode(selectedWidget()?.button_mode);
}

function allowedEntityDomainsForWidgetType(
  type,
  sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN,
  buttonMode = DEFAULT_BUTTON_MODE,
) {
  if (type === "empty_tile") return [];
  if (type === "sensor" || type === "graph" || type === "solar_forecast") return ["sensor"];
  if (type === "button") {
    const normalizedMode = normalizeButtonMode(buttonMode);
    return buttonModeRequiresMediaPlayer(normalizedMode) ? ["media_player"] : ["switch", "media_player"];
  }
  if (type === "light_tile") return ["light"];
  if (type === "heating_tile") return ["climate"];
  if (type === "weather_tile" || type === "weather_3day") return ["weather"];
  if (type === "todo_list") return ["todo"];
  if (type === "media_player") return ["media_player"];
  if (type === "roborock_tile") return ["vacuum"];
  if (type === "slider") {
    const normalized = normalizeSliderEntityDomain(sliderDomain);
    if (normalized === "auto") {
      return ["light", "media_player", "cover"];
    }
    return [normalized];
  }
  return [];
}

function expectedDomainForWidgetType(
  type,
  sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN,
  buttonMode = DEFAULT_BUTTON_MODE,
) {
  const domains = allowedEntityDomainsForWidgetType(type, sliderDomain, buttonMode);
  return domains.length === 1 ? domains[0] : "";
}

function secondaryEntityConfigForWidgetType(type) {
  if (type === "heating_tile") {
    return {
      enabled: true,
      domain: "sensor",
      optional: false,
      labelKey: "layout.inspector.secondary_entity",
      labelFallback: "Actual entity (sensor)",
      invalidStatusKey: "layout.status.secondary_sensor_required",
    };
  }
  if (type === "roborock_tile") {
    return {
      enabled: true,
      domain: "image",
      optional: true,
      labelKey: "layout.inspector.secondary_entity_roborock",
      labelFallback: "Map entity (image, optional)",
      invalidStatusKey: "layout.status.secondary_image_required",
    };
  }
  return {
    enabled: false,
    domain: "",
    optional: true,
    labelKey: "layout.inspector.secondary_entity",
    labelFallback: "Actual entity (sensor)",
    invalidStatusKey: "layout.status.secondary_sensor_required",
  };
}

function listEntitiesByDomain(domain) {
  if (!domain) return editor.entities;
  return editor.entities.filter((entity) => typeof entity.id === "string" && entity.id.startsWith(`${domain}.`));
}

function entityMatchesWidgetType(
  entity,
  type,
  sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN,
  buttonMode = DEFAULT_BUTTON_MODE,
) {
  if (type === "empty_tile") return true;

  const id = typeof entity?.id === "string" ? entity.id : "";
  if (!id) return false;

  const allowedDomains = allowedEntityDomainsForWidgetType(type, sliderDomain, buttonMode);
  if (!allowedDomains.length) return true;
  const matchesDomain = allowedDomains.some((domain) => id.startsWith(`${domain}.`));
  if (!matchesDomain) return false;
  const modelEntity = entity?.capabilities
    ? entity
    : editor.entities.find((candidate) => candidate?.id === id);
  if (type === "slider" && id.startsWith("light.") && modelEntity?.capabilities?.dimming === false) {
    return false;
  }
  return true;
}

function listEntitiesForWidgetType(
  type,
  sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN,
  buttonMode = DEFAULT_BUTTON_MODE,
) {
  if (type === "empty_tile") return [];
  return editor.entities.filter((entity) => entityMatchesWidgetType(entity, type, sliderDomain, buttonMode));
}

function pickDefaultEntityForWidgetType(
  type,
  sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN,
  buttonMode = DEFAULT_BUTTON_MODE,
) {
  if (type === "empty_tile") return "";
  const matching = listEntitiesForWidgetType(type, sliderDomain, buttonMode);
  if (matching.length > 0) return matching[0].id;
  return "";
}

function uniqueId(prefix, list, accessor = (x) => x.id) {
  let i = 1;
  while (true) {
    const candidate = `${prefix}_${i}`;
    if (!list.some((entry) => accessor(entry) === candidate)) return candidate;
    i += 1;
  }
}

function sanitizeIdPart(value) {
  const normalized = String(value || "")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9_]+/g, "_")
    .replace(/^_+|_+$/g, "");
  return normalized || "item";
}

function createWidgetIdForPage(page, type) {
  const pagePart = sanitizeIdPart(page?.id || page?.title || "page");
  const typePart = sanitizeIdPart(type || "widget");
  const allWidgets = editor.layout?.pages?.flatMap((p) => (Array.isArray(p.widgets) ? p.widgets : [])) || [];

  let i = 1;
  while (true) {
    const candidate = `${pagePart}_${typePart}_${i}`;
    if (!allWidgets.some((widget) => widget?.id === candidate)) {
      return candidate;
    }
    i += 1;
  }
}

async function apiGet(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
  return response.json();
}

function setEntityOptionsList(target, entities) {
  target.innerHTML = "";
  for (const entity of entities) {
    if (!entity || typeof entity.id !== "string" || !entity.id.length) continue;
    const option = document.createElement("option");
    option.value = entity.id;
    option.label = `${entity.id} (${entity.name || entity.id})`;
    target.appendChild(option);
  }
}

function normalizeEntitySearchTerm(value) {
  if (typeof value !== "string") return "";
  const trimmed = value.trim();
  if (!trimmed) return "";
  const wildcardIndex = trimmed.indexOf("*");
  if (wildcardIndex < 0) return trimmed;
  return trimmed.slice(wildcardIndex + 1).trim();
}

function parseEntitySearchInput(rawValue, fallbackDomain = "") {
  const value = typeof rawValue === "string" ? rawValue.trim() : "";
  let domain = typeof fallbackDomain === "string" ? fallbackDomain.trim().toLowerCase() : "";
  let search = value;

  const dotIndex = value.indexOf(".");
  if (dotIndex > 0) {
    const candidateDomain = value.slice(0, dotIndex).trim().toLowerCase();
    if (/^[a-z0-9_]+$/.test(candidateDomain)) {
      domain = candidateDomain;
      search = value.slice(dotIndex + 1);
    }
  }

  search = normalizeEntitySearchTerm(search);
  return { domain, search };
}

function entityContainsSearch(entity, search) {
  if (!search) return true;
  const needle = search.toLowerCase();
  const id = String(entity?.id || "").toLowerCase();
  const name = String(entity?.name || "").toLowerCase();
  return id.includes(needle) || name.includes(needle);
}

function filterLocalEntitySuggestions(source, domain, search, maxItems) {
  const results = [];
  for (const entity of source) {
    if (!entity || typeof entity.id !== "string" || entity.id.length === 0) continue;
    if (domain && !entity.id.startsWith(`${domain}.`)) continue;
    if (!entityContainsSearch(entity, search)) continue;
    results.push(entity);
    if (results.length >= maxItems) break;
  }
  return results;
}

async function fetchEntitySuggestions(domain, search, limit) {
  const params = new URLSearchParams();
  if (domain) params.set("domain", domain);
  if (search) params.set("search", search);
  params.set("limit", String(limit));
  const data = await apiGet(`/api/entities?${params.toString()}`);
  return Array.isArray(data.items) ? data.items : [];
}

function primaryEntitySource() {
  const inspectorType = inspectorWidgetType();
  const sliderDomain = inspectorSliderEntityDomain();
  const buttonMode = inspectorButtonMode();
  const typedOptions = listEntitiesForWidgetType(inspectorType, sliderDomain, buttonMode);
  return typedOptions.length > 0 ? typedOptions : editor.entities;
}

function defaultPrimaryEntityDomain() {
  return expectedDomainForWidgetType(inspectorWidgetType(), inspectorSliderEntityDomain(), inspectorButtonMode());
}

function entityDomainFromValue(value) {
  const text = String(value || "").trim();
  const dotIndex = text.indexOf(".");
  if (dotIndex <= 0) return "";
  const domain = text.slice(0, dotIndex).trim().toLowerCase();
  return /^[a-z0-9_]+$/.test(domain) ? domain : "";
}

function preferredInspectorPrimaryDomain(input) {
  const allowed = allowedEntityDomainsForWidgetType(inspectorWidgetType(), inspectorSliderEntityDomain(), inspectorButtonMode());
  if (!allowed.length) return "";
  const currentDomain = entityDomainFromValue(input?.value);
  if (currentDomain && allowed.includes(currentDomain)) {
    return currentDomain;
  }
  return expectedDomainForWidgetType(inspectorWidgetType(), inspectorSliderEntityDomain(), inspectorButtonMode()) || allowed[0];
}

function inspectorEntityPickerTarget(targetKey) {
  const secondaryConfig = secondaryEntityConfigForWidgetType(inspectorWidgetType());
  const targets = {
    primary: {
      input: el.fEntity,
      domain: preferredInspectorPrimaryDomain(el.fEntity),
      titleKey: "layout.inspector.entity",
      titleFallback: "Choose Entity",
    },
    secondary: {
      input: el.fSecondaryEntity,
      domain: secondaryConfig.domain,
      titleKey: secondaryConfig.labelKey,
      titleFallback: secondaryConfig.labelFallback,
    },
    solar_today: {
      input: el.fSolarForecastToday,
      domain: "sensor",
      titleKey: "layout.inspector.solar_today",
      titleFallback: "Today forecast",
    },
    solar_tomorrow: {
      input: el.fSolarForecastTomorrow,
      domain: "sensor",
      titleKey: "layout.inspector.solar_tomorrow",
      titleFallback: "Tomorrow forecast",
    },
    solar_day_3: {
      input: el.fSolarForecastDay3,
      domain: "sensor",
      titleKey: "layout.inspector.solar_day_3",
      titleFallback: "Day 3 forecast",
    },
    solar_day_4: {
      input: el.fSolarForecastDay4,
      domain: "sensor",
      titleKey: "layout.inspector.solar_day_4",
      titleFallback: "Day 4 forecast",
    },
    solar_day_5: {
      input: el.fSolarForecastDay5,
      domain: "sensor",
      titleKey: "layout.inspector.solar_day_5",
      titleFallback: "Day 5 forecast",
    },
  };
  const target = targets[targetKey];
  if (!target?.input || target.input.disabled || !target.domain) {
    return null;
  }
  return target;
}

function openInspectorEntityPicker(targetKey) {
  const target = inspectorEntityPickerTarget(targetKey);
  if (!target) return;
  openLightEntityPicker(`inspector_${targetKey}`, {
    inspectorTarget: target,
    configOverride: entityPickerConfigForDomain(target.domain, target.titleKey, target.titleFallback),
  });
}

function scheduleEntityAutocomplete(kind, immediate = false) {
  const isSecondary = kind === "secondary";
  const state = isSecondary ? entityAutocomplete.secondary : entityAutocomplete.primary;
  const input = isSecondary ? el.fSecondaryEntity : el.fEntity;
  const options = isSecondary ? el.sensorEntityOptions : el.entityOptions;
  const secondaryConfig = secondaryEntityConfigForWidgetType(inspectorWidgetType());
  if (!input || !options) return;
  if (!isSecondary && input.disabled) return;
  if (isSecondary && input.disabled) return;
  if (isSecondary && !secondaryConfig.enabled) return;

  if (state.timerId !== null) {
    window.clearTimeout(state.timerId);
    state.timerId = null;
  }

  const run = async () => {
    const requestSeq = ++state.requestSeq;
    const raw = input.value || "";
    const fallbackDomain = isSecondary ? secondaryConfig.domain : defaultPrimaryEntityDomain();
    const { domain, search } = parseEntitySearchInput(raw, fallbackDomain);
    const source = isSecondary ? listEntitiesByDomain(secondaryConfig.domain) : primaryEntitySource();
    const allowedDomains = isSecondary
      ? [secondaryConfig.domain]
      : allowedEntityDomainsForWidgetType(inspectorWidgetType(), inspectorSliderEntityDomain(), inspectorButtonMode());

    if (domain && allowedDomains.length > 0 && !allowedDomains.includes(domain)) {
      setEntityOptionsList(options, []);
      return;
    }

    const localResults = filterLocalEntitySuggestions(source, domain, search, ENTITY_AUTOCOMPLETE_MAX_ITEMS);
    setEntityOptionsList(options, localResults);

    const shouldQueryApi = domain.length > 0 || search.length >= 2;
    if (!shouldQueryApi) return;

    try {
      const remoteResults = await fetchEntitySuggestions(domain, search, ENTITY_AUTOCOMPLETE_MAX_ITEMS);
      if (requestSeq !== state.requestSeq) return;
      if (remoteResults.length > 0) {
        setEntityOptionsList(options, remoteResults);
      }
    } catch (_) {
      // Keep local fallback options.
    }
  };

  if (immediate) {
    void run();
    return;
  }

  state.timerId = window.setTimeout(() => {
    state.timerId = null;
    void run();
  }, ENTITY_AUTOCOMPLETE_DEBOUNCE_MS);
}

function scheduleSettingsWeatherAutocomplete(immediate = false) {
  const input = el.settingsTopbarWeatherEntity;
  const options = el.settingsWeatherEntityOptions;
  const state = entityAutocomplete.settingsWeather;
  if (!input || !options || !state) return;

  if (state.timerId !== null) {
    window.clearTimeout(state.timerId);
    state.timerId = null;
  }

  const run = async () => {
    const requestSeq = ++state.requestSeq;
    const { search } = parseEntitySearchInput(input.value || "", "weather");
    const localResults = filterLocalEntitySuggestions(
      listEntitiesByDomain("weather"),
      "weather",
      search,
      ENTITY_AUTOCOMPLETE_MAX_ITEMS,
    );
    setEntityOptionsList(options, localResults);

    if (search.length < 2) return;
    try {
      const remoteResults = await fetchEntitySuggestions("weather", search, ENTITY_AUTOCOMPLETE_MAX_ITEMS);
      if (requestSeq !== state.requestSeq) return;
      if (remoteResults.length > 0) {
        setEntityOptionsList(options, remoteResults);
      }
    } catch (_) {
      // Keep local fallback options.
    }
  };

  if (immediate) {
    void run();
    return;
  }

  state.timerId = window.setTimeout(() => {
    state.timerId = null;
    void run();
  }, ENTITY_AUTOCOMPLETE_DEBOUNCE_MS);
}

function scheduleSettingsStockAutocomplete(input, immediate = false) {
  const options = el.settingsStockEntityOptions;
  const state = entityAutocomplete.settingsStocks;
  if (!input || !options || !state) return;

  if (state.timerId !== null) {
    window.clearTimeout(state.timerId);
    state.timerId = null;
  }

  const run = async () => {
    const requestSeq = ++state.requestSeq;
    const { search } = parseEntitySearchInput(input.value || "", "sensor");
    const localResults = filterLocalEntitySuggestions(
      listEntitiesByDomain("sensor"),
      "sensor",
      search,
      ENTITY_AUTOCOMPLETE_MAX_ITEMS,
    );
    setEntityOptionsList(options, localResults);

    if (search.length < 2) return;
    try {
      const remoteResults = await fetchEntitySuggestions("sensor", search, ENTITY_AUTOCOMPLETE_MAX_ITEMS);
      if (requestSeq !== state.requestSeq) return;
      if (remoteResults.length > 0) {
        setEntityOptionsList(options, remoteResults);
      }
    } catch (_) {
      // Keep local fallback options.
    }
  };

  if (immediate) {
    void run();
    return;
  }

  state.timerId = window.setTimeout(() => {
    state.timerId = null;
    void run();
  }, ENTITY_AUTOCOMPLETE_DEBOUNCE_MS);
}

async function loadLayout() {
  setStatus(t("layout.status.loading"));
  try {
    editor.layout = await apiGet("/api/layout");
    if (!editor.layout || !Array.isArray(editor.layout.pages)) {
      editor.layout = defaultLayout();
    }
  } catch (err) {
    editor.layout = defaultLayout();
    setStatus(t("layout.status.load_failed", { error: err.message }), true);
  }

  if (!editor.layout.pages.length) {
    editor.layout.pages.push(defaultLayout().pages[0]);
  }
  normalizeLayoutWidgets(editor.layout);
  editor.selectedPageId = editor.layout.pages[0].id;
  editor.selectedWidgetId = null;
  renderAll();
  setStatus(t("layout.status.loaded"));
}

async function loadEntities() {
  try {
    const data = await apiGet("/api/entities");
    editor.entities = Array.isArray(data.items) ? data.items : [];
    renderEntityOptions();
  } catch (err) {
    setStatus(t("layout.status.entity_fetch_failed", { error: err.message }), true);
  }
}

async function refreshStates() {
  try {
    const data = await apiGet("/api/state");
    editor.states = new Map();
    if (Array.isArray(data.items)) {
      for (const item of data.items) {
        editor.states.set(item.entity_id, item.state);
      }
    }
    renderCanvas();
  } catch (_) {
    // Keep previous preview values.
  }
}

async function loadEnergyPreview() {
  if (!layoutHasHaEnergyPage()) {
    if (editor.energySnapshot !== null) {
      editor.energySnapshot = null;
      renderCanvas();
    }
    return;
  }
  try {
    editor.energySnapshot = await apiGet("/api/ha/energy");
    renderCanvas();
  } catch (_) {
    if (editor.energySnapshot !== null) {
      editor.energySnapshot = null;
      renderCanvas();
    }
  }
}

function clearLightEntityPickerPoll() {
  if (editor.lightPicker.pollTimerId !== null) {
    window.clearTimeout(editor.lightPicker.pollTimerId);
    editor.lightPicker.pollTimerId = null;
  }
}

function clearLightEntityPickerSearchDebounce() {
  if (editor.lightPicker.searchDebounceId !== null) {
    window.clearTimeout(editor.lightPicker.searchDebounceId);
    editor.lightPicker.searchDebounceId = null;
  }
}

function cancelLightEntityPickerRequest() {
  const config = entityPickerConfig();
  const params = new URLSearchParams();
  params.set("domain", config.domain);
  const search = entityPickerSearchValue();
  if (search) params.set("search", search);
  fetch(`/api/ha/light_entities?${params.toString()}`, { method: "DELETE", cache: "no-store" }).catch(() => {});
}

function entityPickerConfig(widgetType = editor.lightPicker.widgetType) {
  if (editor.lightPicker.configOverride) {
    return {
      widgetType,
      ...editor.lightPicker.configOverride,
    };
  }
  const normalizedWidgetType = ENTITY_PICKER_CONFIGS[widgetType] ? widgetType : "light_tile";
  return {
    widgetType: normalizedWidgetType,
    ...ENTITY_PICKER_CONFIGS[normalizedWidgetType],
  };
}

function entityPickerConfigForDomain(domain, titleKey = "entity_picker.title_entity", titleFallback = "Choose Entity") {
  const keyByDomain = {
    climate: "heating_tile",
    cover: "slider",
    image: "image",
    light: "light_tile",
    media_player: "media_player",
    sensor: "sensor",
    switch: "button",
    todo: "todo_list",
    vacuum: "roborock_tile",
    weather: "weather_tile",
  };
  const key = keyByDomain[domain] || "sensor";
  const base = ENTITY_PICKER_CONFIGS[key];
  if (!base) {
    return {
      domain,
      titleKey,
      blankKey: "entity_picker.blank",
      widgetKey: "entity_picker.title_entity",
      itemsKey: "entity_picker.items_entity",
      titleFallback,
      blankFallback: "Blank Entity",
      widgetFallback: "Entity",
      itemsFallback: `${domain} entities`,
      minSearch: 2,
      liveSearch: false,
    };
  }
  return {
    ...base,
    domain,
    titleKey,
    titleFallback,
  };
}

function entityPickerItemsLabel(config = entityPickerConfig()) {
  return t(config.itemsKey, {}, config.itemsFallback);
}

function entityPickerSearchValue() {
  return String(editor.lightPicker.search || "").trim();
}

function entityPickerSearchReady(config = entityPickerConfig()) {
  const minSearch = Number(config.minSearch || 0);
  return minSearch <= 0 || entityPickerSearchValue().length >= minSearch;
}

function entityPickerLiveSearchEnabled(config = entityPickerConfig()) {
  return config.liveSearch !== false;
}

function entityPickerCacheKey(domain, search = entityPickerSearchValue()) {
  return `${domain}|${String(search || "").trim().toLowerCase()}`;
}

function normalizeEntityPickerItems(items, domain) {
  if (!Array.isArray(items)) return [];
  const prefix = `${domain}.`;
  return items
    .filter((item) => item && typeof item.id === "string" && item.id.startsWith(prefix))
    .map((item) => ({
      id: item.id,
      name: String(item.name || item.id),
      room: String(item.room || ""),
      area_id: String(item.area_id || ""),
      icon: String(item.icon || ""),
    }));
}

function entityPickerRoomLabel(item) {
  return item.room || t("entity_picker.unassigned_room");
}

function mergeEntityPickerItemsIntoEntities(items, domain) {
  if (!Array.isArray(items) || !items.length) return;
  const byId = new Map(editor.entities.map((entity) => [entity.id, entity]));
  for (const item of items) {
    if (!item?.id || byId.has(item.id)) continue;
    const entity = {
      id: item.id,
      name: item.name || item.id,
      domain,
      icon: item.icon || "",
      capabilities: {},
    };
    editor.entities.push(entity);
    byId.set(item.id, entity);
  }
  renderEntityOptions();
}

function groupEntityPickerItemsByRoom(items) {
  const groups = new Map();
  for (const item of items) {
    const label = entityPickerRoomLabel(item);
    if (!groups.has(label)) {
      groups.set(label, []);
    }
    groups.get(label).push(item);
  }
  return [...groups.entries()]
    .map(([room, entries]) => ({
      room,
      entries: entries.sort((a, b) => a.name.localeCompare(b.name) || a.id.localeCompare(b.id)),
    }))
    .sort((a, b) => {
      const unassigned = t("entity_picker.unassigned_room");
      if (a.room === unassigned && b.room !== unassigned) return 1;
      if (b.room === unassigned && a.room !== unassigned) return -1;
      return a.room.localeCompare(b.room);
    });
}

function renderLightEntityPicker(data = {}) {
  if (!el.lightEntityPickerRooms || !el.lightEntityPickerStatus) return;

  const config = entityPickerConfig();
  const domain = config.domain;
  const search = entityPickerSearchValue();
  const cacheKey = entityPickerCacheKey(domain, search);
  const widgetLabel = t(config.widgetKey, {}, config.widgetFallback);
  const itemsLabel = entityPickerItemsLabel(config);
  const searchReady = entityPickerSearchReady(config);
  const liveSearch = entityPickerLiveSearchEnabled(config);
  const inspectorTarget = editor.lightPicker.inspectorTarget;
  const sourceItems = Object.prototype.hasOwnProperty.call(data, "items")
    ? data.items
    : editor.lightPicker.items;
  const items = normalizeEntityPickerItems(sourceItems, domain);

  if (el.lightEntityPickerTitle) {
    el.lightEntityPickerTitle.textContent = t(config.titleKey, {}, config.titleFallback);
  }
  if (el.lightEntityPickerBlankBtn) {
    el.lightEntityPickerBlankBtn.textContent = t(config.blankKey, {}, config.blankFallback);
    el.lightEntityPickerBlankBtn.classList.toggle("hidden", Boolean(inspectorTarget));
  }
  if (el.lightEntityPickerRefreshBtn) {
    el.lightEntityPickerRefreshBtn.textContent = liveSearch ? t("entity_picker.refresh") : t("entity_picker.search");
  }
  if (el.lightEntityPickerSearch && el.lightEntityPickerSearch.value !== editor.lightPicker.search) {
    el.lightEntityPickerSearch.value = editor.lightPicker.search;
  }

  if (items.length > 0 || data.status === "ready" || data.status === "refreshing") {
    editor.lightPicker.items = items;
    editor.lightPicker.itemsByDomain[cacheKey] = items;
    editor.lightPicker.hasLoaded = true;
    editor.lightPicker.loadedByDomain[cacheKey] = true;
    mergeEntityPickerItemsIntoEntities(items, domain);
  }
  editor.lightPicker.lastStatus = data.status || editor.lightPicker.lastStatus;

  const pending = data.pending === true || editor.lightPicker.loading;
  let statusText = "";
  if (data.status === "disconnected") {
    statusText = t("entity_picker.disconnected");
  } else if (!searchReady && !editor.lightPicker.items.length) {
    statusText = t("entity_picker.search_hint", { count: config.minSearch, items: itemsLabel });
  } else if (!liveSearch && searchReady && !editor.lightPicker.hasLoaded && !pending && !editor.lightPicker.items.length) {
    statusText = t("entity_picker.search_ready", { items: itemsLabel });
  } else if (data.status === "refreshing") {
    statusText = t("entity_picker.refreshing_items", { items: itemsLabel });
  } else if (pending && !editor.lightPicker.items.length) {
    statusText = t("entity_picker.pending");
  } else if (!editor.lightPicker.items.length) {
    statusText = t("entity_picker.empty_items", { items: itemsLabel });
  }
  if (data.truncated) {
    statusText = statusText ? `${statusText}\n${t("entity_picker.truncated")}` : t("entity_picker.truncated");
  }
  el.lightEntityPickerStatus.textContent = statusText;

  const loaded = Number.isFinite(Number(data.loaded)) ? Number(data.loaded) : editor.lightPicker.items.length;
  const rawTotal = Number.isFinite(Number(data.total)) ? Number(data.total) : 0;
  const limit = Number.isFinite(Number(data.limit)) && Number(data.limit) > 0 ? Number(data.limit) : loaded;
  const target = rawTotal > 0 ? Math.min(rawTotal, limit) : limit;
  const progressVisible = searchReady && (pending || rawTotal > 0 || data.truncated === true);
  if (el.lightEntityPickerProgress && el.lightEntityPickerProgressBar && el.lightEntityPickerProgressText) {
    el.lightEntityPickerProgress.classList.toggle("hidden", !progressVisible);
    const pct = target > 0 ? Math.min(100, Math.max(0, (loaded * 100) / target)) : (pending ? 8 : 0);
    el.lightEntityPickerProgressBar.style.width = `${pct}%`;
    el.lightEntityPickerProgressText.textContent = rawTotal > 0
      ? t("entity_picker.progress_total", {
        loaded: Math.min(loaded, target),
        target,
        total: rawTotal,
      })
      : t("entity_picker.progress", { loaded, target: target || "..." });
  }

  const groups = groupEntityPickerItemsByRoom(editor.lightPicker.items);
  el.lightEntityPickerRooms.innerHTML = "";
  for (const group of groups) {
    const details = document.createElement("details");
    details.className = "light-picker-room";
    details.open = groups.length <= 3 || group.entries.length <= 8;

    const summary = document.createElement("summary");
    summary.textContent = `${group.room} (${group.entries.length})`;
    details.appendChild(summary);

    const list = document.createElement("div");
    list.className = "light-picker-room-list";
    for (const item of group.entries) {
      const button = document.createElement("button");
      button.className = "light-picker-entity";
      button.type = "button";
      button.title = item.id;
      button.innerHTML = `<strong></strong><span></span>`;
      button.querySelector("strong").textContent = item.name || item.id;
      button.querySelector("span").textContent = item.id;
      button.onclick = () => {
        if (editor.lightPicker.inspectorTarget?.input) {
          const input = editor.lightPicker.inspectorTarget.input;
          input.value = item.id;
          input.dispatchEvent(new Event("change", { bubbles: true }));
          input.focus();
          closeLightEntityPicker();
          setStatus(t("entity_picker.assigned", { entity: item.id }));
          return;
        }
        addWidget(config.widgetType || editor.lightPicker.widgetType, {
          entityId: item.id,
          title: item.name || item.id,
        });
        closeLightEntityPicker();
        setStatus(t("entity_picker.added_widget", { widget: widgetLabel, entity: item.id }));
      };
      list.appendChild(button);
    }
    details.appendChild(list);
    el.lightEntityPickerRooms.appendChild(details);
  }
}

async function fetchLightEntityPicker(options = {}) {
  const refresh = options.refresh === true;
  const pollCount = Number(options.pollCount || 0);
  const config = entityPickerConfig();
  const domain = config.domain;
  const search = entityPickerSearchValue();
  const itemsLabel = entityPickerItemsLabel(config);
  if (!entityPickerSearchReady(config)) {
    editor.lightPicker.loading = false;
    renderLightEntityPicker({
      status: "idle",
      pending: false,
      items: editor.lightPicker.items,
    });
    return;
  }
  const requestSeq = ++editor.lightPicker.requestSeq;
  editor.lightPicker.loading = true;
  if (pollCount === 0 || refresh) {
    const startStatus = refresh && editor.lightPicker.items.length > 0 ? "refreshing" : "pending";
    renderLightEntityPicker({
      status: startStatus,
      pending: true,
      items: editor.lightPicker.items,
    });
  }

  const params = new URLSearchParams();
  params.set("domain", domain);
  if (search) params.set("search", search);
  if (refresh) params.set("refresh", "1");

  try {
    const data = await apiGet(`/api/ha/light_entities${params.toString() ? `?${params.toString()}` : ""}`);
    if (requestSeq !== editor.lightPicker.requestSeq) return;
    editor.lightPicker.loading = data.pending === true;
    renderLightEntityPicker(data);

    if (data.pending === true && pollCount < LIGHT_ENTITY_PICKER_MAX_POLLS) {
      clearLightEntityPickerPoll();
      editor.lightPicker.pollTimerId = window.setTimeout(() => {
        editor.lightPicker.pollTimerId = null;
        void fetchLightEntityPicker({ refresh: false, pollCount: pollCount + 1 });
      }, LIGHT_ENTITY_PICKER_POLL_MS);
    }
  } catch (err) {
    if (requestSeq !== editor.lightPicker.requestSeq) return;
    editor.lightPicker.loading = false;
    const message = t("entity_picker.fetch_failed_items", { items: itemsLabel, error: err.message });
    el.lightEntityPickerStatus.textContent = message;
    setStatus(message, true);
  }
}

function openLightEntityPicker(widgetType = "light_tile", options = {}) {
  editor.lightPicker.inspectorTarget = options.inspectorTarget || null;
  editor.lightPicker.configOverride = options.configOverride || null;
  const config = entityPickerConfig(widgetType);
  if (!el.lightEntityPickerOverlay) {
    addWidget(config.widgetType || widgetType);
    editor.lightPicker.inspectorTarget = null;
    editor.lightPicker.configOverride = null;
    return;
  }
  editor.lightPicker.widgetType = widgetType;
  editor.lightPicker.domain = config.domain;
  editor.lightPicker.search = editor.lightPicker.searchByDomain[config.domain] || "";
  if (el.lightEntityPickerSearch) {
    el.lightEntityPickerSearch.value = editor.lightPicker.search;
  }
  const cacheKey = entityPickerCacheKey(config.domain);
  editor.lightPicker.items = editor.lightPicker.itemsByDomain[cacheKey] || [];
  editor.lightPicker.hasLoaded = editor.lightPicker.loadedByDomain[cacheKey] === true;
  const shouldAutoFetch =
    !editor.lightPicker.hasLoaded && entityPickerSearchReady(config) && entityPickerLiveSearchEnabled(config);
  el.lightEntityPickerOverlay.classList.remove("hidden");
  renderLightEntityPicker({
    status: editor.lightPicker.hasLoaded ? "ready" : (shouldAutoFetch ? "pending" : "idle"),
    pending: shouldAutoFetch,
    items: editor.lightPicker.items,
  });
  if (shouldAutoFetch) {
    void fetchLightEntityPicker();
  }
}

function closeLightEntityPicker() {
  clearLightEntityPickerPoll();
  clearLightEntityPickerSearchDebounce();
  editor.lightPicker.requestSeq += 1;
  cancelLightEntityPickerRequest();
  editor.lightPicker.loading = false;
  if (el.lightEntityPickerOverlay) {
    el.lightEntityPickerOverlay.classList.add("hidden");
  }
  editor.lightPicker.inspectorTarget = null;
  editor.lightPicker.configOverride = null;
  if (el.lightEntityPickerBlankBtn) {
    el.lightEntityPickerBlankBtn.classList.remove("hidden");
  }
}

function layoutWidgetCount() {
  return (editor.layout?.pages || []).reduce((count, page) => (
    count + (Array.isArray(page.widgets) ? page.widgets.length : 0)
  ), 0);
}

function setupWizardPending() {
  return storageGet(SETUP_WIZARD_PENDING_STORAGE_KEY) === "1";
}

function markSetupWizardPending() {
  storageSet(SETUP_WIZARD_PENDING_STORAGE_KEY, "1");
  storageRemove(SETUP_WIZARD_DISMISSED_STORAGE_KEY);
}

function markSetupWizardDismissed() {
  storageRemove(SETUP_WIZARD_PENDING_STORAGE_KEY);
  storageSet(SETUP_WIZARD_DISMISSED_STORAGE_KEY, "1");
}

function setupWizardShouldAutoOpen() {
  if (!el.setupWizardOverlay || !editor.layout) return false;
  if (setupWizardPending()) return true;
  if (storageGet(SETUP_WIZARD_DISMISSED_STORAGE_KEY) === "1") return false;
  return Boolean(editor.settings?.ha?.configured) && layoutWidgetCount() === 0;
}

function setupWizardCountText() {
  const count = selectedPage()?.widgets?.length || 0;
  if (count <= 0) return t("setup.count_none");
  if (count === 1) return t("setup.count_one");
  return t("setup.count_many", { count });
}

function applySetupWizardPageTitle() {
  const page = selectedPage();
  if (!page || !el.setupWizardPageTitle) return;
  const title = el.setupWizardPageTitle.value.trim();
  if (title) {
    page.title = title;
    renderAll();
  }
}

function renderSetupWizard() {
  if (!el.setupWizardOverlay) return;
  if (el.setupWizardPageTitle && document.activeElement !== el.setupWizardPageTitle) {
    el.setupWizardPageTitle.value = selectedPage()?.title || t("layout.default_page.title");
  }
  if (el.setupWizardCount) {
    el.setupWizardCount.textContent = setupWizardCountText();
  }
}

function openSetupWizard(options = {}) {
  if (!el.setupWizardOverlay || !editor.layout) return;
  editor.setupWizard.active = true;
  editor.setupWizard.openedManually = options.manual === true;
  editor.setupWizard.addedSinceOpen = 0;
  if (el.setupWizardStatus) {
    el.setupWizardStatus.textContent = "";
    el.setupWizardStatus.classList.remove("error");
  }
  renderSetupWizard();
  el.setupWizardOverlay.classList.remove("hidden");
}

function closeSetupWizard(dismiss = true) {
  editor.setupWizard.active = false;
  if (dismiss) {
    markSetupWizardDismissed();
  }
  if (el.setupWizardOverlay) {
    el.setupWizardOverlay.classList.add("hidden");
  }
}

function openSetupWizardEntityPicker(widgetType) {
  applySetupWizardPageTitle();
  openLightEntityPicker(widgetType);
}

function onSetupWizardWidgetAdded(widget) {
  if (!editor.setupWizard.active || !widget) return;
  editor.setupWizard.addedSinceOpen += 1;
  if (el.setupWizardStatus) {
    el.setupWizardStatus.textContent = t("setup.added", { title: widget.title || widget.id });
    el.setupWizardStatus.classList.remove("error");
  }
  renderSetupWizard();
}

async function saveSetupWizardLayout(options = {}) {
  applySetupWizardPageTitle();
  if (el.setupWizardStatus) {
    el.setupWizardStatus.textContent = t("setup.saving");
    el.setupWizardStatus.classList.remove("error");
  }
  try {
    await saveLayout();
    if (el.setupWizardStatus) {
      el.setupWizardStatus.textContent = t("setup.saved");
      el.setupWizardStatus.classList.remove("error");
    }
    markSetupWizardDismissed();
    if (options.closeOnSuccess === true) {
      closeSetupWizard(false);
    }
    return true;
  } catch (err) {
    if (el.setupWizardStatus) {
      el.setupWizardStatus.textContent = t("setup.save_failed", { error: err.message });
      el.setupWizardStatus.classList.add("error");
    }
    return false;
  }
}

function renderEntityOptions() {
  const inspectorType = inspectorWidgetType();
  const sliderDomain = inspectorSliderEntityDomain();
  const buttonMode = inspectorButtonMode();
  const secondaryConfig = secondaryEntityConfigForWidgetType(inspectorType);
  const inspectorOptions = listEntitiesForWidgetType(inspectorType, sliderDomain, buttonMode);
  const primaryOptions = inspectorOptions.length > 0 ? inspectorOptions : editor.entities;
  setEntityOptionsList(el.entityOptions, primaryOptions.slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS));

  setEntityOptionsList(
    el.sensorEntityOptions,
    (secondaryConfig.domain || inspectorType === "solar_forecast")
      ? listEntitiesByDomain(inspectorType === "solar_forecast" ? "sensor" : secondaryConfig.domain).slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS)
      : [],
  );
  if (el.energyEntityOptions) {
    setEntityOptionsList(el.energyEntityOptions, listEntitiesByDomain("sensor").slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS));
  }
  if (el.settingsWeatherEntityOptions) {
    setEntityOptionsList(
      el.settingsWeatherEntityOptions,
      listEntitiesByDomain("weather").slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS),
    );
  }
  if (el.settingsStockEntityOptions) {
    setEntityOptionsList(
      el.settingsStockEntityOptions,
      listEntitiesByDomain("sensor").slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS),
    );
  }

  if (el.fSecondaryEntityLabel) {
    const primaryLabelKey = inspectorType === "solar_forecast"
      ? "layout.inspector.solar_remaining_entity"
      : "layout.inspector.entity";
    setTextById("fEntityLabel", primaryLabelKey);
    el.fSecondaryEntityLabel.textContent = t(secondaryConfig.labelKey, {}, secondaryConfig.labelFallback);
  }

  const secondaryEnabled = secondaryConfig.enabled;
  if (el.fSecondaryEntityWrap) {
    el.fSecondaryEntityWrap.classList.toggle("hidden", !secondaryEnabled);
  }
  el.fSecondaryEntity.disabled = !secondaryEnabled;
  if (!secondaryEnabled) {
    el.fSecondaryEntity.value = "";
  }

  const primaryEnabled = inspectorType !== "empty_tile";
  if (el.fEntityWrap) {
    el.fEntityWrap.classList.toggle("hidden", !primaryEnabled);
  }
  if (el.fEntity) {
    el.fEntity.disabled = !primaryEnabled;
    if (!primaryEnabled) {
      el.fEntity.value = "";
    }
  }

  document.querySelectorAll("[data-inspector-entity-target]").forEach((button) => {
    const target = inspectorEntityPickerTarget(button.dataset.inspectorEntityTarget || "");
    button.disabled = !target;
  });
}

function renderPages() {
  el.pagesList.innerHTML = "";
  for (const page of editor.layout.pages) {
    const li = document.createElement("li");
    li.className = `list-item ${page.id === editor.selectedPageId ? "active selected" : ""}`;

    const label = document.createElement("span");
    const badge = isEnergyPage(page) ? " ⚡" : "";
    label.textContent = `${page.title || page.id}${badge}`;
    label.title = `[${page.id}] ${isEnergyPage(page) ? "energy" : "page"}`;
    label.className = "list-item-label";
    li.appendChild(label);
    li.onclick = () => {
      editor.selectedPageId = page.id;
      editor.selectedWidgetId = null;
      renderAll();
    };

    const actions = document.createElement("span");
    actions.className = "row-actions";

    const renameBtn = document.createElement("button");
    renameBtn.type = "button";
    renameBtn.className = "row-icon-btn";
    renameBtn.title = t("layout.pages.rename") || "Rename";
    renameBtn.textContent = "✎";
    renameBtn.onclick = (ev) => {
      ev.stopPropagation();
      startInlinePageRename(li, label, page);
    };
    actions.appendChild(renameBtn);

    const delBtn = document.createElement("button");
    delBtn.type = "button";
    delBtn.className = "row-icon-btn row-delete-btn";
    delBtn.title = t("layout.pages.delete") || "Delete";
    delBtn.textContent = "✕";
    delBtn.setAttribute("aria-label", t("layout.pages.delete") || "Delete");
    delBtn.onclick = (ev) => {
      ev.stopPropagation();
      editor.selectedPageId = page.id;
      deletePage();
    };
    actions.appendChild(delBtn);

    li.appendChild(actions);
    el.pagesList.appendChild(li);
  }
  renderPagesMini();
}

function startInlinePageRename(li, labelSpan, page) {
  if (!li || !page) return;
  const input = document.createElement("input");
  input.type = "text";
  input.className = "row-rename-input";
  input.value = page.title || page.id;
  input.maxLength = 63;

  const commit = (save) => {
    if (save) {
      const next = input.value.trim();
      page.title = next || page.id;
      if (isEnergyPage(page)) {
        applyEnergyPageConfig({ render: false });
      }
    }
    renderAll();
  };

  input.onkeydown = (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      commit(true);
    } else if (e.key === "Escape") {
      e.preventDefault();
      commit(false);
    }
  };
  input.onblur = () => commit(true);

  li.replaceChild(input, labelSpan);
  input.focus();
  input.select();
}

function renderPagesMini() {
  if (!el.pagesMiniList) return;
  el.pagesMiniList.innerHTML = "";
  for (const page of editor.layout.pages) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = `mini-page-btn ${page.id === editor.selectedPageId ? "active" : ""}`;
    button.textContent = `${isEnergyPage(page) ? "E " : ""}${page.title || page.id}`;
    button.title = `${page.title || page.id} [${page.id}]`;
    button.onclick = () => {
      editor.selectedPageId = page.id;
      editor.selectedWidgetId = null;
      renderAll();
    };
    el.pagesMiniList.appendChild(button);
  }
}

function renderPageEditor() {
  const page = selectedPage();
  if (!page) {
    el.pageTitleInput.value = "";
    el.pageTitleInput.disabled = true;
    el.applyPageBtn.disabled = true;
    if (el.energyPageOptions) {
      el.energyPageOptions.classList.add("hidden");
    }
    return;
  }
  el.pageTitleInput.disabled = false;
  el.applyPageBtn.disabled = false;
  el.pageTitleInput.value = page.title || page.id;

  const energyPage = isEnergyPage(page);
  if (el.energyPageOptions) {
    el.energyPageOptions.classList.toggle("hidden", !energyPage);
  }
  if (energyPage) {
    normalizeEnergyConfig(page);
    if (el.energySource) {
      el.energySource.value = page.energy.source || ENERGY_SOURCE_HA;
    }
    updateEnergySourceUi(page.energy.source);
    const inputs = getEnergyInputs();
    for (const key of ENERGY_ENTITY_KEYS) {
      if (inputs[key]) {
        inputs[key].value = page.energy[key] || "";
      }
    }
  }
}

function updateEnergySourceUi(source) {
  const checkedSource = ENERGY_SOURCES.has(source) ? source : ENERGY_SOURCE_HA;
  const isManual = checkedSource === ENERGY_SOURCE_MANUAL;
  if (el.energyManualOptions) {
    el.energyManualOptions.classList.toggle("hidden", !isManual);
  }
  if (el.energySourceHint) {
    el.energySourceHint.textContent = t(isManual ? "layout.energy.source_hint_manual" : "layout.energy.source_hint_ha");
  }
}

function renderWidgets() {
  const page = selectedPage();
  el.widgetsList.innerHTML = "";
  if (!page) return;

  const energyPage = isEnergyPage(page);
  const addButtons = [
    el.addSensorBtn,
    el.addButtonBtn,
    el.addSliderBtn,
    el.addGraphBtn,
    el.addEmptyTileBtn,
    el.addLightTileBtn,
    el.addHeatingTileBtn,
    el.addWeatherTileBtn,
    el.addWeather3DayBtn,
    el.addTodoListBtn,
    el.addMediaPlayerBtn,
  ];
  for (const button of addButtons) {
    if (button) button.disabled = energyPage;
  }
  if (el.openSetupWizardBtn) {
    el.openSetupWizardBtn.disabled = energyPage;
  }
  if (el.deleteWidgetBtn) {
    el.deleteWidgetBtn.disabled = energyPage || !editor.selectedWidgetId;
  }

  if (energyPage) {
    const li = document.createElement("li");
    li.className = "list-item muted";
    li.textContent = t("layout.energy.no_widgets");
    el.widgetsList.appendChild(li);
    return;
  }

  for (const widget of page.widgets) {
    const li = document.createElement("li");
    li.className = `list-item ${widget.id === editor.selectedWidgetId ? "active selected" : ""}`;

    const label = document.createElement("span");
    label.className = "list-item-label";
    label.textContent = `${widgetDisplayLabel(widget)}`;
    label.title = `[${widget.id}] ${widget.type}`;
    li.appendChild(label);
    li.onclick = () => {
      editor.selectedWidgetId = widget.id;
      renderAll();
    };

    const actions = document.createElement("span");
    actions.className = "row-actions";
    const delBtn = document.createElement("button");
    delBtn.type = "button";
    delBtn.className = "row-icon-btn row-delete-btn";
    delBtn.title = t("layout.widgets.delete") || "Delete";
    delBtn.textContent = "✕";
    delBtn.setAttribute("aria-label", t("layout.widgets.delete") || "Delete");
    delBtn.onclick = (ev) => {
      ev.stopPropagation();
      editor.selectedWidgetId = widget.id;
      if (typeof el.deleteWidgetBtn?.click === "function") {
        el.deleteWidgetBtn.click();
      }
    };
    actions.appendChild(delBtn);
    li.appendChild(actions);

    el.widgetsList.appendChild(li);
  }
}

function widgetDisplayLabel(widget) {
  const title = (widget.title || "").trim();
  if (title) return `${title} · ${widget.type}`;
  return `${widget.type} [${widget.id}]`;
}

function geometryStyle(node, rect) {
  node.style.left = `${rect.x}px`;
  node.style.top = `${rect.y}px`;
  node.style.width = `${rect.w}px`;
  node.style.height = `${rect.h}px`;
}

function selectWidgetLive(widgetId, selectedBox) {
  editor.selectedWidgetId = widgetId;
  document.querySelectorAll(".widget-box.selected").forEach((node) => node.classList.remove("selected"));
  if (selectedBox) {
    selectedBox.classList.add("selected");
  }
  renderWidgets();
  renderInspector();
}

function attachDragAndResize(box, widget) {
  const startMove = (mode, downEvent, captureTarget) => {
    downEvent.preventDefault();
    downEvent.stopPropagation();
    const startX = downEvent.clientX;
    const startY = downEvent.clientY;
    const startRect = { ...widget.rect };
    const pointerId = downEvent.pointerId;
    let moved = false;
    let finished = false;

    // Capture all subsequent pointer events on this element so they don't get
    // hijacked by native drag, hover over iframes/images, or focus changes.
    try { captureTarget.setPointerCapture(pointerId); } catch (_) { /* ignore */ }

    // Suppress full canvas re-renders triggered by HA state pushes / other
    // async events while the user is interacting with the tile.
    editor.canvasInteractionActive = true;
    editor.canvasRenderPending = false;

    box.classList.add(mode === "drag" ? "dragging" : "resizing");

    const onMove = (moveEvent) => {
      if (moveEvent.pointerId !== pointerId) return;
      const dx = moveEvent.clientX - startX;
      const dy = moveEvent.clientY - startY;
      const limits = widgetSizeLimits(widget.type);
      const maxW = Math.min(limits.maxW, CANVAS_WIDTH);
      const maxH = Math.min(limits.maxH, CANVAS_HEIGHT);
      const minW = Math.min(limits.minW, maxW);
      const minH = Math.min(limits.minH, maxH);
      let nextX = widget.rect.x;
      let nextY = widget.rect.y;
      let nextW = widget.rect.w;
      let nextH = widget.rect.h;

      if (mode === "drag") {
        nextX = clamp(snap(startRect.x + dx), 0, CANVAS_WIDTH - startRect.w);
        nextY = clamp(snap(startRect.y + dy), 0, CANVAS_HEIGHT - startRect.h);
      } else {
        nextW = clamp(snap(startRect.w + dx), minW, Math.min(maxW, CANVAS_WIDTH - startRect.x));
        nextH = clamp(snap(startRect.h + dy), minH, Math.min(maxH, CANVAS_HEIGHT - startRect.y));
      }

      if (nextX !== widget.rect.x || nextY !== widget.rect.y || nextW !== widget.rect.w || nextH !== widget.rect.h) {
        moved = true;
        widget.rect.x = nextX;
        widget.rect.y = nextY;
        widget.rect.w = nextW;
        widget.rect.h = nextH;
        geometryStyle(box, widget.rect);
        renderInspector();
      }
    };

    const cleanup = () => {
      if (finished) return;
      finished = true;
      captureTarget.removeEventListener("pointermove", onMove);
      captureTarget.removeEventListener("pointerup", onUp);
      captureTarget.removeEventListener("pointercancel", onUp);
      captureTarget.removeEventListener("lostpointercapture", onUp);
      try { captureTarget.releasePointerCapture(pointerId); } catch (_) { /* ignore */ }
      box.classList.remove("dragging");
      box.classList.remove("resizing");
      editor.canvasInteractionActive = false;
      const hadPendingRender = editor.canvasRenderPending;
      editor.canvasRenderPending = false;
      if (moved) {
        renderWidgets();
        renderInspector();
      }
      if (hadPendingRender) {
        // Flush any canvas updates that async events requested while we were
        // dragging (e.g. HA state pushes). The geometry is already live via
        // geometryStyle(), so this only matters for content changes.
        renderCanvas();
      }
    };

    const onUp = (upEvent) => {
      if (upEvent && upEvent.pointerId !== undefined && upEvent.pointerId !== pointerId) return;
      cleanup();
    };

    captureTarget.addEventListener("pointermove", onMove);
    captureTarget.addEventListener("pointerup", onUp);
    captureTarget.addEventListener("pointercancel", onUp);
    captureTarget.addEventListener("lostpointercapture", onUp);
  };

  box.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) return;
    if (event.target.classList.contains("resize-handle")) return;
    selectWidgetLive(widget.id, box);
    startMove("drag", event, box);
  });

  const resizeHandle = box.querySelector(".resize-handle");
  resizeHandle.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) return;
    event.stopPropagation();
    selectWidgetLive(widget.id, box);
    startMove("resize", event, resizeHandle);
  });
}

function escapeHtml(value) {
  return String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function solarPreviewValue(entityId) {
  const raw = editor.states.get(entityId) ?? "";
  const parsed = Number(String(raw).replace(",", "."));
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : null;
}

function solarPreviewFormatValue(value, includeUnit = true) {
  if (value === null) return "--";
  const formatted = value >= 100 ? value.toFixed(0) : value.toFixed(1);
  return includeUnit ? `${formatted} kWh` : formatted;
}

function solarPreviewRows(widget) {
  const rows = [
    { label: "Rest", entityId: widget.entity_id },
    { label: "Heute", entityId: widget.forecast_today_entity_id },
    { label: "Morgen", entityId: widget.forecast_tomorrow_entity_id },
    { label: "Tag 3", entityId: widget.forecast_day_3_entity_id },
    { label: "Tag 4", entityId: widget.forecast_day_4_entity_id },
    { label: "Tag 5", entityId: widget.forecast_day_5_entity_id },
  ];
  const values = rows.map((row) => solarPreviewValue(row.entityId));
  const configuredMax = Number(widget.solar_forecast_bar_max_kwh);
  const maxValue = Number.isFinite(configuredMax) && configuredMax > 0
    ? configuredMax
    : Math.max(1, ...values.filter((value) => value !== null));
  return rows.map((row, index) => {
    const value = values[index];
    const percent = value === null ? 0 : clamp(Math.round((value * 100) / maxValue), 0, 100);
    return { ...row, value, percent };
  });
}

function solarPreviewMarkup(widget) {
  const orientation = normalizeSolarForecastBarOrientation(widget.solar_forecast_bar_orientation);
  const rows = solarPreviewRows(widget);
  const rowMarkup = rows.map((row) => {
    const widthStyle = `width:${row.percent}%`;
    const heightStyle = `height:${row.percent}%`;
    if (orientation === "vertical") {
      return `
        <div class="solar-preview-col">
          <div class="solar-preview-vbar"><i style="${heightStyle}"></i></div>
          <strong>${escapeHtml(row.label)}</strong>
          <span>${escapeHtml(solarPreviewFormatValue(row.value, false))}</span>
        </div>
      `;
    }
    return `
      <div class="solar-preview-row">
        <strong>${escapeHtml(row.label)}</strong>
        <div class="solar-preview-hbar"><i style="${widthStyle}"></i></div>
        <span>${escapeHtml(solarPreviewFormatValue(row.value))}</span>
      </div>
    `;
  }).join("");
  return `
    <div class="solar-preview ${orientation}">
      <div class="solar-preview-title">${escapeHtml(widget.title || "Solar Forecast")}</div>
      <div class="solar-preview-body">${rowMarkup}</div>
    </div>
    <div class="resize-handle"></div>
  `;
}

function energyPreviewSensorCount(count) {
  return t(count === 1 ? "layout.energy.sensor_count_one" : "layout.energy.sensor_count_many", { count });
}

function energyPreviewEntityLabel(entityId) {
  return escapeHtml(entityId || t("layout.energy.no_sensor"));
}

function energyPreviewNodeMarkup(id, label, value, style = "") {
  const styleAttr = style ? ` style="${escapeHtml(style)}"` : "";
  return `<div class="energy-preview-node ${id}"${styleAttr}><span>${escapeHtml(label)}</span><strong>${escapeHtml(value)}</strong></div>`;
}

function energyPreviewLabelMarkup(id, title, detail) {
  return `<div class="energy-preview-label ${id}"><strong>${escapeHtml(title)}</strong><small>${energyPreviewEntityLabel(detail)}</small></div>`;
}

function energyPreviewNumber(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function energyPreviewPositive(value) {
  const n = energyPreviewNumber(value);
  return n === null ? 0 : Math.max(n, 0);
}

function energyPreviewFormatValue(value, unit = "") {
  const n = energyPreviewNumber(value);
  const suffix = String(unit || "").trim();
  if (n === null) return suffix ? `-- ${suffix}` : "--";
  const abs = Math.abs(n);
  const decimals = abs === 0 || abs >= 100 ? 0 : 1;
  const text = n.toFixed(decimals);
  return suffix ? `${text} ${suffix}` : text;
}

function energyPreviewFormatKwh(value) {
  return energyPreviewFormatValue(value, "kWh");
}

function energyPreviewComputeFlows(snapshot) {
  let fromGrid = energyPreviewPositive(snapshot?.from_grid_kwh);
  let toGrid = energyPreviewPositive(snapshot?.to_grid_kwh);
  let solar = energyPreviewPositive(snapshot?.solar_kwh);
  let toBattery = energyPreviewPositive(snapshot?.to_battery_kwh);
  let fromBattery = energyPreviewPositive(snapshot?.from_battery_kwh);
  const out = {
    usedSolar: 0,
    usedGrid: 0,
    usedBattery: 0,
    usedTotal: fromGrid + solar + fromBattery - toGrid - toBattery,
    gridToBattery: 0,
    batteryToGrid: 0,
    solarToBattery: 0,
    solarToGrid: 0,
  };
  let remaining = Math.max(out.usedTotal, 0);

  const gridToBattery = Math.max(0, Math.min(toBattery, fromGrid - remaining));
  out.gridToBattery += gridToBattery;
  toBattery -= gridToBattery;
  fromGrid -= gridToBattery;

  out.solarToBattery = Math.min(solar, toBattery);
  toBattery -= out.solarToBattery;
  solar -= out.solarToBattery;

  out.solarToGrid = Math.min(solar, toGrid);
  toGrid -= out.solarToGrid;
  solar -= out.solarToGrid;

  out.batteryToGrid = Math.min(fromBattery, toGrid);
  fromBattery -= out.batteryToGrid;
  toGrid -= out.batteryToGrid;

  const secondGridToBattery = Math.min(fromGrid, toBattery);
  out.gridToBattery += secondGridToBattery;
  fromGrid -= secondGridToBattery;
  toBattery -= secondGridToBattery;

  out.usedSolar = Math.min(remaining, solar);
  remaining -= out.usedSolar;
  out.usedBattery = Math.min(fromBattery, remaining);
  remaining -= out.usedBattery;
  out.usedGrid = Math.min(remaining, fromGrid);
  return out;
}

function energyPreviewHomeRingStyle(flows) {
  const parts = [
    { color: ENERGY_PREVIEW_COLORS.solar, value: energyPreviewPositive(flows?.usedSolar) },
    { color: ENERGY_PREVIEW_COLORS.battery, value: energyPreviewPositive(flows?.usedBattery) },
    { color: ENERGY_PREVIEW_COLORS.grid, value: energyPreviewPositive(flows?.usedGrid) },
  ].filter((part) => part.value > 0.001);
  const total = parts.reduce((sum, part) => sum + part.value, 0);
  if (total <= 0.001 || parts.length === 0) {
    return `--energy-home-ring: ${ENERGY_PREVIEW_COLORS.idle} 0deg 360deg;`;
  }

  let start = 0;
  const segments = parts.map((part, index) => {
    if (index === parts.length - 1) {
      return `${part.color} ${start}deg 360deg`;
    }
    const remainingSegments = parts.length - index - 1;
    const maxEnd = 360 - remainingSegments;
    let end = Math.round(start + (part.value / total) * 360);
    end = Math.max(start + 1, Math.min(maxEnd, end));
    const segment = `${part.color} ${start}deg ${end}deg`;
    start = end;
    return segment;
  });
  return `--energy-home-ring: ${segments.join(", ")};`;
}

function energyPreviewFlowMarkup(id, visible, path, dot) {
  if (!visible) return "";
  const dotMarkup = dot ? `<circle class="dot ${id}" cx="${dot[0]}" cy="${dot[1]}" r="5" />` : "";
  return `<path class="flow ${id}" d="${path}" />${dotMarkup}`;
}

function renderEnergyCanvasPreview(page) {
  const compact = isCompactCanvas();
  const energy = page.energy || {};
  const source = energy.source === ENERGY_SOURCE_MANUAL ? ENERGY_SOURCE_MANUAL : ENERGY_SOURCE_HA;
  const isManual = source === ENERGY_SOURCE_MANUAL;
  const configured = ENERGY_ENTITY_KEYS.filter((key) => energy[key]).length;
  const autoLabel = t("layout.energy.preview_auto");
  const headerBadge = isManual ? energyPreviewSensorCount(configured) : t("layout.energy.preview_source_ha");
  const snapshot = !isManual && editor.energySnapshot?.available === true ? editor.energySnapshot : null;
  const snapshotFlows = snapshot ? energyPreviewComputeFlows(snapshot) : null;
  const solarEntity = isManual ? energy.solar_power_entity_id : autoLabel;
  const gridEntity = isManual
    ? (energy.grid_power_entity_id || energy.grid_import_power_entity_id || energy.grid_export_power_entity_id)
    : autoLabel;
  const homeEntity = isManual ? energy.home_power_entity_id : autoLabel;
  const batteryEntity = isManual
    ? (energy.battery_power_entity_id || energy.battery_charge_power_entity_id ||
      energy.battery_discharge_power_entity_id || energy.battery_soc_entity_id)
    : autoLabel;
  const nodes = isManual ? {
    lowCarbon: false,
    solar: !!solarEntity,
    gas: false,
    grid: !!gridEntity,
    home: !!homeEntity || configured > 0,
    battery: !!batteryEntity,
    water: false,
  } : {
    lowCarbon: false,
    solar: snapshot?.has_solar === true,
    gas: snapshot?.has_gas === true,
    grid: snapshot?.has_grid === true,
    home: true,
    battery: snapshot?.has_battery === true,
    water: snapshot?.has_water === true,
  };
  if (isManual && configured === 0) {
    nodes.home = true;
  }
  const homeValue = snapshot ? energyPreviewFormatKwh(Math.max(snapshotFlows?.usedTotal || 0, 0)) : (isManual ? "-- W" : "-- kWh");
  const solarValue = snapshot ? energyPreviewFormatKwh(snapshot.solar_kwh) : (isManual ? "-- W" : "-- kWh");
  const gridImport = energyPreviewPositive(snapshot?.from_grid_kwh);
  const gridExport = energyPreviewPositive(snapshot?.to_grid_kwh);
  const gridValue = snapshot
    ? `${gridExport > gridImport && gridExport > 0.01 ? "out" : "in"} ${energyPreviewFormatKwh(Math.max(gridImport, gridExport))}`
    : (isManual ? "-- W" : "-- kWh");
  const batteryCharge = energyPreviewPositive(snapshot?.to_battery_kwh);
  const batteryDischarge = energyPreviewPositive(snapshot?.from_battery_kwh);
  const batteryValue = snapshot
    ? `${batteryCharge > batteryDischarge && batteryCharge > 0.01 ? "chg" : "out"} ${energyPreviewFormatKwh(Math.max(batteryCharge, batteryDischarge))}`
    : (isManual ? "--%" : "-- kWh");
  const gasValue = snapshot ? energyPreviewFormatValue(snapshot.gas_value, snapshot.gas_unit) : "--";
  const waterValue = snapshot ? energyPreviewFormatValue(snapshot.water_value, snapshot.water_unit) : "--";
  const homeRingStyle = energyPreviewHomeRingStyle(snapshotFlows);
  const flows = (compact ? [
    energyPreviewFlowMarkup("low-carbon", nodes.lowCarbon && nodes.grid, "M76 126 V162", [76, 150]),
    energyPreviewFlowMarkup("solar-return", nodes.solar && nodes.grid, "M225 125 V169 A24 24 0 0 1 201 193 H119", [174, 193]),
    energyPreviewFlowMarkup("solar", nodes.solar && nodes.home, "M255 125 V169 A24 24 0 0 0 279 193 H356", [318, 193]),
    energyPreviewFlowMarkup("battery-in", nodes.solar && nodes.battery, "M240 125 V247", [240, 186]),
    energyPreviewFlowMarkup("grid", nodes.grid && nodes.home, "M119 205 H356", [238, 205]),
    energyPreviewFlowMarkup("battery-out", nodes.battery && nodes.home, "M255 247 V241 A24 24 0 0 0 279 217 H356", [310, 217]),
    energyPreviewFlowMarkup("return", nodes.grid && nodes.battery, "M119 217 H201 A24 24 0 0 1 225 241 V247", [176, 217]),
    energyPreviewFlowMarkup("gas", nodes.gas && nodes.home, "M404 125 V157", [404, 145]),
    energyPreviewFlowMarkup("water", nodes.water && nodes.home, "M404 247 V253", [404, 250]),
  ] : [
    energyPreviewFlowMarkup("low-carbon", nodes.lowCarbon && nodes.grid, "M96 190 V282", [96, 254]),
    energyPreviewFlowMarkup("solar-return", nodes.solar && nodes.grid, "M312 190 V286 A40 40 0 0 1 272 326 H150", [250, 326]),
    energyPreviewFlowMarkup("solar", nodes.solar && nodes.home, "M312 190 V286 A40 40 0 0 0 352 326 H522", [402, 326]),
    energyPreviewFlowMarkup("battery-in", nodes.solar && nodes.battery, "M312 190 V452", [312, 312]),
    energyPreviewFlowMarkup("grid", nodes.grid && nodes.home, "M150 346 H522", [244, 346]),
    energyPreviewFlowMarkup("battery-out", nodes.battery && nodes.home, "M336 452 V386 A40 40 0 0 1 376 346 H522", [394, 346]),
    energyPreviewFlowMarkup("return", nodes.grid && nodes.battery, "M150 368 H272 A40 40 0 0 1 312 408 V452", [216, 368]),
    energyPreviewFlowMarkup("gas", nodes.gas && nodes.home, "M528 190 V282", [528, 254]),
    energyPreviewFlowMarkup("water", nodes.water && nodes.home, "M528 452 V402", [528, 426]),
  ]).join("");
  const nodeMarkup = [
    nodes.lowCarbon ? energyPreviewNodeMarkup("low-carbon", "LC", "-- kWh") : "",
    nodes.solar ? energyPreviewNodeMarkup("solar", "PV", solarValue) : "",
    nodes.gas ? energyPreviewNodeMarkup("gas", "GAS", gasValue) : "",
    nodes.grid ? energyPreviewNodeMarkup("grid", "GRID", gridValue) : "",
    nodes.home ? energyPreviewNodeMarkup("home", "HOME", homeValue, homeRingStyle) : "",
    nodes.battery ? energyPreviewNodeMarkup("battery", "BAT", batteryValue) : "",
    nodes.water ? energyPreviewNodeMarkup("water", "H2O", waterValue) : "",
  ].join("");
  const labelMarkup = [
    nodes.lowCarbon ? energyPreviewLabelMarkup("low-carbon", t("layout.energy.low_carbon"), autoLabel) : "",
    nodes.solar ? energyPreviewLabelMarkup("solar", t("layout.energy.solar"), solarEntity) : "",
    nodes.gas ? energyPreviewLabelMarkup("gas", t("layout.energy.gas"), autoLabel) : "",
    nodes.grid ? energyPreviewLabelMarkup("grid", t("layout.energy.grid"), gridEntity) : "",
    nodes.home ? energyPreviewLabelMarkup("home", t("layout.energy.home"), homeEntity) : "",
    nodes.battery ? energyPreviewLabelMarkup("battery", t("layout.energy.battery"), batteryEntity) : "",
    nodes.water ? energyPreviewLabelMarkup("water", t("layout.energy.water"), autoLabel) : "",
  ].join("");
  const node = document.createElement("div");
  node.className = compact ? "energy-page-preview compact" : "energy-page-preview";
  const previewWidth = CANVAS_WIDTH;
  const previewHeight = CANVAS_HEIGHT;
  node.style.setProperty("--energy-preview-width", `${previewWidth}px`);
  node.style.setProperty("--energy-preview-height", `${previewHeight}px`);
  const viewBox = `0 0 ${previewWidth} ${previewHeight}`;
  node.innerHTML = `
    <div class="energy-preview-card">
      <div class="energy-preview-heading">
        <strong>${escapeHtml(t("layout.energy.preview_title"))}</strong>
        <span>${escapeHtml(headerBadge)}</span>
      </div>
      <svg class="energy-preview-flow" viewBox="${viewBox}" aria-hidden="true">
        ${flows}
      </svg>
      ${nodeMarkup}
      ${labelMarkup}
    </div>
  `;
  el.canvas.appendChild(node);
}

function renderCanvas() {
  if (editor.activePane === "settings") {
    setActiveSettingsSection(editor.activeSettingsSection);
    return;
  }
  // While the user is actively dragging or resizing a tile we must not rebuild
  // the canvas DOM — doing so would destroy the element that owns the pointer
  // capture and cause the drag to "let go" mid-motion. Remember that a render
  // was requested and replay it once the interaction ends.
  if (editor.canvasInteractionActive) {
    editor.canvasRenderPending = true;
    return;
  }
  const page = selectedPage();
  el.canvas.innerHTML = "";
  if (!page) {
    el.canvasTitle.textContent = t("layout.canvas.title");
    return;
  }

  el.canvasTitle.textContent = `${t("layout.canvas.title")}: ${page.title || page.id}`;

  if (isEnergyPage(page)) {
    renderEnergyCanvasPreview(page);
    return;
  }

  for (const widget of page.widgets) {
    const box = document.createElement("div");
    const isEmptyTile = widget.type === "empty_tile";
    const isMediaPlayerButton = widget.type === "button" && String(widget.entity_id || "").startsWith("media_player.");
    const previewTitle = (isMediaPlayerButton && !String(widget.title || "").trim()) ? "" : (widget.title || widget.id);
    box.className = `widget-box ${isEmptyTile ? "empty-tile" : ""} ${widget.id === editor.selectedWidgetId ? "selected" : ""}`;
    box.dataset.widgetId = widget.id;
    box.style.zIndex = isEmptyTile ? "1" : "10";
    const previewState = isEmptyTile ? "design" : (editor.states.get(widget.entity_id) || "unavailable");
    let extraHint = "";
    if (widget.type === "weather_3day") {
      /* Mirrors the firmware layout in w_weather_tile.c:
       *   compact/panels3: ROWS_TOP=108, BOTTOM_PAD=12, ROW_HEIGHT=36, ROW_GAP=2
       *   default:         ROWS_TOP=150, BOTTOM_PAD=12, ROW_HEIGHT=44, ROW_GAP=4
       *   visible rows = clamp(floor((h-162+4)/48), 2, 6)
       *   forecast days = visible rows - 1 (the "Now" row).
       * Keep these constants in sync when the tile layout changes. */
      const h = Number(widget.rect && widget.rect.h) || 0;
      const compactForecast = isCompactCanvas();
      const rowsTop = compactForecast ? 108 : 150;
      const rowHeight = compactForecast ? 36 : 44;
      const rowGap = compactForecast ? 2 : 4;
      const avail = h - rowsTop - 12;
      let rows = 2;
      if (avail > rowHeight) {
        rows = Math.floor((avail + rowGap) / (rowHeight + rowGap));
      }
      if (rows < 2) rows = 2;
      if (rows > 6) rows = 6;
      const days = rows - 1;
      extraHint = `<div class="w-hint">forecast days ${days}/5</div>`;
    }
    if (widget.type === "solar_forecast") {
      box.innerHTML = solarPreviewMarkup(widget);
    } else {
      box.innerHTML = `
        <div class="w-type">${widget.type}</div>
        <div class="w-title">${previewTitle}</div>
        <div class="w-state">${previewState}</div>
        ${extraHint}
        <div class="resize-handle"></div>
      `;
    }
    geometryStyle(box, widget.rect);
    attachDragAndResize(box, widget);
    el.canvas.appendChild(box);
  }
}

function renderInspector() {
  const widget = selectedWidget();
  if (!widget) {
    el.fTitle.value = "";
    el.fType.value = "sensor";
    el.fEntity.value = "";
    el.fSecondaryEntity.value = "";
    el.fX.value = "";
    el.fY.value = "";
    el.fW.value = "";
    el.fH.value = "";
    if (el.buttonOptions) {
      el.buttonOptions.classList.add("hidden");
    }
    if (el.sliderOptions) {
      el.sliderOptions.classList.add("hidden");
    }
    if (el.graphOptions) {
      el.graphOptions.classList.add("hidden");
    }
    if (el.fSliderDirection) {
      el.fSliderDirection.value = DEFAULT_SLIDER_DIRECTION;
    }
    if (el.fSliderEntityDomain) {
      el.fSliderEntityDomain.value = DEFAULT_SLIDER_ENTITY_DOMAIN;
    }
    if (el.fButtonAccentColor) {
      el.fButtonAccentColor.value = DEFAULT_BUTTON_ACCENT_COLOR;
    }
    if (el.fButtonMode) {
      el.fButtonMode.value = DEFAULT_BUTTON_MODE;
    }
    if (el.fSliderAccentColor) {
      el.fSliderAccentColor.value = DEFAULT_SLIDER_ACCENT_COLOR;
    }
    if (el.fGraphLineColor) {
      el.fGraphLineColor.value = DEFAULT_GRAPH_LINE_COLOR;
    }
    if (el.fGraphTimeWindowMin) {
      el.fGraphTimeWindowMin.value = String(DEFAULT_GRAPH_TIME_WINDOW_MIN);
    }
    if (el.fGraphPointCount) {
      el.fGraphPointCount.value = "";
    }
    if (el.fGraphDisplayMode) {
      el.fGraphDisplayMode.value = DEFAULT_GRAPH_DISPLAY_MODE;
    }
    if (el.fGraphBarBucketMin) {
      el.fGraphBarBucketMin.value = String(DEFAULT_GRAPH_BAR_BUCKET_MIN);
    }
    if (el.fGraphBarBucketMinWrap) {
      el.fGraphBarBucketMinWrap.classList.add("hidden");
    }
    if (el.heatingOptions) {
      el.heatingOptions.classList.add("hidden");
    }
    if (el.fHeatingStyleVariant) {
      el.fHeatingStyleVariant.value = "default";
    }
    if (el.fHeatingArcOpening) {
      el.fHeatingArcOpening.value = "left";
    }
    if (el.fHeatingArcOpeningWrap) {
      el.fHeatingArcOpeningWrap.classList.add("hidden");
    }
    if (el.solarForecastFields) {
      el.solarForecastFields.classList.add("hidden");
    }
    if (el.solarForecastStyleOptions) {
      el.solarForecastStyleOptions.classList.add("hidden");
    }
    if (el.fSolarForecastBarOrientation) {
      el.fSolarForecastBarOrientation.value = DEFAULT_SOLAR_FORECAST_BAR_ORIENTATION;
    }
    for (const input of [
      el.fSolarForecastToday,
      el.fSolarForecastTomorrow,
      el.fSolarForecastDay3,
      el.fSolarForecastDay4,
      el.fSolarForecastDay5,
      el.fSolarForecastBarMax,
    ]) {
      if (input) input.value = "";
    }
    renderEntityOptions();
    return;
  }
  el.fTitle.value = widget.title || "";
  el.fType.value = widget.type;
  el.fEntity.value = widget.entity_id || "";
  el.fSecondaryEntity.value = widget.secondary_entity_id || "";
  if (el.fSolarForecastToday) el.fSolarForecastToday.value = widget.forecast_today_entity_id || "";
  if (el.fSolarForecastTomorrow) el.fSolarForecastTomorrow.value = widget.forecast_tomorrow_entity_id || "";
  if (el.fSolarForecastDay3) el.fSolarForecastDay3.value = widget.forecast_day_3_entity_id || "";
  if (el.fSolarForecastDay4) el.fSolarForecastDay4.value = widget.forecast_day_4_entity_id || "";
  if (el.fSolarForecastDay5) el.fSolarForecastDay5.value = widget.forecast_day_5_entity_id || "";
  if (el.fSolarForecastBarMax) el.fSolarForecastBarMax.value = widget.solar_forecast_bar_max_kwh > 0 ? String(widget.solar_forecast_bar_max_kwh) : "";
  if (el.fSolarForecastBarOrientation) {
    el.fSolarForecastBarOrientation.value = normalizeSolarForecastBarOrientation(widget.solar_forecast_bar_orientation);
  }
  el.fX.value = widget.rect.x;
  el.fY.value = widget.rect.y;
  el.fW.value = widget.rect.w;
  el.fH.value = widget.rect.h;

  const isButton = widget.type === "button";
  const isSlider = widget.type === "slider";
  const isGraph = widget.type === "graph";
  const isHeating = widget.type === "heating_tile";
  const isSolarForecast = widget.type === "solar_forecast";
  if (el.buttonOptions) {
    el.buttonOptions.classList.toggle("hidden", !isButton);
  }
  if (el.sliderOptions) {
    el.sliderOptions.classList.toggle("hidden", !isSlider);
  }
  if (el.graphOptions) {
    el.graphOptions.classList.toggle("hidden", !isGraph);
  }
  if (el.heatingOptions) {
    el.heatingOptions.classList.toggle("hidden", !isHeating);
  }
  if (el.solarForecastFields) {
    el.solarForecastFields.classList.toggle("hidden", !isSolarForecast);
  }
  if (el.solarForecastStyleOptions) {
    el.solarForecastStyleOptions.classList.toggle("hidden", !isSolarForecast);
  }
  if (isButton) {
    const accent = normalizeHexColor(widget.button_accent_color, DEFAULT_BUTTON_ACCENT_COLOR);
    const buttonMode = normalizeButtonMode(widget.button_mode);
    widget.button_accent_color = accent;
    widget.button_mode = buttonMode;
    if (el.fButtonAccentColor) {
      el.fButtonAccentColor.value = accent;
    }
    if (el.fButtonMode) {
      el.fButtonMode.value = buttonMode;
    }
  } else {
    if (el.fButtonAccentColor) {
      el.fButtonAccentColor.value = DEFAULT_BUTTON_ACCENT_COLOR;
    }
    if (el.fButtonMode) {
      el.fButtonMode.value = DEFAULT_BUTTON_MODE;
    }
  }
  if (isSlider) {
    const sliderEntityDomain = normalizeSliderEntityDomain(widget.slider_entity_domain);
    const direction = normalizeSliderDirection(widget.slider_direction);
    const accent = normalizeHexColor(widget.slider_accent_color, DEFAULT_SLIDER_ACCENT_COLOR);
    widget.slider_entity_domain = sliderEntityDomain;
    widget.slider_direction = direction;
    widget.slider_accent_color = accent;
    if (el.fSliderEntityDomain) {
      el.fSliderEntityDomain.value = sliderEntityDomain;
    }
    if (el.fSliderDirection) {
      el.fSliderDirection.value = direction;
    }
    if (el.fSliderAccentColor) {
      el.fSliderAccentColor.value = accent;
    }
  } else {
    if (el.fSliderEntityDomain) {
      el.fSliderEntityDomain.value = DEFAULT_SLIDER_ENTITY_DOMAIN;
    }
    if (el.fSliderDirection) {
      el.fSliderDirection.value = DEFAULT_SLIDER_DIRECTION;
    }
    if (el.fSliderAccentColor) {
      el.fSliderAccentColor.value = DEFAULT_SLIDER_ACCENT_COLOR;
    }
  }
  if (isGraph) {
    const lineColor = normalizeHexColor(widget.graph_line_color, DEFAULT_GRAPH_LINE_COLOR);
    const timeWindowMin = normalizeGraphTimeWindowMin(widget.graph_time_window_min);
    const pointCount = normalizeGraphPointCount(widget.graph_point_count);
    const displayMode = normalizeGraphDisplayMode(widget.graph_display_mode);
    const barBucketMin = normalizeGraphBarBucketMin(widget.graph_bar_bucket_min);
    widget.graph_line_color = lineColor;
    widget.graph_time_window_min = timeWindowMin;
    widget.graph_display_mode = displayMode;
    widget.graph_bar_bucket_min = barBucketMin;
    if (pointCount > 0) {
      widget.graph_point_count = pointCount;
    } else {
      delete widget.graph_point_count;
    }
    if (el.fGraphLineColor) {
      el.fGraphLineColor.value = lineColor;
    }
    if (el.fGraphTimeWindowMin) {
      el.fGraphTimeWindowMin.value = String(widget.graph_time_window_min);
    }
    if (el.fGraphPointCount) {
      el.fGraphPointCount.value = pointCount > 0 ? String(pointCount) : "";
    }
    if (el.fGraphDisplayMode) {
      el.fGraphDisplayMode.value = displayMode;
    }
    if (el.fGraphBarBucketMin) {
      el.fGraphBarBucketMin.value = String(barBucketMin);
    }
    if (el.fGraphBarBucketMinWrap) {
      el.fGraphBarBucketMinWrap.classList.toggle("hidden", displayMode !== "bars");
    }
    if (el.fGraphPointCountWrap) {
      el.fGraphPointCountWrap.classList.toggle("hidden", displayMode !== "line");
    }
  } else {
    if (el.fGraphLineColor) {
      el.fGraphLineColor.value = DEFAULT_GRAPH_LINE_COLOR;
    }
    if (el.fGraphTimeWindowMin) {
      el.fGraphTimeWindowMin.value = String(DEFAULT_GRAPH_TIME_WINDOW_MIN);
    }
    if (el.fGraphPointCount) {
      el.fGraphPointCount.value = "";
    }
    if (el.fGraphDisplayMode) {
      el.fGraphDisplayMode.value = DEFAULT_GRAPH_DISPLAY_MODE;
    }
    if (el.fGraphBarBucketMin) {
      el.fGraphBarBucketMin.value = String(DEFAULT_GRAPH_BAR_BUCKET_MIN);
    }
    if (el.fGraphBarBucketMinWrap) {
      el.fGraphBarBucketMinWrap.classList.add("hidden");
    }
  }

  if (isHeating) {
    const styleVariant = widget.style_variant === "arc_semi" ? "arc_semi" : "default";
    const arcOpening = ["left", "right", "top", "bottom"].includes(widget.arc_opening) ? widget.arc_opening : "left";
    widget.style_variant = styleVariant;
    widget.arc_opening = arcOpening;
    if (el.fHeatingStyleVariant) {
      el.fHeatingStyleVariant.value = styleVariant;
    }
    if (el.fHeatingArcOpening) {
      el.fHeatingArcOpening.value = arcOpening;
    }
    if (el.fHeatingArcOpeningWrap) {
      el.fHeatingArcOpeningWrap.classList.toggle("hidden", styleVariant !== "arc_semi");
    }
  } else {
    if (el.fHeatingStyleVariant) {
      el.fHeatingStyleVariant.value = "default";
    }
    if (el.fHeatingArcOpening) {
      el.fHeatingArcOpening.value = "left";
    }
    if (el.fHeatingArcOpeningWrap) {
      el.fHeatingArcOpeningWrap.classList.add("hidden");
    }
  }

  renderEntityOptions();
}

function renderAll() {
  normalizeLayoutWidgets(editor.layout);
  renderPages();
  renderPageEditor();
  renderWidgets();
  renderInspector();
  renderCanvas();
}

function addPage() {
  const pageId = uniqueId("page", editor.layout.pages);
  const pageNumber = editor.layout.pages.length + 1;
  editor.layout.pages.push({
    id: pageId,
    title: t("layout.pages.new_title", { number: pageNumber }),
    widgets: [],
  });
  editor.selectedPageId = pageId;
  editor.selectedWidgetId = null;
  renderAll();
}

function addEnergyPage() {
  const pageId = uniqueId("energy", editor.layout.pages);
  editor.layout.pages.push({
    id: pageId,
    type: ENERGY_PAGE_TYPE,
    title: t("layout.pages.energy_title"),
    energy: defaultEnergyConfig(),
    widgets: [],
  });
  editor.selectedPageId = pageId;
  editor.selectedWidgetId = null;
  renderAll();
  void loadEnergyPreview();
}

function deletePage() {
  if (!editor.layout.pages.length || !editor.selectedPageId) return;
  if (editor.layout.pages.length === 1) {
    setStatus(t("layout.status.at_least_one_page"), true);
    return;
  }
  const page = editor.layout.pages.find((p) => p.id === editor.selectedPageId);
  if (!page) return;
  const name = page.title || page.id;
  if (!window.confirm(t("layout.pages.confirm_delete", { name }))) return;
  editor.layout.pages = editor.layout.pages.filter((p) => p.id !== editor.selectedPageId);
  editor.selectedPageId = editor.layout.pages[0].id;
  editor.selectedWidgetId = null;
  renderAll();
}

function applyPageName() {
  const page = selectedPage();
  if (!page) return;
  const nextTitle = el.pageTitleInput.value.trim();
  page.title = nextTitle || page.id;
  if (isEnergyPage(page)) {
    applyEnergyPageConfig({ render: false });
  }
  renderAll();
}

function applyEnergyPageConfig(options = {}) {
  const page = selectedPage();
  if (!isEnergyPage(page)) return false;
  normalizeEnergyConfig(page);
  const selectedSource = (el.energySource?.value || page.energy.source || ENERGY_SOURCE_HA).trim();
  page.energy.source = ENERGY_SOURCES.has(selectedSource) ? selectedSource : ENERGY_SOURCE_HA;
  const inputs = getEnergyInputs();
  for (const key of ENERGY_ENTITY_KEYS) {
    page.energy[key] = (inputs[key]?.value || "").trim();
  }
  updateEnergySourceUi(page.energy.source);
  if (options.render !== false) {
    renderAll();
  }
  if (page.energy.source === ENERGY_SOURCE_HA) {
    void loadEnergyPreview();
  }
  return true;
}

function addWidget(type, options = {}) {
  const page = selectedPage();
  if (!page) return;
  if (isEnergyPage(page)) {
    setStatus(t("layout.status.energy_page_only"), true);
    return null;
  }
  const sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN;
  const id = createWidgetIdForPage(page, type);
  const entityId = typeof options.entityId === "string"
    ? options.entityId
    : type === "solar_forecast"
      ? DEFAULT_SOLCAST_ENTITIES.remaining
      : pickDefaultEntityForWidgetType(type, sliderDomain);
  const secondaryEntityId = type === "heating_tile" ? pickDefaultEntityForWidgetType("sensor") : "";
  const compact = isCompactCanvas();
  const defaultW = compact
    ? type === "weather_3day" ? 420
      : type === "solar_forecast" ? 420
      : type === "todo_list" ? 300
      : type === "media_player" ? 300
      : type === "roborock_tile" ? 460
      : type === "weather_tile" ? 220
      : (type === "light_tile" || type === "empty_tile") ? 140
      : type === "heating_tile" ? 150
      : 180
    : type === "weather_3day" ? 360
      : type === "solar_forecast" ? 460
      : type === "todo_list" ? 360
      : type === "media_player" ? 360
      : type === "roborock_tile" ? 360
      : (type === "light_tile" || type === "heating_tile" || type === "weather_tile" || type === "empty_tile") ? 300
      : 220;
  const defaultH = compact
    ? type === "weather_3day" ? 240
      : type === "solar_forecast" ? 240
      : type === "todo_list" ? 220
      : type === "media_player" ? 220
      : type === "roborock_tile" ? 300
      : type === "weather_tile" ? 180
      : (type === "light_tile" || type === "empty_tile") ? 140
      : type === "heating_tile" ? 150
      : 110
    : type === "weather_3day" ? 260
      : type === "solar_forecast" ? 250
      : type === "todo_list" ? 360
      : type === "media_player" ? 280
      : type === "roborock_tile" ? 300
      : (type === "light_tile" || type === "heating_tile" || type === "weather_tile" || type === "empty_tile") ? 260
      : 120;
  const rect = clampRectToCanvas({ x: 20, y: 20, w: defaultW, h: defaultH }, type);

  const widget = {
    id,
    type,
    title: typeof options.title === "string" && options.title.trim() ? options.title.trim() : id,
    entity_id: entityId,
    secondary_entity_id: secondaryEntityId,
    rect,
  };
  if (type === "slider") {
    widget.slider_entity_domain = DEFAULT_SLIDER_ENTITY_DOMAIN;
    widget.slider_direction = DEFAULT_SLIDER_DIRECTION;
    widget.slider_accent_color = DEFAULT_SLIDER_ACCENT_COLOR;
  }
  if (type === "button") {
    widget.button_mode = DEFAULT_BUTTON_MODE;
    widget.button_accent_color = DEFAULT_BUTTON_ACCENT_COLOR;
  }
  if (type === "graph") {
    widget.graph_line_color = DEFAULT_GRAPH_LINE_COLOR;
    widget.graph_time_window_min = DEFAULT_GRAPH_TIME_WINDOW_MIN;
  }
  if (type === "solar_forecast") {
    widget.title = "Solar Forecast";
    widget.forecast_today_entity_id = DEFAULT_SOLCAST_ENTITIES.today;
    widget.forecast_tomorrow_entity_id = DEFAULT_SOLCAST_ENTITIES.tomorrow;
    widget.forecast_day_3_entity_id = DEFAULT_SOLCAST_ENTITIES.day3;
    widget.forecast_day_4_entity_id = DEFAULT_SOLCAST_ENTITIES.day4;
    widget.forecast_day_5_entity_id = DEFAULT_SOLCAST_ENTITIES.day5;
    widget.solar_forecast_bar_orientation = DEFAULT_SOLAR_FORECAST_BAR_ORIENTATION;
  }

  page.widgets.push(widget);
  editor.selectedWidgetId = id;
  renderAll();
  onSetupWizardWidgetAdded(widget);
  return widget;
}

function deleteWidget() {
  const page = selectedPage();
  if (!page || !editor.selectedWidgetId) return;
  const widget = page.widgets.find((w) => w.id === editor.selectedWidgetId);
  if (!widget) return;
  const name = widgetDisplayLabel(widget);
  if (!window.confirm(t("layout.widgets.confirm_delete", { name }))) return;
  page.widgets = page.widgets.filter((w) => w.id !== editor.selectedWidgetId);
  editor.selectedWidgetId = null;
  renderAll();
}

function renderInspectorChange(refreshInspector) {
  if (refreshInspector) {
    renderAll();
    return;
  }
  renderWidgets();
  renderCanvas();
}

function applyInspector(options = {}) {
  const widget = selectedWidget();
  if (!widget) return false;

  const refreshInspector = options.refreshInspector !== false;
  const softEntityValidation = options.softEntityValidation === true;

  const widgetType = widget.type;
  const sliderDomain = widgetType === "slider"
    ? normalizeSliderEntityDomain(el.fSliderEntityDomain?.value)
    : DEFAULT_SLIDER_ENTITY_DOMAIN;
  const buttonMode = widgetType === "button"
    ? normalizeButtonMode(el.fButtonMode?.value)
    : DEFAULT_BUTTON_MODE;
  const secondaryConfig = secondaryEntityConfigForWidgetType(widgetType);
  const nextEntityId = el.fEntity.value.trim() || pickDefaultEntityForWidgetType(widgetType, sliderDomain, buttonMode);

  const primaryEntityValid = entityMatchesWidgetType({ id: nextEntityId }, widgetType, sliderDomain, buttonMode);
  if (!primaryEntityValid && !softEntityValidation) {
    const allowedDomains = allowedEntityDomainsForWidgetType(widgetType, sliderDomain, buttonMode);
    const allowedHint = allowedDomains.length ? allowedDomains.join(", ") : t("layout.status.expected_domain");
    setStatus(t("layout.status.entity_domain_required", { domains: allowedHint }), true);
    return false;
  }

  widget.title = el.fTitle.value.trim();
  if (primaryEntityValid) {
    widget.entity_id = nextEntityId;
  }
  if (widgetType === "solar_forecast") {
    widget.forecast_today_entity_id = el.fSolarForecastToday?.value.trim() || DEFAULT_SOLCAST_ENTITIES.today;
    widget.forecast_tomorrow_entity_id = el.fSolarForecastTomorrow?.value.trim() || DEFAULT_SOLCAST_ENTITIES.tomorrow;
    widget.forecast_day_3_entity_id = el.fSolarForecastDay3?.value.trim() || DEFAULT_SOLCAST_ENTITIES.day3;
    widget.forecast_day_4_entity_id = el.fSolarForecastDay4?.value.trim() || DEFAULT_SOLCAST_ENTITIES.day4;
    widget.forecast_day_5_entity_id = el.fSolarForecastDay5?.value.trim() || DEFAULT_SOLCAST_ENTITIES.day5;
    widget.solar_forecast_bar_orientation = normalizeSolarForecastBarOrientation(el.fSolarForecastBarOrientation?.value);
    const barMax = Number(el.fSolarForecastBarMax?.value);
    if (Number.isFinite(barMax) && barMax > 0) {
      widget.solar_forecast_bar_max_kwh = barMax;
    } else {
      delete widget.solar_forecast_bar_max_kwh;
    }
    delete widget.secondary_entity_id;
  } else {
    delete widget.forecast_today_entity_id;
    delete widget.forecast_tomorrow_entity_id;
    delete widget.forecast_day_3_entity_id;
    delete widget.forecast_day_4_entity_id;
    delete widget.forecast_day_5_entity_id;
    delete widget.solar_forecast_bar_max_kwh;
    delete widget.solar_forecast_bar_orientation;
  }
  if (secondaryConfig.enabled) {
    const typedSecondaryEntityId = el.fSecondaryEntity.value.trim();
    const secondaryEntityId = secondaryConfig.optional
      ? typedSecondaryEntityId
      : (typedSecondaryEntityId || pickDefaultEntityForWidgetType(secondaryConfig.domain));
    if (secondaryEntityId.length > 0 && !secondaryEntityId.startsWith(`${secondaryConfig.domain}.`)) {
      if (!softEntityValidation) {
        setStatus(t(secondaryConfig.invalidStatusKey), true);
        return false;
      }
    } else {
      widget.secondary_entity_id = secondaryEntityId;
    }
  } else {
    widget.secondary_entity_id = "";
  }
  if (widgetType === "button") {
    widget.button_mode = buttonMode;
    widget.button_accent_color = normalizeHexColor(el.fButtonAccentColor?.value, DEFAULT_BUTTON_ACCENT_COLOR);
  } else {
    delete widget.button_mode;
    delete widget.button_accent_color;
  }
  if (widgetType === "slider") {
    widget.slider_entity_domain = sliderDomain;
    widget.slider_direction = normalizeSliderDirection(el.fSliderDirection?.value);
    widget.slider_accent_color = normalizeHexColor(el.fSliderAccentColor?.value, DEFAULT_SLIDER_ACCENT_COLOR);
  } else {
    delete widget.slider_entity_domain;
    delete widget.slider_direction;
    delete widget.slider_accent_color;
  }
  if (widgetType === "graph") {
    widget.graph_line_color = normalizeHexColor(el.fGraphLineColor?.value, DEFAULT_GRAPH_LINE_COLOR);
    widget.graph_time_window_min = normalizeGraphTimeWindowMin(el.fGraphTimeWindowMin?.value);
    widget.graph_display_mode = normalizeGraphDisplayMode(el.fGraphDisplayMode?.value);
    widget.graph_bar_bucket_min = normalizeGraphBarBucketMin(el.fGraphBarBucketMin?.value);
    const graphPointCount = normalizeGraphPointCount(el.fGraphPointCount?.value);
    if (graphPointCount > 0) {
      widget.graph_point_count = graphPointCount;
    } else {
      delete widget.graph_point_count;
    }
  } else {
    delete widget.graph_line_color;
    delete widget.graph_time_window_min;
    delete widget.graph_point_count;
    delete widget.graph_display_mode;
    delete widget.graph_bar_bucket_min;
  }
  if (widgetType === "heating_tile") {
    const variant = el.fHeatingStyleVariant?.value === "arc_semi" ? "arc_semi" : "default";
    widget.style_variant = variant;
    if (variant === "arc_semi") {
      const opening = el.fHeatingArcOpening?.value;
      widget.arc_opening = ["left", "right", "top", "bottom"].includes(opening) ? opening : "left";
    } else {
      delete widget.arc_opening;
    }
  } else {
    delete widget.style_variant;
    delete widget.arc_opening;
  }
  widget.rect = clampRectToCanvas(
    {
      x: Number(el.fX.value || 0),
      y: Number(el.fY.value || 0),
      w: Number(el.fW.value || widget.rect.w),
      h: Number(el.fH.value || widget.rect.h),
    },
    widgetType
  );
  editor.selectedWidgetId = widget.id;
  renderInspectorChange(refreshInspector);
  return true;
}

function autoApplyInspector(options = {}) {
  return applyInspector({
    refreshInspector: false,
    ...options,
  });
}

function bindInspectorAutoApply(input, events = ["change"], options = {}) {
  if (!input) return;
  const handler = () => autoApplyInspector(options);
  for (const eventName of events) {
    input.addEventListener(eventName, handler);
  }
}

async function saveLayout() {
  if (isEnergyPage(selectedPage())) {
    applyEnergyPageConfig({ render: false });
  }
  normalizeLayoutWidgets(editor.layout);
  setStatus(t("layout.status.saving"));
  const response = await fetch("/api/layout", {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(editor.layout),
  });
  if (!response.ok) {
    let detail = await response.text();
    try {
      const json = JSON.parse(detail);
      detail = (json.errors || []).join(", ") || detail;
    } catch (_) {}
    throw new Error(detail);
  }
  setStatus(t("layout.status.saved"));
}

function exportLayout() {
  if (isEnergyPage(selectedPage())) {
    applyEnergyPageConfig({ render: false });
  }
  normalizeLayoutWidgets(editor.layout);
  const blob = new Blob([JSON.stringify(editor.layout, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = "betta-layout.json";
  a.click();
  URL.revokeObjectURL(url);
}

function importLayoutFromText(text) {
  const parsed = JSON.parse(text);
  if (!parsed || !Array.isArray(parsed.pages)) {
    throw new Error(t("layout.status.invalid_json"));
  }
  normalizeLayoutWidgets(parsed);
  editor.layout = parsed;
  editor.selectedPageId = parsed.pages[0]?.id || null;
  editor.selectedWidgetId = null;
  renderAll();
  setStatus(t("layout.status.imported"));
}

function bindUi() {
  setupSettingsWorkspace();
  for (const button of el.settingsNavButtons || []) {
    button.onclick = () => setActiveSettingsSection(button.dataset.settingsSection);
  }
  if (el.toggleWidgetsSection) {
    el.toggleWidgetsSection.onclick = () => toggleSection("widgets");
  }
  if (el.toggleInspectorSection) {
    el.toggleInspectorSection.onclick = () => toggleSection("inspector");
  }
  applySectionCollapseState();

  el.layoutTabBtn.onclick = () => setActivePane("layout");
  el.settingsTabBtn.onclick = async () => {
    setActivePane("settings");
    await loadSettings(true);
    await loadOtaStatus(true);
  };
  el.addPageBtn.onclick = addPage;
  if (el.addEnergyPageBtn) {
    el.addEnergyPageBtn.onclick = addEnergyPage;
  }
  el.deletePageBtn.onclick = deletePage;
  el.applyPageBtn.onclick = applyPageName;
  initSimpleUiMenus();
  if (el.applyEnergyPageBtn) {
    el.applyEnergyPageBtn.onclick = () => applyEnergyPageConfig();
  }
  if (el.energySource) {
    el.energySource.onchange = () => applyEnergyPageConfig();
  }
  for (const input of Object.values(getEnergyInputs())) {
    if (!input) continue;
    input.onchange = () => applyEnergyPageConfig();
    input.onblur = () => applyEnergyPageConfig();
  }
  el.addSensorBtn.onclick = () => openLightEntityPicker("sensor");
  el.addButtonBtn.onclick = () => openLightEntityPicker("button");
  el.addSliderBtn.onclick = () => addWidget("slider");
  el.addGraphBtn.onclick = () => openLightEntityPicker("graph");
  el.addEmptyTileBtn.onclick = () => addWidget("empty_tile");
  el.addLightTileBtn.onclick = () => openLightEntityPicker("light_tile");
  if (el.openSetupWizardBtn) {
    el.openSetupWizardBtn.onclick = () => openSetupWizard({ manual: true });
  }
  el.addHeatingTileBtn.onclick = () => openLightEntityPicker("heating_tile");
  el.addWeatherTileBtn.onclick = () => openLightEntityPicker("weather_tile");
  el.addWeather3DayBtn.onclick = () => openLightEntityPicker("weather_3day");
  if (el.addSolarForecastBtn) {
    el.addSolarForecastBtn.onclick = () => addWidget("solar_forecast");
  }
  if (el.addTodoListBtn) {
    el.addTodoListBtn.onclick = () => openLightEntityPicker("todo_list");
  }
  if (el.addMediaPlayerBtn) {
    el.addMediaPlayerBtn.onclick = () => openLightEntityPicker("media_player");
  }
  if (el.addRoborockTileBtn) {
    el.addRoborockTileBtn.onclick = () => openLightEntityPicker("roborock_tile");
  }
  if (el.lightEntityPickerRefreshBtn) {
    el.lightEntityPickerRefreshBtn.onclick = () => {
      const config = entityPickerConfig();
      const cacheKey = entityPickerCacheKey(config.domain);
      editor.lightPicker.hasLoaded = false;
      editor.lightPicker.loadedByDomain[cacheKey] = false;
      clearLightEntityPickerPoll();
      clearLightEntityPickerSearchDebounce();
      void fetchLightEntityPicker({ refresh: true });
    };
  }
  if (el.lightEntityPickerSearch) {
    el.lightEntityPickerSearch.oninput = () => {
      const config = entityPickerConfig();
      const search = el.lightEntityPickerSearch.value || "";
      editor.lightPicker.search = search;
      editor.lightPicker.searchByDomain[config.domain] = search;
      const cacheKey = entityPickerCacheKey(config.domain, search);
      editor.lightPicker.items = editor.lightPicker.itemsByDomain[cacheKey] || [];
      editor.lightPicker.hasLoaded = editor.lightPicker.loadedByDomain[cacheKey] === true;
      clearLightEntityPickerPoll();
      clearLightEntityPickerSearchDebounce();
      const shouldAutoFetch =
        !editor.lightPicker.hasLoaded && entityPickerSearchReady(config) && entityPickerLiveSearchEnabled(config);
      editor.lightPicker.requestSeq += 1;
      editor.lightPicker.loading = shouldAutoFetch;
      renderLightEntityPicker({
        status: editor.lightPicker.hasLoaded ? "ready" : (shouldAutoFetch ? "pending" : "idle"),
        pending: shouldAutoFetch,
        items: editor.lightPicker.items,
      });
      if (shouldAutoFetch) {
        editor.lightPicker.searchDebounceId = window.setTimeout(() => {
          editor.lightPicker.searchDebounceId = null;
          void fetchLightEntityPicker({ refresh: true });
        }, ENTITY_PICKER_SEARCH_DEBOUNCE_MS);
      }
    };
    el.lightEntityPickerSearch.onkeydown = (event) => {
      if (event.key !== "Enter") return;
      event.preventDefault();
      const config = entityPickerConfig();
      const cacheKey = entityPickerCacheKey(config.domain);
      editor.lightPicker.hasLoaded = false;
      editor.lightPicker.loadedByDomain[cacheKey] = false;
      clearLightEntityPickerPoll();
      clearLightEntityPickerSearchDebounce();
      void fetchLightEntityPicker({ refresh: true });
    };
  }
  if (el.lightEntityPickerCloseBtn) {
    el.lightEntityPickerCloseBtn.onclick = closeLightEntityPicker;
  }
  if (el.lightEntityPickerBlankBtn) {
    el.lightEntityPickerBlankBtn.onclick = () => {
      addWidget(editor.lightPicker.widgetType || "light_tile");
      closeLightEntityPicker();
    };
  }
  if (el.lightEntityPickerOverlay) {
    el.lightEntityPickerOverlay.addEventListener("click", (event) => {
      if (event.target === el.lightEntityPickerOverlay) {
        closeLightEntityPicker();
      }
    });
  }
  if (el.setupWizardAddLightBtn) {
    el.setupWizardAddLightBtn.onclick = () => openSetupWizardEntityPicker("light_tile");
  }
  if (el.setupWizardAddHeatingBtn) {
    el.setupWizardAddHeatingBtn.onclick = () => openSetupWizardEntityPicker("heating_tile");
  }
  if (el.setupWizardAddWeatherBtn) {
    el.setupWizardAddWeatherBtn.onclick = () => openSetupWizardEntityPicker("weather_tile");
  }
  if (el.setupWizardAddButtonBtn) {
    el.setupWizardAddButtonBtn.onclick = () => openSetupWizardEntityPicker("button");
  }
  if (el.setupWizardAddSensorBtn) {
    el.setupWizardAddSensorBtn.onclick = () => openSetupWizardEntityPicker("sensor");
  }
  if (el.setupWizardPageTitle) {
    el.setupWizardPageTitle.onchange = applySetupWizardPageTitle;
    el.setupWizardPageTitle.onblur = applySetupWizardPageTitle;
  }
  if (el.setupWizardCloseBtn) {
    el.setupWizardCloseBtn.onclick = () => closeSetupWizard(true);
  }
  if (el.setupWizardSkipBtn) {
    el.setupWizardSkipBtn.onclick = () => closeSetupWizard(true);
  }
  if (el.setupWizardDoneBtn) {
    el.setupWizardDoneBtn.onclick = () => {
      void saveSetupWizardLayout({ closeOnSuccess: true });
    };
  }
  if (el.setupWizardSaveBtn) {
    el.setupWizardSaveBtn.onclick = () => {
      void saveSetupWizardLayout();
    };
  }
  if (el.setupWizardOverlay) {
    el.setupWizardOverlay.addEventListener("click", (event) => {
      if (event.target === el.setupWizardOverlay) {
        closeSetupWizard(true);
      }
    });
  }
  el.deleteWidgetBtn.onclick = deleteWidget;
  if (el.applyInspectorBtn) {
    el.applyInspectorBtn.onclick = () => applyInspector();
  }
  el.reloadBtn.onclick = () => loadLayout();
  el.fType.onchange = () => {
    const sliderDomain = normalizeSliderEntityDomain(el.fSliderEntityDomain?.value);
    const buttonMode = normalizeButtonMode(el.fButtonMode?.value);
    if (el.buttonOptions) {
      el.buttonOptions.classList.toggle("hidden", el.fType.value !== "button");
    }
    if (el.sliderOptions) {
      el.sliderOptions.classList.toggle("hidden", el.fType.value !== "slider");
    }
    if (el.graphOptions) {
      el.graphOptions.classList.toggle("hidden", el.fType.value !== "graph");
    }
    if (el.fType.value === "button") {
      if (el.fButtonMode) {
        el.fButtonMode.value = buttonMode;
      }
      if (el.fButtonAccentColor) {
        el.fButtonAccentColor.value = normalizeHexColor(el.fButtonAccentColor.value, DEFAULT_BUTTON_ACCENT_COLOR);
      }
    } else {
      if (el.fButtonMode) {
        el.fButtonMode.value = DEFAULT_BUTTON_MODE;
      }
      if (el.fButtonAccentColor) {
        el.fButtonAccentColor.value = DEFAULT_BUTTON_ACCENT_COLOR;
      }
    }
    if (el.fType.value === "slider") {
      if (el.fSliderEntityDomain) {
        el.fSliderEntityDomain.value = sliderDomain;
      }
      if (el.fSliderDirection) {
        el.fSliderDirection.value = normalizeSliderDirection(el.fSliderDirection.value);
      }
      if (el.fSliderAccentColor) {
        el.fSliderAccentColor.value = normalizeHexColor(el.fSliderAccentColor.value, DEFAULT_SLIDER_ACCENT_COLOR);
      }
    }
    if (el.fType.value === "graph") {
      if (el.fGraphLineColor) {
        el.fGraphLineColor.value = normalizeHexColor(el.fGraphLineColor.value, DEFAULT_GRAPH_LINE_COLOR);
      }
      if (el.fGraphTimeWindowMin) {
        el.fGraphTimeWindowMin.value = String(normalizeGraphTimeWindowMin(el.fGraphTimeWindowMin.value));
      }
      if (el.fGraphPointCount) {
        const normalizedGraphPoints = normalizeGraphPointCount(el.fGraphPointCount.value);
        el.fGraphPointCount.value = normalizedGraphPoints > 0 ? String(normalizedGraphPoints) : "";
      }
      if (el.fGraphDisplayMode) {
        el.fGraphDisplayMode.value = normalizeGraphDisplayMode(el.fGraphDisplayMode.value);
      }
      if (el.fGraphBarBucketMin) {
        el.fGraphBarBucketMin.value = String(normalizeGraphBarBucketMin(el.fGraphBarBucketMin.value));
      }
    } else {
      if (el.fGraphLineColor) {
        el.fGraphLineColor.value = DEFAULT_GRAPH_LINE_COLOR;
      }
      if (el.fGraphTimeWindowMin) {
        el.fGraphTimeWindowMin.value = String(DEFAULT_GRAPH_TIME_WINDOW_MIN);
      }
      if (el.fGraphPointCount) {
        el.fGraphPointCount.value = "";
      }
      if (el.fGraphDisplayMode) {
        el.fGraphDisplayMode.value = DEFAULT_GRAPH_DISPLAY_MODE;
      }
      if (el.fGraphBarBucketMin) {
        el.fGraphBarBucketMin.value = String(DEFAULT_GRAPH_BAR_BUCKET_MIN);
      }
    }
    renderEntityOptions();
    const currentEntity = el.fEntity.value.trim();
    const effectiveButtonMode = el.fType.value === "button"
      ? normalizeButtonMode(el.fButtonMode?.value)
      : DEFAULT_BUTTON_MODE;
    if (!entityMatchesWidgetType({ id: currentEntity }, el.fType.value, sliderDomain, effectiveButtonMode)) {
      el.fEntity.value = pickDefaultEntityForWidgetType(el.fType.value, sliderDomain, effectiveButtonMode);
    }
    if (el.fType.value === "heating_tile") {
      const sensorEntity = el.fSecondaryEntity.value.trim();
      if (!sensorEntity.startsWith("sensor.")) {
        el.fSecondaryEntity.value = pickDefaultEntityForWidgetType("sensor");
      }
    } else {
      el.fSecondaryEntity.value = "";
    }
  };
  if (el.fEntity) {
    el.fEntity.oninput = () => scheduleEntityAutocomplete("primary");
    el.fEntity.onfocus = () => scheduleEntityAutocomplete("primary", true);
    el.fEntity.onchange = () => autoApplyInspector();
    el.fEntity.onblur = () => autoApplyInspector();
  }
  if (el.fButtonMode) {
    el.fButtonMode.onchange = () => {
      el.fButtonMode.value = normalizeButtonMode(el.fButtonMode.value);
      if (inspectorWidgetType() !== "button") return;
      renderEntityOptions();
      scheduleEntityAutocomplete("primary", true);
      const currentEntity = el.fEntity.value.trim();
      const buttonMode = inspectorButtonMode();
      if (!entityMatchesWidgetType({ id: currentEntity }, "button", DEFAULT_SLIDER_ENTITY_DOMAIN, buttonMode)) {
        el.fEntity.value = pickDefaultEntityForWidgetType("button", DEFAULT_SLIDER_ENTITY_DOMAIN, buttonMode);
      }
      autoApplyInspector();
    };
  }
  if (el.fSliderEntityDomain) {
    el.fSliderEntityDomain.onchange = () => {
      el.fSliderEntityDomain.value = normalizeSliderEntityDomain(el.fSliderEntityDomain.value);
      if (inspectorWidgetType() !== "slider") return;
      scheduleEntityAutocomplete("primary", true);
      const currentEntity = el.fEntity.value.trim();
      const sliderDomain = inspectorSliderEntityDomain();
      if (!entityMatchesWidgetType({ id: currentEntity }, "slider", sliderDomain)) {
        el.fEntity.value = pickDefaultEntityForWidgetType("slider", sliderDomain);
      }
      autoApplyInspector();
    };
  }
  if (el.fSecondaryEntity) {
    el.fSecondaryEntity.oninput = () => scheduleEntityAutocomplete("secondary");
    el.fSecondaryEntity.onfocus = () => scheduleEntityAutocomplete("secondary", true);
    el.fSecondaryEntity.onchange = () => autoApplyInspector();
    el.fSecondaryEntity.onblur = () => autoApplyInspector();
  }
  document.querySelectorAll("[data-inspector-entity-target]").forEach((button) => {
    button.addEventListener("click", () => {
      openInspectorEntityPicker(button.dataset.inspectorEntityTarget || "");
    });
  });
  bindInspectorAutoApply(el.fTitle, ["input"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fButtonAccentColor, ["input", "change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSliderDirection, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSliderAccentColor, ["input", "change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGraphLineColor, ["input", "change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGraphTimeWindowMin, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fGraphPointCount, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fGraphDisplayMode, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fGraphBarBucketMin, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fHeatingStyleVariant, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fHeatingArcOpening, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastToday, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastTomorrow, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastDay3, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastDay4, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastDay5, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastBarMax, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastBarOrientation, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fX, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fY, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fW, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fH, ["change"], { refreshInspector: true, softEntityValidation: true });
  el.reloadSettingsBtn.onclick = async () => {
    await loadSettings();
    await loadOtaStatus(true);
  };
  if (el.startOtaUrlBtn) {
    el.startOtaUrlBtn.onclick = () => {
      void startOtaFromUrl();
    };
  }
  if (el.refreshOtaStatusBtn) {
    el.refreshOtaStatusBtn.onclick = () => {
      void loadOtaStatus();
    };
  }
  if (el.uploadOtaBtn) {
    el.uploadOtaBtn.onclick = () => {
      void uploadOtaFile();
    };
  }
  el.scanWifiBtn.onclick = () => scanWifiNetworks("settings");
  el.settingsWifiScanResults.onchange = () => {
    const option = el.settingsWifiScanResults.selectedOptions?.[0];
    const ssid = option?.dataset?.ssid || "";
    if (ssid) {
      el.settingsWifiSsid.value = ssid;
    }
    const bssid = normalizeBssid(option?.dataset?.bssid || "");
    if (bssid && el.settingsWifiBssid) {
      el.settingsWifiBssid.value = bssid;
    }
  };
  if (el.settingsWifiCountryCode) {
    el.settingsWifiCountryCode.oninput = () => {
      const cleaned = (el.settingsWifiCountryCode.value || "")
        .toUpperCase()
        .replace(/[^A-Z]/g, "")
        .slice(0, 2);
      el.settingsWifiCountryCode.value = cleaned;
    };
  }
  if (el.settingsWifiBssid) {
    el.settingsWifiBssid.oninput = () => {
      const cleaned = (el.settingsWifiBssid.value || "")
        .toUpperCase()
        .replace(/[^0-9A-F:-]/g, "")
        .slice(0, 17);
      el.settingsWifiBssid.value = cleaned;
    };
  }
  if (el.settingsTopbarWeatherEntity) {
    el.settingsTopbarWeatherEntity.oninput = () => scheduleSettingsWeatherAutocomplete();
    el.settingsTopbarWeatherEntity.onfocus = () => scheduleSettingsWeatherAutocomplete(true);
  }
  for (const input of [el.settingsStockEntity1, el.settingsStockEntity2, el.settingsStockEntity3]) {
    if (!input) continue;
    input.oninput = () => scheduleSettingsStockAutocomplete(input);
    input.onfocus = () => scheduleSettingsStockAutocomplete(input, true);
  }
  if (el.settingsHardwareActiveBrightness) {
    el.settingsHardwareActiveBrightness.oninput = () => {
      setPercentSlider(
        el.settingsHardwareActiveBrightness,
        el.settingsHardwareActiveBrightnessValue,
        el.settingsHardwareActiveBrightness.value,
      );
    };
  }
  if (el.settingsHardwareDimBrightness) {
    el.settingsHardwareDimBrightness.oninput = () => {
      setPercentSlider(
        el.settingsHardwareDimBrightness,
        el.settingsHardwareDimBrightnessValue,
        el.settingsHardwareDimBrightness.value,
      );
    };
  }
  if (el.uploadLanguageCode) {
    el.uploadLanguageCode.oninput = () => {
      const cleaned = (el.uploadLanguageCode.value || "")
        .toLowerCase()
        .replace(/[^a-z0-9_-]/g, "")
        .slice(0, 15);
      el.uploadLanguageCode.value = cleaned;
    };
  }
  if (el.settingsLanguage) {
    el.settingsLanguage.onchange = async () => {
      const lang = normalizeUiLanguage(el.settingsLanguage.value);
      if (!editor.settings) editor.settings = {};
      if (!editor.settings.ui) editor.settings.ui = {};
      editor.settings.ui.language = lang;
      await loadI18nLanguage(lang);
      if (el.uploadLanguageCode) {
        el.uploadLanguageCode.value = lang;
      }
      renderSettings();
    };
  }
  if (el.reloadLanguagesBtn) {
    el.reloadLanguagesBtn.onclick = async () => {
      try {
        await loadLanguageCatalog();
        renderLanguageOptions();
        if (el.settingsTranslationInfo) {
          el.settingsTranslationInfo.textContent = t("settings.translation.info");
        }
      } catch (err) {
        if (el.settingsTranslationInfo) {
          el.settingsTranslationInfo.textContent = t("settings.translation.upload_fail", { error: err.message });
          el.settingsTranslationInfo.classList.add("error");
        }
      }
    };
  }
  if (el.downloadLanguageBtn) {
    el.downloadLanguageBtn.onclick = async () => {
      const lang = normalizeUiLanguage(el.settingsLanguage?.value || editor.i18nLanguage);
      await loadI18nLanguage(lang);
      downloadLanguageJson();
    };
  }
  if (el.uploadLanguageBtn) {
    el.uploadLanguageBtn.onclick = async () => {
      if (el.settingsTranslationInfo) {
        el.settingsTranslationInfo.classList.remove("error");
      }
      try {
        const targetLang = normalizeLanguageCode(el.uploadLanguageCode?.value, "");
        await uploadLanguageJson();
        if (el.settingsTranslationInfo) {
          el.settingsTranslationInfo.textContent = t("settings.translation.upload_ok", { lang: targetLang });
          el.settingsTranslationInfo.classList.remove("error");
        }
        if (el.uploadLanguageFile) {
          el.uploadLanguageFile.value = "";
        }
        await loadI18nLanguage(targetLang || editor.i18nLanguage, true);
        renderSettings();
      } catch (err) {
        if (el.settingsTranslationInfo) {
          el.settingsTranslationInfo.textContent = t("settings.translation.upload_fail", { error: err.message });
          el.settingsTranslationInfo.classList.add("error");
        }
      }
    };
  }
  if (el.provScanWifiBtn) {
    el.provScanWifiBtn.onclick = () => scanWifiNetworks("provisioning");
  }
  if (el.provWifiScanResults) {
    el.provWifiScanResults.onchange = () => {
      const option = el.provWifiScanResults.selectedOptions?.[0];
      const ssid = option?.dataset?.ssid || "";
      if (ssid && el.provWifiSsid) {
        el.provWifiSsid.value = ssid;
      }
    };
  }
  if (el.provWifiCountryCode) {
    el.provWifiCountryCode.oninput = () => {
      const cleaned = (el.provWifiCountryCode.value || "")
        .toUpperCase()
        .replace(/[^A-Z]/g, "")
        .slice(0, 2);
      el.provWifiCountryCode.value = cleaned;
    };
  }
  if (el.provWifiShowPassword && el.provWifiPassword) {
    el.provWifiShowPassword.onchange = () => {
      el.provWifiPassword.type = el.provWifiShowPassword.checked ? "text" : "password";
    };
  }
  if (el.provHaShowToken && el.provHaToken) {
    el.provHaShowToken.onchange = () => {
      el.provHaToken.type = el.provHaShowToken.checked ? "text" : "password";
    };
  }
  if (el.provWifiSaveBtn) {
    el.provWifiSaveBtn.onclick = async () => {
      try {
        await saveWifiProvisioning();
      } catch (err) {
        setProvisioningInfo("wifi", t("provision.save_failed", { error: err.message }), true);
      }
    };
  }
  if (el.provHaSaveBtn) {
    el.provHaSaveBtn.onclick = async () => {
      try {
        await saveHaProvisioning();
      } catch (err) {
        setProvisioningInfo("ha", t("provision.save_failed", { error: err.message }), true);
      }
    };
  }
  el.saveSettingsBtn.onclick = async () => {
    try {
      await saveSettings();
    } catch (err) {
      setStatus(t("status.settings_save_failed", { error: err.message }), true);
    }
  };
  el.saveBtn.onclick = async () => {
    try {
      await saveLayout();
    } catch (err) {
      setStatus(t("layout.status.save_failed", { error: err.message }), true);
    }
  };
  el.exportBtn.onclick = exportLayout;
  el.importBtn.onclick = () => {
    const text = el.jsonPaste.value.trim();
    if (!text) return;
    try {
      importLayoutFromText(text);
    } catch (err) {
      setStatus(t("layout.status.import_failed", { error: err.message }), true);
    }
  };
  el.importFile.onchange = async (event) => {
    const file = event.target.files?.[0];
    if (!file) return;
    const text = await file.text();
    try {
      importLayoutFromText(text);
    } catch (err) {
      setStatus(t("layout.status.file_import_failed", { error: err.message }), true);
    }
  };
}

async function startEditor() {
  if (editor.editorStarted) return;
  editor.editorStarted = true;
  setProvisioningVisible(false);
  setActivePane("layout");
  await Promise.all([loadLayout(), loadEntities(), refreshStates()]);
  await loadEnergyPreview();
  await loadHaDiagnostics();
  if (setupWizardShouldAutoOpen()) {
    openSetupWizard();
  }
  /* Poll diagnostics again shortly after startup so the banner appears automatically
   * once the ha_client watchdog has classified the missing entities (typically ~5-8 s
   * after WebSocket auth). Further refreshes are less frequent. */
  window.setTimeout(loadHaDiagnostics, 3000);
  window.setTimeout(loadHaDiagnostics, 8000);
  window.setTimeout(loadHaDiagnostics, 15000);
  window.setInterval(refreshStates, 5000);
  window.setInterval(loadEnergyPreview, 15000);
  window.setInterval(loadHaDiagnostics, 30000);
}

async function bootstrap() {
  bindUi();
  initThemeSection();
  setStatus(t("status.idle"));
  await loadAppVersion();
  const settings = await loadSettings(true);
  const stage = provisioningStageForSettings(settings);
  if (stage) {
    showProvisioningStage(stage, settings);
    return;
  }
  await startEditor();
}


// ============================================================
// Theme management (appended)
// ============================================================
const themeState = {
  list: [],
  activeId: "",
  editing: null,
  baseId: "",
};

function themeEl(id) {
  return document.getElementById(id);
}

function themeSetInfo(msg, isError) {
  const info = themeEl("settingsThemeInfo");
  if (info) {
    info.textContent = msg || "";
    info.style.color = isError ? "#ff6b6b" : "";
  }
}

async function themeFetchList() {
  const resp = await fetch("/api/themes");
  if (!resp.ok) throw new Error("themes list failed");
  const data = await resp.json();
  if (Array.isArray(data)) {
    return { themes: data, active_id: "" };
  }
  return {
    themes: Array.isArray(data && data.themes) ? data.themes : [],
    active_id: (data && data.active_id) || "",
  };
}

async function themeFetchActive() {
  const resp = await fetch("/api/themes/active");
  if (!resp.ok) throw new Error("themes active failed");
  return resp.json();
}

async function themeFetchById(id) {
  const resp = await fetch("/api/themes/get?id=" + encodeURIComponent(id));
  if (!resp.ok) throw new Error("theme get failed");
  return resp.json();
}

function themePopulateSelect() {
  const sel = themeEl("settingsThemeSelect");
  if (!sel) return;
  const prev = sel.value;
  sel.innerHTML = "";
  for (const entry of themeState.list) {
    const opt = document.createElement("option");
    opt.value = entry.id;
    opt.textContent = (entry.builtin ? "[built-in] " : "[custom] ") + (entry.name || entry.id);
    sel.appendChild(opt);
  }
  if (themeState.activeId) {
    sel.value = themeState.activeId;
  } else if (prev) {
    sel.value = prev;
  }
}

function themeSnapTo565(hex) {
  if (typeof hex !== "string") return hex;
  const m = /^#?([0-9a-fA-F]{6})$/.exec(hex.trim());
  if (!m) return hex;
  const v = parseInt(m[1], 16);
  let r = (v >> 16) & 0xff;
  let g = (v >> 8) & 0xff;
  let b = v & 0xff;
  const r5 = Math.round((r * 31) / 255);
  const g6 = Math.round((g * 63) / 255);
  const b5 = Math.round((b * 31) / 255);
  r = (r5 << 3) | (r5 >> 2);
  g = (g6 << 2) | (g6 >> 4);
  b = (b5 << 3) | (b5 >> 2);
  return "#" + ((r << 16) | (g << 8) | b).toString(16).padStart(6, "0");
}

const THEME_COLOR_GROUPS = [
  {
    id: "screen",
    label: "Screen & Content",
    keys: ["screen_bg", "screen_bg_grad", "content_bg", "content_border"],
  },
  {
    id: "topbar",
    label: "Top Bar",
    keys: [
      "topbar_bg",
      "topbar_border",
      "topbar_text",
      "topbar_muted",
      "topbar_chip_bg",
      "topbar_chip_border",
      "topbar_status_on",
      "topbar_status_off",
    ],
  },
  {
    id: "text",
    label: "Text",
    keys: ["text_primary", "text_soft", "text_muted"],
  },
  {
    id: "nav",
    label: "Navigation",
    keys: [
      "nav_bg",
      "nav_border",
      "nav_btn_bg_idle",
      "nav_btn_bg_active",
      "nav_tab_idle",
      "nav_tab_active",
      "nav_home_idle",
      "nav_home_active",
    ],
  },
  {
    id: "status",
    label: "Status & Connectivity",
    keys: ["ok", "error", "wifi_off"],
  },
  {
    id: "cards",
    label: "Cards (generic tiles)",
    keys: [
      "card_bg_off",
      "card_bg_on",
      "card_border",
      "card_icon_off",
      "card_icon_on",
      "state_on",
      "state_off",
    ],
  },
  {
    id: "light",
    label: "Light Tiles",
    keys: [
      "light_icon_on",
      "light_track_on",
      "light_track_off",
      "light_ind_on",
      "light_ind_off",
      "light_knob_on",
      "light_knob_off",
    ],
  },
  {
    id: "heat",
    label: "Heating Tiles",
    keys: [
      "heat_icon_on",
      "heat_track_on",
      "heat_track_off",
      "heat_ind_on",
      "heat_ind_off",
      "heat_knob_on",
      "heat_knob_off",
    ],
  },
  {
    id: "weather",
    label: "Weather",
    keys: ["weather_icon"],
  },
];

function themeHumanizeKey(key) {
  if (!key) return "";
  return key
    .replace(/_/g, " ")
    .replace(/\bbg\b/gi, "background")
    .replace(/\bind\b/gi, "indicator")
    .replace(/\bbtn\b/gi, "button")
    .replace(/\btxt\b/gi, "text")
    .replace(/\b\w/g, (c) => c.toUpperCase());
}

function themeRenderColorGrid(palette) {
  const grid = themeEl("settingsThemeColors");
  if (!grid) return;
  grid.innerHTML = "";
  const colors = (palette && palette.colors) || {};
  const seen = new Set();

  const renderInput = (container, key) => {
    if (!(key in colors)) return;
    seen.add(key);
    const lbl = document.createElement("label");
    const span = document.createElement("span");
    span.textContent = themeHumanizeKey(key);
    span.title = key;
    const input = document.createElement("input");
    input.type = "color";
    input.dataset.key = key;
    const snapped = themeSnapTo565(colors[key]);
    input.value = snapped;
    if (themeState.editing && themeState.editing.colors) {
      themeState.editing.colors[key] = snapped;
    }
    input.addEventListener("input", () => {
      const snappedLive = themeSnapTo565(input.value);
      if (snappedLive !== input.value) {
        input.value = snappedLive;
      }
      themeState.editing.colors[key] = snappedLive;
      themeRenderPreview();
    });
    lbl.appendChild(span);
    lbl.appendChild(input);
    container.appendChild(lbl);
  };

  const appendGroup = (label, keys, { open = true } = {}) => {
    const available = keys.filter((k) => k in colors && !seen.has(k));
    if (available.length === 0) return;
    const group = document.createElement("details");
    group.className = "theme-color-group";
    if (open) group.open = true;
    const summary = document.createElement("summary");
    summary.textContent = label;
    group.appendChild(summary);
    const body = document.createElement("div");
    body.className = "theme-color-group-body";
    for (const key of available) renderInput(body, key);
    group.appendChild(body);
    grid.appendChild(group);
  };

  for (const g of THEME_COLOR_GROUPS) {
    appendGroup(g.label, g.keys, { open: true });
  }

  // Any keys not covered by a known group — render as "Other" collapsed.
  const leftover = Object.keys(colors)
    .filter((k) => !seen.has(k))
    .sort();
  if (leftover.length > 0) {
    appendGroup("Other", leftover, { open: false });
  }
}

function themeRenderPreview() {
  const canvas = themeEl("settingsThemePreviewCanvas");
  if (!canvas || !themeState.editing) return;
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  const c = themeState.editing.colors || {};
  const bg = c.screen_bg || "#121212";
  const cardOff = c.card_bg_off || "#2a2a2a";
  const cardOn = c.card_bg_on || "#3a3a3a";
  const text = c.text_primary || "#ffffff";
  const soft = c.text_soft || "#cccccc";
  const accent = c.nav_tab_active || "#6fe8ff";
  const heatInd = c.heat_ind_on || accent;

  ctx.fillStyle = bg;
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  // Tile 1 (off)
  ctx.fillStyle = cardOff;
  themeRoundRect(ctx, 10, 10, 100, 90, 12, true);
  ctx.fillStyle = text;
  ctx.font = "11px sans-serif";
  ctx.fillText("Sensor", 18, 28);
  ctx.font = "bold 20px sans-serif";
  ctx.fillText("21.5°", 18, 60);
  ctx.font = "10px sans-serif";
  ctx.fillStyle = soft;
  ctx.fillText("off", 18, 88);

  // Tile 2 (on)
  ctx.fillStyle = cardOn;
  themeRoundRect(ctx, 120, 10, 100, 90, 12, true);
  ctx.fillStyle = text;
  ctx.font = "11px sans-serif";
  ctx.fillText("Light", 128, 28);
  ctx.fillStyle = accent;
  ctx.fillRect(128, 40, 80, 8);
  ctx.fillStyle = text;
  ctx.font = "10px sans-serif";
  ctx.fillText("on", 128, 88);

  // Tile 3 (heating arc)
  ctx.fillStyle = cardOn;
  themeRoundRect(ctx, 230, 10, 120, 200, 12, true);
  ctx.strokeStyle = c.heat_track_on || "#555";
  ctx.lineWidth = 10;
  ctx.beginPath();
  ctx.arc(290, 110, 45, Math.PI * 0.8, Math.PI * 0.2, false);
  ctx.stroke();
  ctx.strokeStyle = heatInd;
  ctx.beginPath();
  ctx.arc(290, 110, 45, Math.PI * 0.8, Math.PI * 1.4, false);
  ctx.stroke();
  ctx.fillStyle = text;
  ctx.font = "bold 18px sans-serif";
  ctx.fillText("22.5°", 272, 115);
  ctx.fillStyle = soft;
  ctx.font = "10px sans-serif";
  ctx.fillText("Target 22.5", 270, 170);

  // Bottom nav area
  ctx.fillStyle = c.topbar_bg || "#1a1a1a";
  ctx.fillRect(0, canvas.height - 30, canvas.width, 30);
  ctx.fillStyle = accent;
  ctx.fillRect(10, canvas.height - 26, 40, 22);
  ctx.fillStyle = text;
  ctx.font = "11px sans-serif";
  ctx.fillText("Home", 14, canvas.height - 12);
}

function themeRoundRect(ctx, x, y, w, h, r, fill) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
  if (fill) ctx.fill();
  else ctx.stroke();
}

async function themeLoadAndRender() {
  try {
    const listResp = await themeFetchList();
    themeState.list = listResp.themes;
    const active = await themeFetchActive();
    themeState.activeId = (active && active.id) || listResp.active_id || "";
    themeState.baseId = themeState.activeId;
    themeState.editing = JSON.parse(JSON.stringify(active));
    themePopulateSelect();
    themeRenderColorGrid(themeState.editing);
    themeRenderPreview();
    themeSyncEditFields();
    themeSetInfo("");
  } catch (e) {
    themeSetInfo("Load failed: " + (e && e.message ? e.message : e), true);
  }
}

/* Prefill the "Custom theme ID / name" inputs based on what's in the
 * dropdown + the currently loaded palette, so that selecting an existing
 * custom theme makes it immediately editable (Save overwrites the same id).
 * For built-in themes we clear the inputs so the user has to pick a new id. */
function themeSyncEditFields() {
  const idInput = themeEl("settingsThemeNewId");
  const nameInput = themeEl("settingsThemeNewName");
  const baseMeta = themeEl("settingsThemeBase");
  if (!idInput || !nameInput) return;

  const editing = themeState.editing || {};
  const entry = themeState.list.find((e) => e.id === editing.id);
  const isCustom = entry && !entry.builtin;

  if (isCustom) {
    idInput.value = editing.id || "";
    nameInput.value = editing.name || entry.name || "";
    idInput.readOnly = true;
    idInput.title = "Editing existing custom theme. Clear to save as a new one.";
    if (baseMeta) {
      baseMeta.textContent =
        "Editing custom theme \"" + (editing.name || editing.id) +
        "\". Changes are saved back to id \"" + editing.id + "\".";
    }
  } else {
    idInput.value = "";
    nameInput.value = "";
    idInput.readOnly = false;
    idInput.title = "";
    if (baseMeta) {
      baseMeta.textContent =
        "Base palette: " + (editing.name || editing.id || "active theme") +
        ". Edit colors below, then \"Save as custom\".";
    }
  }

  const saveBtn = themeEl("settingsThemeSaveCustomBtn");
  if (saveBtn) {
    saveBtn.textContent = isCustom ? "Save changes" : "Save as custom";
  }
}

async function themeApplyActive() {
  const sel = themeEl("settingsThemeSelect");
  if (!sel || !sel.value) return;
  try {
    const resp = await fetch("/api/themes/active", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ id: sel.value }),
    });
    if (!resp.ok) throw new Error(await resp.text());
    themeSetInfo("Active theme set to " + sel.value);
    await themeLoadAndRender();
  } catch (e) {
    themeSetInfo("Apply failed: " + e.message, true);
  }
}

async function themeExportActive() {
  const sel = themeEl("settingsThemeSelect");
  const id = (sel && sel.value) || themeState.activeId;
  if (!id) return;
  try {
    const theme = await themeFetchById(id);
    const blob = new Blob([JSON.stringify(theme, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "theme-" + id + ".json";
    a.click();
    URL.revokeObjectURL(url);
  } catch (e) {
    themeSetInfo("Export failed: " + e.message, true);
  }
}

async function themeImportFile() {
  const input = themeEl("settingsThemeImportFile");
  if (!input || !input.files || !input.files[0]) {
    themeSetInfo("Choose a JSON file first", true);
    return;
  }
  try {
    const text = await input.files[0].text();
    const theme = JSON.parse(text);
    if (!theme || !theme.id) throw new Error("invalid theme JSON: missing id");
    const resp = await fetch("/api/themes/custom", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(theme),
    });
    if (!resp.ok) throw new Error(await resp.text());
    themeSetInfo("Imported theme " + theme.id);
    await themeLoadAndRender();
  } catch (e) {
    themeSetInfo("Import failed: " + e.message, true);
  }
}

async function themeSaveCustom() {
  const idInput = themeEl("settingsThemeNewId");
  const nameInput = themeEl("settingsThemeNewName");
  let id = (idInput && idInput.value || "").trim();
  let name = (nameInput && nameInput.value || "").trim();
  // If the inputs are empty but we are editing an existing custom theme,
  // reuse its id/name so "Save" overwrites the same theme.
  const editing = themeState.editing || {};
  const editingEntry = themeState.list.find((e) => e.id === editing.id);
  if (!id && editingEntry && !editingEntry.builtin) {
    id = editing.id || "";
    if (!name) name = editing.name || editing.id || "";
  }
  if (!id || !/^[A-Za-z0-9_-]+$/.test(id)) {
    themeSetInfo("Custom theme ID must be alphanumeric (_-) only", true);
    return;
  }
  if (!themeState.editing || !themeState.editing.colors) {
    themeSetInfo("No palette to save", true);
    return;
  }
  const payload = {
    id,
    name: name || id,
    builtin: false,
    colors: themeState.editing.colors,
  };
  try {
    const resp = await fetch("/api/themes/custom", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    if (!resp.ok) throw new Error(await resp.text());
    themeSetInfo("Saved custom theme " + id);
    await themeLoadAndRender();
    // Re-select the just-saved theme so editing continues on it.
    const selAfter = themeEl("settingsThemeSelect");
    if (selAfter) {
      selAfter.value = id;
      if (typeof selAfter.onchange === "function") {
        await selAfter.onchange();
      }
    }
  } catch (e) {
    themeSetInfo("Save failed: " + e.message, true);
  }
}

async function themeDeleteCustom() {
  const sel = themeEl("settingsThemeSelect");
  if (!sel || !sel.value) return;
  const entry = themeState.list.find((e) => e.id === sel.value);
  if (!entry) return;
  if (entry.builtin) {
    themeSetInfo("Cannot delete built-in themes", true);
    return;
  }
  if (!window.confirm("Delete theme " + entry.id + "?")) return;
  try {
    const resp = await fetch("/api/themes/custom?id=" + encodeURIComponent(entry.id), { method: "DELETE" });
    if (!resp.ok) throw new Error(await resp.text());
    themeSetInfo("Deleted " + entry.id);
    await themeLoadAndRender();
  } catch (e) {
    themeSetInfo("Delete failed: " + e.message, true);
  }
}

function themeResetToActive() {
  void themeLoadAndRender();
}

function initThemeSection() {
  const applyBtn = themeEl("settingsThemeApplyBtn");
  if (applyBtn) applyBtn.onclick = () => void themeApplyActive();
  const exportBtn = themeEl("settingsThemeExportBtn");
  if (exportBtn) exportBtn.onclick = () => void themeExportActive();
  const importBtn = themeEl("settingsThemeImportBtn");
  if (importBtn) importBtn.onclick = () => void themeImportFile();
  const delBtn = themeEl("settingsThemeDeleteBtn");
  if (delBtn) delBtn.onclick = () => void themeDeleteCustom();
  const saveBtn = themeEl("settingsThemeSaveCustomBtn");
  if (saveBtn) saveBtn.onclick = () => void themeSaveCustom();
  const resetBtn = themeEl("settingsThemeResetBtn");
  if (resetBtn) resetBtn.onclick = () => themeResetToActive();
  const sel = themeEl("settingsThemeSelect");
  if (sel) {
    sel.onchange = async () => {
      try {
        const theme = await themeFetchById(sel.value);
        themeState.editing = theme;
        themeState.baseId = theme.id;
        themeRenderColorGrid(themeState.editing);
        themeRenderPreview();
        themeSyncEditFields();
      } catch (e) {
        themeSetInfo("Load failed: " + e.message, true);
      }
    };
  }
  void themeLoadAndRender();
}

// ============================================================
// Simplified layout UI: + Add dropdowns for Pages / Widgets.
// The original buttons are kept hidden (.legacy-hidden) so every
// existing handler keeps working — menu items just click them.
// ============================================================
function initSimpleUiMenus() {
  bindDropdown("addPageMenuBtn", "addPageMenu");
  bindDropdown("addWidgetMenuBtn", "addWidgetMenu");

  const pageNormal = document.getElementById("addPageMenuNormal");
  if (pageNormal) {
    pageNormal.onclick = () => {
      closeAllDropdowns();
      if (el.addPageBtn) el.addPageBtn.click();
    };
  }
  const pageEnergy = document.getElementById("addPageMenuEnergy");
  if (pageEnergy) {
    pageEnergy.onclick = () => {
      closeAllDropdowns();
      if (el.addEnergyPageBtn) el.addEnergyPageBtn.click();
    };
  }

  const widgetMenu = document.getElementById("addWidgetMenu");
  if (widgetMenu) {
    widgetMenu.querySelectorAll("[data-add-target]").forEach((item) => {
      item.onclick = () => {
        closeAllDropdowns();
        const targetId = item.getAttribute("data-add-target");
        const btn = document.getElementById(targetId);
        if (btn) btn.click();
      };
    });
  }

  document.addEventListener("click", (ev) => {
    const target = ev.target;
    if (!(target instanceof Element)) return;
    if (target.closest(".dropdown")) return;
    closeAllDropdowns();
  });
  document.addEventListener("keydown", (ev) => {
    if (ev.key === "Escape") closeAllDropdowns();
  });
}

function bindDropdown(toggleId, menuId) {
  const toggle = document.getElementById(toggleId);
  const menu = document.getElementById(menuId);
  if (!toggle || !menu) return;
  toggle.onclick = (ev) => {
    ev.stopPropagation();
    const willOpen = menu.classList.contains("hidden");
    closeAllDropdowns();
    if (willOpen) {
      menu.classList.remove("hidden");
      toggle.setAttribute("aria-expanded", "true");
    }
  };
}

function closeAllDropdowns() {
  for (const menu of document.querySelectorAll(".dropdown-menu")) {
    menu.classList.add("hidden");
  }
  for (const toggle of document.querySelectorAll(".dropdown-toggle")) {
    toggle.setAttribute("aria-expanded", "false");
  }
}

bootstrap();
