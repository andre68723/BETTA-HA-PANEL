/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Style cards, firmware info, update URL, HA diagnostics, canvas geometry
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
// The Style group is only worth showing when the selected widget has style options.
function updateStyleGroupVisibility() {
  const group = document.getElementById("inspectorGroupStyle");
  if (!group) return;
  const hasOptions = Array.from(group.querySelectorAll(".slider-options")).some(
    (node) => !node.classList.contains("hidden")
  );
  group.classList.toggle("hidden", !hasOptions);
}

// A stored style ID as the panel resolves it (ui_style_lookup() in the firmware).
function normalizeUiStyle(style) {
  return UI_STYLES.includes(style) ? style : UI_STYLE_FALLBACK;
}

// Editor previews follow the panel's global style (Settings > UI).
function applyUiStyleAttribute(style) {
  document.body.dataset.uiStyle = normalizeUiStyle(style);
}

function renderStyleCards() {
  const box = document.getElementById("settingsUiStyleCards");
  if (!box || !el.settingsUiStyle) return;
  if (!box.children.length) {
    for (const style of UI_STYLES) {
      const card = document.createElement("button");
      card.type = "button";
      card.className = "style-card";
      card.dataset.style = style;
      card.setAttribute("role", "radio");
      card.innerHTML =
        '<span class="style-preview"><span class="sp-btn"></span><span class="sp-bar"><i></i></span></span><span class="style-name"></span>';
      card.addEventListener("click", () => {
        if (el.settingsUiStyle.value === style) return;
        el.settingsUiStyle.value = style;
        el.settingsUiStyle.dispatchEvent(new Event("change", { bubbles: true }));
      });
      box.appendChild(card);
    }
  }
  for (const card of box.children) {
    card.querySelector(".style-name").textContent = t(`settings.appearance.${card.dataset.style}`);
    const on = card.dataset.style === el.settingsUiStyle.value;
    card.classList.toggle("selected", on);
    card.setAttribute("aria-checked", on ? "true" : "false");
  }
}

// The user's text size step as the panel stores it (ui.text_step, 0 Standard, 1 Larger, 2 Largest).
function normalizeTextStep(value) {
  const step = Math.round(Number(value));
  return Number.isFinite(step) ? Math.min(2, Math.max(0, step)) : 0;
}

// Pixel size of a text role on the connected panel at the chosen text step (/api/version "text"), or the fallback before it is known.
function textRoleSize(role, fallback) {
  const text = editor.appText;
  const index = text && Array.isArray(text.roles) ? text.roles.indexOf(role) : -1;
  const step = normalizeTextStep(el.settingsUiTextStep ? el.settingsUiTextStep.value : editor.settings?.ui?.text_step);
  const row = text && Array.isArray(text.sizes) ? text.sizes[step] : null;
  return index >= 0 && row && Number.isFinite(row[index]) ? row[index] : fallback;
}

function updateTextStepInfo() {
  const info = document.getElementById("settingsUiTextStepInfo");
  if (!info) return;
  info.textContent = editor.appText
    ? t("settings.ui.text_step_info", {
      meta: textRoleSize("meta", 0), label: textRoleSize("label", 0), value: textRoleSize("value", 0),
      key: textRoleSize("key", 0), hero: textRoleSize("hero", 0),
    })
    : "";
}

function updateStyleInfo() {
  renderStyleCards();
  const info = document.getElementById("settingsUiStyleInfo");
  if (info && el.settingsUiStyle) {
    info.textContent = t(`settings.appearance.${el.settingsUiStyle.value}_info`);
  }
}

function setTextById(id, key, vars) {
  const node = document.getElementById(id);
  if (!node) return;
  node.textContent = t(key, vars);
}

function setActionTooltipById(id, key) {
  const node = document.getElementById(id);
  if (!node) return;
  node.title = t(key);
  node.setAttribute("aria-label", node.title);
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

// Display labels for /api/version's "components" map; order here is the
// display order. Keys not present in the payload (e.g. esp_lcd_jd9365 on
// variants that don't use it) are skipped rather than shown blank. descKey
// points at a short i18n description of what the component is/does.
const COMPONENT_VERSION_LABELS = [
  ["esp_idf", "ESP-IDF", "settings.firmware_info.desc.esp_idf"],
  ["lvgl", "LVGL", "settings.firmware_info.desc.lvgl"],
  ["esp_lvgl_adapter", "esp_lvgl_adapter", "settings.firmware_info.desc.esp_lvgl_adapter"],
  ["esp_lvgl_port", "esp_lvgl_port", "settings.firmware_info.desc.esp_lvgl_port"],
  ["esp_lcd_jd9365", "esp_lcd_jd9365", "settings.firmware_info.desc.esp_lcd_jd9365"],
  ["esp_lcd_touch", "esp_lcd_touch", "settings.firmware_info.desc.esp_lcd_touch"],
  ["esp_hosted", "esp_hosted", "settings.firmware_info.desc.esp_hosted"],
  ["esp_wifi_remote", "esp_wifi_remote", "settings.firmware_info.desc.esp_wifi_remote"],
  ["esp_websocket_client", "esp_websocket_client", "settings.firmware_info.desc.esp_websocket_client"],
  ["littlefs", "littlefs", "settings.firmware_info.desc.littlefs"],
  ["mqtt", "mqtt", "settings.firmware_info.desc.mqtt"],
  ["cjson", "cjson", "settings.firmware_info.desc.cjson"],
  ["esp_serial_slave_link", "esp_serial_slave_link", "settings.firmware_info.desc.esp_serial_slave_link"],
  ["esp32_c6_firmware", "ESP32-C6 firmware", "settings.firmware_info.desc.esp32_c6_firmware"],
  ["waveshare_bsp", "Waveshare BSP", "settings.firmware_info.desc.waveshare_bsp"],
  ["m5stack_bsp", "M5Stack Tab5 BSP", "settings.firmware_info.desc.m5stack_bsp"],
  ["esp_lcd_st7703", "esp_lcd_st7703", "settings.firmware_info.desc.esp_lcd_st7703"],
  ["esp_lcd_touch_gt911", "esp_lcd_touch_gt911", "settings.firmware_info.desc.esp_lcd_touch_gt911"],
  ["esp_video", "esp_video", "settings.firmware_info.desc.esp_video"],
  ["esp_ipa", "esp_ipa", "settings.firmware_info.desc.esp_ipa"],
  ["esp_cam_sensor", "esp_cam_sensor", "settings.firmware_info.desc.esp_cam_sensor"],
  ["esp_sccb_intf", "esp_sccb_intf", "settings.firmware_info.desc.esp_sccb_intf"],
];

function renderMqttAdvancedExample() {
  const mqttAdvancedCode = document.getElementById("settingsMqttAdvancedCode");
  const mqttAdvancedNote = document.getElementById("settingsMqttAdvancedNote");
  const topic = editor.mqttCommandTopic || "";
  if (mqttAdvancedCode) {
    mqttAdvancedCode.textContent = topic
      ? t("settings.mqtt.advanced_yaml", { topic })
      : t("settings.mqtt.advanced_yaml_placeholder");
  }
  if (mqttAdvancedNote) {
    mqttAdvancedNote.textContent = topic
      ? t("settings.mqtt.advanced_note", { topic })
      : t("settings.mqtt.advanced_note_unknown");
  }
}

function renderFirmwareInfo() {
  if (!el.settingsFirmwareInfoBox) return;
  const components = editor.appComponents;
  const rows = [];
  if (components && typeof components === "object") {
    for (const [key, label, descKey] of COMPONENT_VERSION_LABELS) {
      const value = components[key];
      if (typeof value === "string" && value.trim().length > 0) {
        rows.push([label, value.trim(), t(descKey)]);
      }
    }
  }

  if (el.settingsFirmwareInfoSummary) {
    el.settingsFirmwareInfoSummary.textContent = t("settings.firmware_info.components", { count: rows.length });
  }
  if (el.settingsFirmwareInfoDetails) el.settingsFirmwareInfoDetails.hidden = rows.length === 0;

  const hasInfo = rows.length > 0 || Boolean(editor.appBuildDate) || Boolean(editor.appGitCommit);
  el.settingsFirmwareInfoBox.hidden = !hasInfo;
  if (!hasInfo) return;

  if (el.settingsFirmwareInfoMeta) {
    const built = [editor.appBuildDate, editor.appBuildTime].filter(Boolean).join(" ");
    el.settingsFirmwareInfoMeta.textContent = t("settings.firmware_info.meta", {
      built: built || "?",
      commit: editor.appGitCommit || "?",
    });
  }

  if (el.settingsFirmwareInfoBody) {
    el.settingsFirmwareInfoBody.replaceChildren();
    for (const [label, version, description] of rows) {
      const tr = document.createElement("tr");
      for (const text of [label, version, description]) {
        const td = document.createElement("td");
        td.textContent = text;
        tr.appendChild(td);
      }
      el.settingsFirmwareInfoBody.appendChild(tr);
    }
  }
  if (el.settingsFirmwareInfoTable) {
    el.settingsFirmwareInfoTable.hidden = rows.length === 0;
  }
}

// Why the panel stopped sending requests to Home Assistant (ha_auth_state.h): a rejected token or an IP
// ban blocks everything until a restart or a new token; "not_admin" only turns the entity search off.
const HA_AUTH_REASONS = ["token_rejected", "forbidden", "not_admin"];

function renderHaAuthBanner(reason) {
  const banner = document.getElementById("haAuthBanner");
  if (!banner) return;
  const known = HA_AUTH_REASONS.includes(reason);
  banner.hidden = !known;
  if (!known) return;
  document.getElementById("haAuthTitle").textContent = t(`ha_auth.${reason}.title`);
  document.getElementById("haAuthHint").textContent = t(`ha_auth.${reason}.hint`);
}

const CRASH_BANNER_DISMISS_KEY = "betta.crashBannerDismissed";

// A stored crash dump (a panic, or the UI watchdog's deliberate abort) is easy to miss in Settings > Device; show it above
// the workspace until it is dismissed. The dismissal is remembered per dump, so a new crash shows the banner again.
function crashDumpId(dump) {
  return dump?.id || `${dump?.task || "?"}|${dump?.pc || "?"}|${dump?.mcause || "?"}`;
}

function crashReportItems(dump) {
  if (Array.isArray(dump?.items)) return dump.items.slice(0, 2).filter(item => item?.present !== false);
  return dump?.present ? [dump] : [];
}

function renderCrashReports(dump) {
  const list = document.getElementById("coredumpList");
  if (!list) return;
  list.replaceChildren();
  for (const report of crashReportItems(dump)) {
    const entry = document.createElement("div");
    entry.className = "coredump-entry";
    const description = document.createElement("div");
    description.textContent = crashDescription(report, "settings.status.coredump");
    const metadata = document.createElement("div");
    metadata.className = "meta";
    const build = report.git_commit || report.version || t("settings.status.coredump_unknown_build");
    metadata.textContent = `${t("settings.status.coredump_build", { build })} · ${(Number(report.size || 0) / 1024).toFixed(1)} KiB`;
    if (Object.hasOwn(report, "crashed_at") && !report.crashed_at) metadata.textContent += ` · ${t("settings.status.coredump_unknown_time")}`;
    const actions = document.createElement("div");
    actions.className = "row coredump-actions";
    const download = document.createElement("a");
    download.className = "btn";
    const query = report.id ? `?id=${encodeURIComponent(report.id)}` : "";
    download.href = `/api/coredump${query}`;
    download.download = report.id ? `coredump-${report.id}.bin` : "coredump.bin";
    download.textContent = t("settings.status.coredump_download");
    const erase = document.createElement("button");
    erase.className = "btn";
    erase.type = "button";
    erase.textContent = t("settings.status.coredump_erase");
    erase.disabled = Boolean(editor.crashReportBusy);
    erase.addEventListener("click", () => eraseCrashReport(report));
    actions.append(download, erase);
    const details = document.createElement("details");
    const summary = document.createElement("summary");
    summary.textContent = t("settings.status.coredump_details");
    const info = document.createElement("div");
    info.className = "meta coredump-details";
    info.textContent = [report.project, report.version, report.build_date, report.build_time,
      `${report.pc || "?"} · ${report.mcause_name || report.mcause || "?"}`, report.panic_reason,
      report.elf_sha256 ? `ELF SHA256: ${report.elf_sha256}` : "",
      report.ppa ? `PPA: ${report.ppa.width} × ${report.ppa.height} @ ${report.ppa.input_x},${report.ppa.input_y} · ${report.ppa.rotation}° · ${report.ppa.wait_ms} ms` : "",
      report.ppa ? `Buffers: 0x${Number(report.ppa.input_buffer).toString(16)} → 0x${Number(report.ppa.output_buffer).toString(16)} · ${report.ppa.output_capacity} B` : "",
    ].filter(Boolean).join(" · ");
    details.append(summary, info);
    entry.append(description, metadata, actions);
    if (info.textContent) entry.append(details);
    list.append(entry);
  }
}

async function eraseCrashReport(report) {
  if (editor.crashReportBusy || !window.confirm(t("settings.status.coredump_confirm_erase"))) return;
  const error = document.getElementById("coredumpError");
  if (error) error.textContent = "";
  editor.crashReportBusy = true;
  renderCrashReports({ items: editor.crashReports });
  try {
    const query = report.id ? `?id=${encodeURIComponent(report.id)}` : "";
    const response = await fetch(`/api/coredump/erase${query}`, {
      method: "POST", headers: { "Content-Type": "application/json" },
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const items = editor.crashReports.filter(item => crashDumpId(item) !== crashDumpId(report));
    renderCrashBanner({ present: items.length > 0, items });
    await refreshDeviceStatus();
  } catch (_) {
    if (error) error.textContent = t("settings.status.coredump_erase_failed");
  } finally {
    editor.crashReportBusy = false;
    renderCrashReports({ items: editor.crashReports });
  }
}

function renderCrashBanner(dump) {
  editor.crashReports = crashReportItems(dump);
  dump = editor.crashReports[0] || { present: false };
  if (dump.present !== false) dump = { ...dump, present: true };
  if (!dump?.present || !editor.crashDump || crashDumpId(dump) !== crashDumpId(editor.crashDump)) {
    editor.crashTimeEpoch = 0;
  }
  editor.crashDump = dump?.present ? dump : null;
  renderCrashReports({ items: editor.crashReports });
  const row = document.getElementById("coredumpRow");
  if (row) row.hidden = !dump?.present;
  const text = document.getElementById("coredumpText");
  if (text) text.textContent = dump?.present ? crashDescription(dump, "settings.status.coredump") : "";
  const banner = document.getElementById("crashBanner");
  if (!banner) return;
  const show = Boolean(dump?.present) && storageGet(CRASH_BANNER_DISMISS_KEY) !== crashDumpId(dump);
  banner.hidden = !show;
  if (!show) return;
  document.getElementById("crashBannerTitle").textContent = t("crash.banner_title");
  document.getElementById("crashBannerHint").textContent = crashDescription(dump, "crash.banner_hint");
}

function crashDescription(dump, key) {
  const description = t(key, {
    task: dump.task || "?",
    pc: dump.pc || "?",
    mcause: ["ppa_timeout", "lvgl_stall", "runtime_stall"].includes(dump.cause) ? t(`settings.status.coredump_cause_${dump.cause}`) : dump.mcause_name || dump.mcause || "?",
  });
  const time = formatCrashTime(Object.hasOwn(dump, "crashed_at") ? Number(dump.crashed_at) : editor.crashTimeEpoch);
  return time ? `${description} ${time}` : description;
}

// The crash dump has no timestamp of its own; the reboot log records the wall-clock time of the run that ended in the panic.
function crashTimeFromRebootLog(entries) {
  const sorted = [...(entries || [])].sort((a, b) => Number(b.seq) - Number(a.seq));
  const hit = sorted.find((e) => e && (e.label === "panic" || e.reason === "panic" || e.cause === "ui_watchdog") && Number(e.previous_wall_epoch) > 0);
  return hit ? Number(hit.previous_wall_epoch) : 0;
}

function formatCrashTime(epochSeconds, nowMs = Date.now()) {
  if (!(epochSeconds > 0)) return "";
  const when = new Date(epochSeconds * 1000);
  const text = when.toLocaleString([], { day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit" });
  const minutes = Math.max(0, Math.round((nowMs - when.getTime()) / 60000));
  const ago = minutes < 90 ? t("crash.ago_minutes", { n: minutes }) : t("crash.ago_hours", { n: Math.round(minutes / 60) });
  return t("crash.when", { when: text, ago });
}

async function loadCrashTime() {
  const dump = editor.crashDump;
  if (!dump?.present || Object.hasOwn(dump, "crashed_at")) return;
  try {
    const payload = await apiGet("/api/reboot-log");
    if (!editor.crashDump || crashDumpId(dump) !== crashDumpId(editor.crashDump)) return;
    editor.crashTimeEpoch = crashTimeFromRebootLog(payload?.entries);
    renderCrashBanner(editor.crashDump);
  } catch (_) {
    // the time is a nicety; the banner works without it
  }
}

function bindCrashBanner() {
  const open = document.getElementById("crashBannerOpen");
  if (open) {
    open.onclick = () => {
      setActivePane("settings");
      setActiveSettingsSection("settingsDeviceSection");
      document.getElementById("coredumpRow")?.scrollIntoView({ block: "center" });
    };
  }
  const dismiss = document.getElementById("crashBannerDismiss");
  if (dismiss) {
    dismiss.onclick = () => {
      if (editor.crashDump) storageSet(CRASH_BANNER_DISMISS_KEY, crashDumpId(editor.crashDump));
      document.getElementById("crashBanner").hidden = true;
    };
  }
}

async function loadAppVersion() {
  try {
    const payload = await apiGet("/api/version");
    editor.appVersion = typeof payload?.version === "string" ? payload.version : "";
    editor.appProject = typeof payload?.project === "string" ? payload.project : "";
    editor.appScreenW = Number(payload?.screen_w) || 0;
    editor.appScreenH = Number(payload?.screen_h) || 0;
    editor.appMaxPages = Number(payload?.max_pages) || 0;
    editor.appFullNavPages = Number(payload?.full_nav_pages) || 0;
    editor.appBoard = typeof payload?.board === "string" ? payload.board : "";
    editor.appText = payload?.text && Array.isArray(payload.text.roles) && Array.isArray(payload.text.sizes) ? payload.text : null;
    updateTextStepInfo();
    editor.appSafeMode = payload?.safe_mode === true;
    const safeBanner = document.getElementById("safeModeBanner");
    if (safeBanner) safeBanner.hidden = !editor.appSafeMode;
    renderHaAuthBanner(payload?.ha_auth);
    renderCrashBanner(payload?.coredump);
    if (payload?.coredump?.present) loadCrashTime();
    editor.appComponents = payload?.components && typeof payload.components === "object" ? payload.components : null;
    editor.appBuildDate = typeof payload?.build_date === "string" ? payload.build_date : "";
    editor.appBuildTime = typeof payload?.build_time === "string" ? payload.build_time : "";
    editor.appGitCommit = typeof payload?.git_commit === "string" ? payload.git_commit : "";
    editor.appFreeHeap = Number(payload?.memory?.free_heap_internal) || 0;
    editor.appMinFreeHeap = Number(payload?.memory?.min_free_heap_internal) || 0;
    editor.appInternalTotal = Number(payload?.memory?.internal_total) || 0;
    editor.appFreePsram = Number(payload?.memory?.free_psram) || 0;
    editor.appPsramTotal = Number(payload?.memory?.psram_total) || 0;
    renderBuildTime();
    renderBoardLabel();
    applyCanvasGeometry(payload);
  } catch (_) {
    editor.appVersion = "";
    editor.appProject = "";
    editor.appScreenW = 0;
    editor.appScreenH = 0;
    editor.appBoard = "";
    editor.appComponents = null;
    editor.appBuildDate = "";
    editor.appBuildTime = "";
    editor.appGitCommit = "";
    editor.appFreeHeap = 0;
    editor.appMinFreeHeap = 0;
    editor.appInternalTotal = 0;
    editor.appFreePsram = 0;
    editor.appPsramTotal = 0;
    renderBoardLabel();
  }
  renderAppVersion();
  renderFirmwareInfo();
  void loadOtaStatus(true); // also carries the panel's daily update check
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

// Dismissal is remembered for this set of missing entities; another missing entity shows the banner again.
const HA_DIAGNOSTICS_DISMISS_KEY = "betta.haDiagnosticsDismissed";

function bindHaDiagnosticsBanner() {
  if (!el.haDiagnosticsDismiss) return;
  el.haDiagnosticsDismiss.onclick = () => {
    const signature = haDiagnosticsSignature();
    if (signature) storageSet(HA_DIAGNOSTICS_DISMISS_KEY, signature);
    if (el.haDiagnosticsBanner) el.haDiagnosticsBanner.hidden = true;
  };
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
  if (editor.activePane === "settings") {
    banner.hidden = true; /* about the layout: only shown on the layout tab */
    return;
  }
  if (!d || !d.total || !signature) {
    banner.hidden = true;
    return;
  }
  if (storageGet(HA_DIAGNOSTICS_DISMISS_KEY) === signature) {
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

function applyCanvasGeometry(payload, fromLayout = false) {
  if (!payload || typeof payload !== "object") return;
  const maxBytes = Number(payload.layout_max_bytes);
  if (Number.isFinite(maxBytes) && maxBytes >= 4096) MAX_LAYOUT_BYTES = Math.round(maxBytes);
  // A version refresh must not resize the canvas underneath a loaded layout.
  if (!fromLayout && editor.layout && editor.layout.orientation) return;
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
  updateCanvasScale();
}

// The editor stacks its columns below 1100 px (the media query in styles.css).
function isStackedLayout() {
  return typeof window.matchMedia === "function" && window.matchMedia("(max-width: 1100px)").matches;
}

function updateCanvasScale() {
  if (!el.canvas || !el.canvasWrap) return;
  el.canvasStage = el.canvasStage || document.getElementById("canvasStage");
  const stage = el.canvasStage;
  if (!stage) return;

  const wrapRect = el.canvasWrap.getBoundingClientRect();
  const wrapStyle = window.getComputedStyle(el.canvasWrap);
  const paddingX = (Number.parseFloat(wrapStyle.paddingLeft) || 0) + (Number.parseFloat(wrapStyle.paddingRight) || 0);
  const paddingY = (Number.parseFloat(wrapStyle.paddingTop) || 0) + (Number.parseFloat(wrapStyle.paddingBottom) || 0);
  const availableW = Math.max(240, el.canvasWrap.clientWidth - paddingX);
  // Side by side the canvas must fit the window height; stacked (max-width 1100px) the page scrolls, so only the width limits it.
  const stacked = isStackedLayout();
  const availableH = stacked ? Infinity : Math.max(220, window.innerHeight - wrapRect.top - paddingY - 24);
  const nextScale = Math.max(0.35, Math.min(1, availableW / CANVAS_WIDTH, availableH / CANVAS_HEIGHT));

  editor.canvasScale = nextScale;
  stage.style.width = `${Math.ceil(CANVAS_WIDTH * nextScale)}px`;
  stage.style.height = `${Math.ceil(CANVAS_HEIGHT * nextScale)}px`;
  el.canvas.style.width = `${CANVAS_WIDTH}px`;
  el.canvas.style.height = `${CANVAS_HEIGHT}px`;
  el.canvas.style.setProperty("--canvas-scale", String(nextScale));
}
