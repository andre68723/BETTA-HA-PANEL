/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Theme editor
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
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

/* The layout preview shows the panel's active palette (docs/appearance.md): page, tiles, their
 * border, texts and accent. Only the active theme, not one being edited in the theme section. */
const THEME_CANVAS_VARS = {
  "--pv-page": "content_bg",
  "--pv-card": "card_bg_off",
  "--pv-card-on": "card_bg_on",
  "--pv-border": "card_border",
  "--pv-text": "text_primary",
  "--pv-soft": "text_soft",
  "--pv-muted": "text_muted",
  "--pv-accent": "nav_tab_active",
  "--pv-nav-active": "nav_btn_bg_active",
  "--pv-light-fill": "light_ind_on",
};

function themeApplyCanvasPalette(theme) {
  const canvas = document.getElementById("canvas");
  const colors = theme && theme.colors;
  if (!canvas || !colors) return;
  for (const [name, key] of Object.entries(THEME_CANVAS_VARS)) {
    const value = colors[key];
    if (typeof value === "string" && /^#[0-9a-fA-F]{6}$/.test(value)) canvas.style.setProperty(name, value);
  }
  canvas.style.setProperty("--pv-matte-page", colors.matte_page_bg || colors.content_bg);
  canvas.style.setProperty("--pv-matte-off", colors.matte_card_off || colors.card_bg_off);
  canvas.style.setProperty("--pv-matte-on", colors.matte_card_on || colors.card_bg_on);
  canvas.style.setProperty("--pv-matte-track", colors.matte_track || colors.card_bg_off);
  canvas.style.setProperty("--pv-matte-accent", colors.matte_accent || colors.light_ind_on);
  /* The tile previews use the editor's text tokens; inside the canvas they follow the panel. */
  canvas.style.setProperty("--text", "var(--pv-text)");
  canvas.style.setProperty("--text-2", "var(--pv-soft)");
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
  { id: "matte", label: "Matte surfaces", keys: ["matte_page_bg", "matte_card_off", "matte_card_on", "matte_track", "matte_accent"] },
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
  ctx.fillStyle = themeReadableOn(accent, text);
  ctx.font = "11px sans-serif";
  ctx.fillText("Home", 14, canvas.height - 12);
}

// The text colour that stays readable on a fill: a dark one on a light fill, else the given one.
function themeReadableOn(fill, fallback) {
  const m = /^#?([0-9a-f]{6})$/i.exec(String(fill || "").trim());
  if (!m) return fallback;
  const n = parseInt(m[1], 16);
  const luminance = (0.299 * ((n >> 16) & 255) + 0.587 * ((n >> 8) & 255) + 0.114 * (n & 255)) / 255;
  return luminance > 0.6 ? "#10202e" : fallback;
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
    themeApplyCanvasPalette(active);
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
