/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Normalizers, layout normalization, status line, small helpers (translation lookup, storage)
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
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

// An LED block count the panel accepts, or 0 for the default (left out of the layout).
function normalizeGaugeSegmentCount(value) {
  if (value === null || value === undefined || (typeof value === "string" && value.trim() === "")) return 0;
  const parsed = Math.round(Number(value));
  if (!Number.isFinite(parsed) || parsed === GAUGE_SEGMENTS_DEFAULT) return 0;
  return clamp(parsed, GAUGE_SEGMENTS_MIN, GAUGE_SEGMENTS_MAX);
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
    if (isHousePage(page)) {
      normalizeHouseConfig(page);
      continue;
    }
    if (!page || !Array.isArray(page.widgets)) continue;
    for (const widget of page.widgets) {
      normalizeEnergyTimelineColors(widget);
      if (widget && (widget.type !== "camera_tile" || widget.camera_live !== true)) delete widget.camera_live;
      if (!widget || typeof widget !== "object") continue;
      if (widget.type === "roborock_tile") {
        widget.type = "vacuum_tile"; // legacy name
      }
      if (widget.type === "binary_tile" && widget.display_mode === "door" && widget.rect) {
        // Grow a door-look tile to the size the panel actually draws it (see widgetSizeType()).
        widget.rect = clampRectToCanvas(widget.rect, "contact_tile");
      }
      if (widgetSupportsName(widget.type) && widgetShowsName(widget) !== widgetNameDefault(widget.type)) widget.show_name = widgetShowsName(widget);
      else delete widget.show_name; // only a deviation from the type's default is stored
      delete widget.sensor_show_label; // read-only compatibility for old sensor layouts
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
        if (widget.graph_ha_history !== true) delete widget.graph_ha_history;
        const normalizedGraphPoints = normalizeGraphPointCount(widget.graph_point_count);
        if (normalizedGraphPoints > 0) {
          widget.graph_point_count = normalizedGraphPoints;
        } else {
          delete widget.graph_point_count;
        }
      }
      if (widget.type === "gauge") {
        widget.style_variant = GAUGE_STYLES.includes(widget.style_variant) ? widget.style_variant : "linear";
        widget.gauge_bar_style = widget.gauge_bar_style === "segmented" ? "segmented" : "smooth";
        const segments = widget.gauge_bar_style === "segmented" ? normalizeGaugeSegmentCount(widget.gauge_segment_count) : 0;
        if (segments) {
          widget.gauge_segment_count = segments;
        } else {
          delete widget.gauge_segment_count;
        }
        const min = Number(widget.gauge_min);
        const max = Number(widget.gauge_max);
        widget.gauge_min = Number.isFinite(min) ? min : 0;
        widget.gauge_max = Number.isFinite(max) && max > widget.gauge_min ? max : widget.gauge_min + 100;
      }
      if (widget.type === "solar_forecast") {
        widget.entity_id = widget.entity_id || DEFAULT_SOLCAST_ENTITIES.remaining;
        // ?? keeps an explicitly cleared slot ("") empty; only absent fields get defaults.
        widget.forecast_today_entity_id = widget.forecast_today_entity_id ?? DEFAULT_SOLCAST_ENTITIES.today;
        widget.forecast_tomorrow_entity_id = widget.forecast_tomorrow_entity_id ?? DEFAULT_SOLCAST_ENTITIES.tomorrow;
        widget.forecast_day_3_entity_id = widget.forecast_day_3_entity_id ?? DEFAULT_SOLCAST_ENTITIES.day3;
        widget.forecast_day_4_entity_id = widget.forecast_day_4_entity_id ?? DEFAULT_SOLCAST_ENTITIES.day4;
        widget.forecast_day_5_entity_id = widget.forecast_day_5_entity_id ?? DEFAULT_SOLCAST_ENTITIES.day5;
        widget.solar_forecast_bar_orientation = normalizeSolarForecastBarOrientation(widget.solar_forecast_bar_orientation);
        const barMax = Number(widget.solar_forecast_bar_max_kwh);
        if (Number.isFinite(barMax) && barMax > 0) {
          widget.solar_forecast_bar_max_kwh = barMax;
        } else {
          delete widget.solar_forecast_bar_max_kwh;
        }
        delete widget.secondary_entity_id;
        delete widget.tertiary_entity_id;
      } else if (widget.type === "sonos_favorites") {
        if (typeof widget.secondary_entity_id !== "string" || widget.secondary_entity_id.trim() === "") {
          widget.secondary_entity_id = DEFAULT_SONOS_FAVORITES_SENSOR;
        }
        delete widget.tertiary_entity_id;
      } else if (widget.type === "car_widget" || widget.type === "gauge") {
        if (typeof widget.secondary_entity_id !== "string") {
          widget.secondary_entity_id = "";
        }
        if (typeof widget.tertiary_entity_id !== "string") {
          widget.tertiary_entity_id = "";
        }
      } else if (widget.type === "vacuum_tile" || widget.type === "energy_timeline" || widget.type === "parqet_portfolio") {
        if (typeof widget.tertiary_entity_id !== "string") {
          delete widget.tertiary_entity_id;
        }
      } else {
        delete widget.tertiary_entity_id;
      }
    }
  }
}

function setStatus(text, isError = false) {
  el.status.textContent = text;
  el.status.style.color = isError ? "#ff8f94" : "#9db0c3";
}

// Unsaved-changes tracking: clearDirty() (after loading and after saving, the only two moments the in-memory
// layout matches the panel) remembers the layout as JSON; markDirty() is called after every render or edit and
// only shows the badge when the layout really differs from that snapshot. Selecting a page or a widget renders
// again without changing the layout, so it must not count as an unsaved change. Previously there was no
// indication at all besides the transient "Layout loaded"/"Saved" status text, which a later unrelated status
// message silently overwrites.
let savedLayoutSnapshot = null;

function layoutSnapshot() {
  try {
    return JSON.stringify(editor.layout);
  } catch (err) {
    return null;
  }
}

function markDirty() {
  if (typeof layoutHistory !== "undefined" && typeof pendingUndo !== "undefined" && pendingUndo &&
      layoutSnapshot() !== layoutHistory.states[layoutHistory.cursor]?.json) hideUndoToast();
  if (typeof layoutHistoryRecord === "function") layoutHistoryRecord();
  if (savedLayoutSnapshot !== null) {
    const now = layoutSnapshot();
    if (now !== null && now === savedLayoutSnapshot) {
      setDirtyState(false);
      return;
    }
  }
  setDirtyState(true);
}

function clearDirty(snapshot = layoutSnapshot()) {
  savedLayoutSnapshot = snapshot;
  if (typeof layoutHistory !== "undefined" && !layoutHistory.states.length) layoutHistoryReset();
  setDirtyState(layoutSnapshot() !== snapshot);
}

function setDirtyState(dirty) {
  editor.dirty = dirty;
  if (el.unsavedBadge) el.unsavedBadge.classList.toggle("hidden", !dirty);
  if (el.saveBtn) el.saveBtn.classList.toggle("unsaved-highlight", dirty);
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
