/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Settings pages: Wi-Fi, OTA, device status, alerts, backup, security, saving
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
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

// A failed read of /api/settings says nothing about the configuration: right after a reboot (an OTA
// update with the editor still open) or during a short Wi-Fi drop it simply fails, and treating that
// as "no Wi-Fi configured" used to show the Wi-Fi provisioning form on a fully configured panel,
// inviting the user to overwrite working settings. Retry for a few seconds, then say what happened.
const SETTINGS_BOOT_RETRY_DELAYS_MS = [1000, 1500, 2000, 2500, 3000];

async function loadSettingsForBootstrap() {
  let settings = await loadSettings(true);
  for (const delay of SETTINGS_BOOT_RETRY_DELAYS_MS) {
    if (settings) break;
    await new Promise((resolve) => setTimeout(resolve, delay));
    settings = await loadSettings(true);
  }
  return settings;
}

function showBootError() {
  // The UI language is a panel setting, which is exactly what could not be read: use the browser's
  // language from the built-in tables, without another request to the unreachable panel.
  editor.i18nMap = { ...(WEB_I18N_BUILTIN.en || {}), ...(WEB_I18N_BUILTIN[browserSetupLanguage()] || {}) };
  if (el.provisioningWifiPage) el.provisioningWifiPage.classList.add("hidden");
  if (el.provisioningHaPage) el.provisioningHaPage.classList.add("hidden");
  if (el.bootErrorTitle) el.bootErrorTitle.textContent = t("boot_error.title");
  if (el.bootErrorText) el.bootErrorText.textContent = t("boot_error.text");
  if (el.bootErrorDetail) el.bootErrorDetail.textContent = editor.settingsLoadError || "";
  if (el.bootErrorRetryBtn) {
    el.bootErrorRetryBtn.textContent = t("boot_error.retry");
    el.bootErrorRetryBtn.onclick = () => window.location.reload();
  }
  if (el.bootErrorPage) el.bootErrorPage.classList.remove("hidden");
  setProvisioningVisible(true);
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
  if (el.bootErrorPage) el.bootErrorPage.classList.add("hidden");
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

// Every top-level heading of a settings section and the content after it becomes its own box.
function wrapSettingsBoxes(section) {
  const groups = [];
  let current = null;
  for (const child of Array.from(section.children)) {
    if (child.tagName === "H2" || !current) {
      current = document.createElement("div");
      current.className = "settings-box";
      groups.push(current);
    }
    current.appendChild(child);
  }
  section.replaceChildren(...groups);
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

  let lastNavGroup = null;
  for (const item of SETTINGS_NAV_ITEMS) {
    const heading = document.getElementById(item.headingId);
    const section = heading?.closest("section");
    if (!section) continue;

    section.id = item.sectionId;
    if (!section.classList.contains("display-settings")) wrapSettingsBoxes(section);
    section.classList.add("settings-content-section", "hidden");
    contentPane.appendChild(section);

    if (item.group && item.group !== lastNavGroup) {
      lastNavGroup = item.group;
      const groupLabel = document.createElement("div");
      groupLabel.className = "settings-nav-group";
      groupLabel.dataset.i18nKey = item.group;
      groupLabel.textContent = t(item.group);
      nav.appendChild(groupLabel);
    }

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
    actionsSection.classList.add("settings-sidebar-actions", "panel-section");
    actionsHeading.classList.add("hidden");
    el.settingsPane.replaceChildren(navSection);
    // Save / reload live in the right column, next to the settings, so they need no scrolling.
    if (el.actionsPanel) {
      el.actionsPanel.appendChild(actionsSection);
    } else {
      el.settingsPane.appendChild(actionsSection);
    }
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
  for (const label of document.querySelectorAll(".settings-nav-group")) {
    const key = label.dataset.i18nKey;
    if (key) {
      label.textContent = t(key);
    }
  }
  if (editor.activePane === "settings") {
    setActiveSettingsSection(editor.activeSettingsSection);
  }
}

function activeSettingsNavItem(sectionId = editor.activeSettingsSection) {
  return SETTINGS_NAV_ITEMS.find((item) => item.sectionId === sectionId) || SETTINGS_NAV_ITEMS[0];
}

// Per-tab navigation survives reloads without changing the device's configuration.
function rememberEditorLocation() {
  if (!editor.workspaceNavigationReady) return;
  try {
    window.sessionStorage.setItem("betta-editor-location", JSON.stringify({
      pane: editor.activePane, section: editor.activeSettingsSection,
    }));
  } catch (_) {}
}

function restoreEditorLocation() {
  let saved;
  try { saved = JSON.parse(window.sessionStorage.getItem("betta-editor-location")); } catch (_) {}
  editor.activeSettingsSection = activeSettingsNavItem(saved?.section || "settingsDeviceSection").sectionId;
  editor.workspaceNavigationReady = true;
  setActivePane(saved?.pane === "settings" ? "settings" : "layout");
}

function setActiveSettingsSection(sectionId) {
  setupSettingsWorkspace();
  const item = activeSettingsNavItem(sectionId);
  if (!item) return;

  if (item.sectionId === "settingsSoundSection" && editor.soundSupported === false) {
    setActiveSettingsSection("settingsDeviceSection");
    return;
  }
  editor.activeSettingsSection = item.sectionId;
  if (item.sectionId === "settingsAlertsSection") {
    renderAlertList(); // the draft survives leaving and returning; a reloaded layout replaces it
  }
  for (const section of el.settingsContentSections || []) {
    section.classList.toggle("hidden", section.id !== item.sectionId);
  }
  for (const button of el.settingsNavButtons || []) {
    // A board without audio has no Sound settings at all (see sound_supported in /api/settings).
    button.hidden = button.dataset.settingsSection === "settingsSoundSection" && editor.soundSupported === false;
    const active = button.dataset.settingsSection === item.sectionId;
    button.classList.toggle("active", active);
    button.setAttribute("aria-current", active ? "page" : "false");
  }
  if (editor.activePane === "settings" && el.canvasTitle) {
    el.canvasTitle.textContent = t(item.labelKey);
  }
  rememberEditorLocation();
}

function setActivePane(pane) {
  setupSettingsWorkspace();
  editor.activePane = pane === "settings" ? "settings" : "layout";
  const showLayout = editor.activePane === "layout";
  document.querySelectorAll(".layout-toolbar-only").forEach(node => node.classList.toggle("hidden", !showLayout));
  if (el.stackedSaveBtn) el.stackedSaveBtn.classList.toggle("hidden", showLayout);
  if (typeof closeAllDropdowns === "function") closeAllDropdowns();
  const textSizes = document.getElementById("layoutTextSizeDetails");
  if (textSizes) textSizes.open = false;
  renderHaDiagnosticsBanner();
  el.layoutPane.classList.toggle("hidden", !showLayout);
  el.settingsPane.classList.toggle("hidden", showLayout);
  if (el.settingsContentPane) {
    el.settingsContentPane.classList.toggle("hidden", showLayout);
  }
  if (el.canvasWrap) {
    el.canvasWrap.classList.toggle("hidden", !showLayout);
  }
  if (el.actionsPanel) {
    // The right column serves both modes: the layout inspector or the settings actions.
    el.actionsPanel.classList.remove("hidden");
    for (const child of el.actionsPanel.children) {
      const settingsOnly = child.id === "settingsSidebarActions";
      child.classList.toggle("hidden", settingsOnly ? showLayout : !showLayout);
    }
    // The toggle above shows both the page card and the widget Inspector; only one of them belongs to the selection.
    if (showLayout && typeof updateInspectorPanels === "function") updateInspectorPanels();
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
  rememberEditorLocation();
}

function setSectionCollapsed(sectionKey, collapsed) {
  if (sectionKey === "inspector") {
    editor.sectionCollapsed.inspector = false;
    el.inspectorSection?.classList.remove("collapsed");
    return;
  }
  const map = {
    pages: { section: el.pagesSection, toggle: el.togglePagesSection, headingKey: "layout.pages.heading" },
    widgets: { section: el.widgetsSection, toggle: el.toggleWidgetsSection, headingKey: "layout.widgets.heading" },
  };
  const entry = map[sectionKey];
  if (!entry || !entry.section || !entry.toggle) return;

  const nextCollapsed = Boolean(collapsed);
  editor.sectionCollapsed[sectionKey] = nextCollapsed;
  entry.section.classList.toggle("collapsed", nextCollapsed);
  entry.toggle.textContent = nextCollapsed ? "+" : "-";
  entry.toggle.setAttribute("aria-expanded", nextCollapsed ? "false" : "true");
  // The visible label is just "+"/"-"; a screen reader needs to know which section this collapses
  // and its current effect, not only its expanded state.
  const label = t(nextCollapsed ? "layout.section.expand" : "layout.section.collapse", { section: t(entry.headingKey) });
  entry.toggle.setAttribute("aria-label", label);
  entry.toggle.title = label;
}

function toggleSection(sectionKey) {
  setSectionCollapsed(sectionKey, !editor.sectionCollapsed[sectionKey]);
}

// One scale: night <= dimmed <= active. The slider that was moved pulls the others along.
function enforceBrightnessOrder(changed) {
  const active = el.settingsHardwareActiveBrightness;
  const dim = el.settingsHardwareDimBrightness;
  const night = el.settingsHardwareNightBrightness;
  if (!active || !dim || !night) return;
  let a = Number(active.value);
  let d = Number(dim.value);
  let n = Number(night.value);
  if (changed === "active") {
    if (d > a) d = a;
    if (n > d) n = d;
  } else if (changed === "dim") {
    if (d > a) d = a;
    if (n > d) n = d;
  } else if (n > d) {
    n = d;
  }
  setPercentSlider(dim, el.settingsHardwareDimBrightnessValue, d);
  setPercentSlider(night, el.settingsHardwareNightBrightnessValue, n, 0);
}

// "Now: 12 % (dimmed, room light 20 %)" from /api/version, while the settings page is on screen.
async function refreshBrightnessNow() {
  const box = document.getElementById("settingsBrightnessNow");
  if (!box || box.offsetParent === null) return;
  try {
    const response = await fetch("/api/version", { cache: "no-store" });
    const info = await response.json();
    const d = info && info.display;
    if (!d || typeof d.brightness_percent !== "number" || d.brightness_percent < 0) {
      box.textContent = "";
      return;
    }
    const state = ["night", "dimmed", "weather", "pages", "off", "test"].includes(d.state) ? d.state : "active";
    const parts = [t(`settings.hardware.now_${state}`)];
    if (typeof d.room_light_percent === "number" && d.room_light_percent < 100) {
      parts.push(t("settings.hardware.now_room", { percent: d.room_light_percent }));
    }
    box.textContent = `${t("settings.hardware.now", { percent: d.brightness_percent })} (${parts.join(", ")})`;
  } catch (error) {
    box.textContent = "";
  }
}

// "Current value: 42 %" right under the "Adapt to room light" checkbox, distinct from the
// general brightness hint above (settings.hardware.now_room), which is easy to miss because it
// is shown next to the brightness slider, not next to the checkbox that actually turns this on.
async function refreshCameraBrightnessStatus() {
  const box = el.settingsCameraBrightnessStatus;
  if (!box || box.offsetParent === null) return;
  if (!el.settingsHardwareAutoBrightness || !el.settingsHardwareAutoBrightness.checked) {
    box.textContent = "";
    return;
  }
  try {
    const response = await fetch("/api/version", { cache: "no-store" });
    const info = await response.json();
    const percent = info && info.display && info.display.room_light_percent;
    box.textContent = typeof percent === "number" ? t("settings.camera.brightness_status", { percent }) : "";
  } catch (error) {
    box.textContent = "";
  }
}

// "Test now (30s)": forces presence sampling even with the display active (POST
// /api/camera/test) and polls /api/version for a live "presence detected" readout while it
// runs, since ambient_camera.c otherwise only samples once the display is already dimmed.
let cameraTestTimer = null;
function stopCameraTest(message) {
  if (cameraTestTimer) {
    clearInterval(cameraTestTimer);
    cameraTestTimer = null;
  }
  if (el.settingsCameraTestStatus) {
    el.settingsCameraTestStatus.textContent = message || "";
  }
}
async function pollCameraTest() {
  try {
    const response = await fetch("/api/version", { cache: "no-store" });
    const info = await response.json();
    const cam = info && info.camera;
    if (!cam || !cam.test_active) {
      stopCameraTest("");
      return;
    }
    const key = cam.presence_detected ? "settings.camera.presence_detected" : "settings.camera.presence_not_detected";
    if (el.settingsCameraTestStatus) {
      el.settingsCameraTestStatus.textContent =
        `${t(key, { percent: cam.presence_motion_pct, threshold: cam.presence_threshold_pct })} -- ` +
        t("settings.camera.test_running", { seconds: cam.test_remaining_s });
    }
  } catch (error) {
    /* keep the last status showing; the next tick either recovers or the test times out server-side */
  }
}
async function startCameraTest() {
  if (cameraTestTimer) return; /* already running */
  try {
    const response = await fetch("/api/camera/test", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
    });
    if (!response.ok) throw new Error("request failed");
  } catch (error) {
    if (el.settingsCameraTestStatus) el.settingsCameraTestStatus.textContent = t("settings.camera.test_failed");
    return;
  }
  pollCameraTest();
  cameraTestTimer = setInterval(pollCameraTest, 1500);
}

function setPercentSlider(input, output, value, min = 1) {
  const percent = clamp(Number(value), min, 100);
  if (input) {
    input.value = String(percent);
  }
  if (output) {
    output.textContent = `${percent}%`;
  }
  return percent;
}

function normalizeTopbarSlot(value) {
  const slot = typeof value === "string" ? value : "";
  return ["", "none", "date", "weather", "stock"].includes(slot) ? (slot === "none" ? "" : slot) : "";
}

// The left items as the form shows them; an item repeated in a later box is dropped (each is shown once).
function topbarLeftSlotsFromForm() {
  const slots = [
    normalizeTopbarSlot(el.settingsTopbarSlot1?.value ?? "date"),
    normalizeTopbarSlot(el.settingsTopbarSlot2?.value ?? ""),
    normalizeTopbarSlot(el.settingsTopbarSlot3?.value ?? ""),
  ];
  return slots.map((slot, i) => (slot !== "" && slots.slice(0, i).includes(slot) ? "" : slot));
}

function deriveTopbarSettings(settings) {
  const topbar = settings.topbar || {};
  const slots = Array.isArray(topbar.left_slots) ? topbar.left_slots : [];
  return {
    slot1: normalizeTopbarSlot(slots[0] ?? "date"),
    slot2: normalizeTopbarSlot(slots[1] ?? ""),
    slot3: normalizeTopbarSlot(slots[2] ?? ""),
    weatherEntity: topbar.weather_entity_id || "",
    stockEntity: topbar.stock_entity_id || "",
  };
}

function applySectionCollapseState() {
  setSectionCollapsed("pages", editor.sectionCollapsed.pages);
  setSectionCollapsed("widgets", editor.sectionCollapsed.widgets);
  setSectionCollapsed("inspector", editor.sectionCollapsed.inspector);
}

// The saved panel text size into the layout actions (the legacy control ID remains for binding compatibility).
function applyTextStepToForm(ui) {
  if (!el.settingsUiTextStep) return;
  el.settingsUiTextStep.value = String(normalizeTextStep(ui ? ui.text_step : 0));
  updateTextStepInfo();
}

function renderSettings() {
  void loadAppVersion(); // refresh the device identity and build metadata when settings opens
  const settings = editor.settings || {};
  const wifi = settings.wifi || {};
  const ha = settings.ha || {};
  const mqtt = settings.mqtt || {};
  const topbar = deriveTopbarSettings(settings);
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
  renderSecretFields();
  if (el.settingsHaRestEnabled) {
    el.settingsHaRestEnabled.checked = ha.rest_enabled === true;
  }
  if (el.settingsMqttEnabled) {
    el.settingsMqttEnabled.checked = mqtt.enabled === true;
  }
  if (el.settingsMqttHost) {
    el.settingsMqttHost.value = mqtt.broker_host || "";
  }
  if (el.settingsMqttPort) {
    el.settingsMqttPort.value = mqtt.broker_port ? String(mqtt.broker_port) : "";
  }
  if (el.settingsMqttTls) {
    el.settingsMqttTls.checked = mqtt.tls_enabled === true;
  }
  if (el.settingsMqttUsername) {
    el.settingsMqttUsername.value = mqtt.username || "";
  }
  if (el.settingsMqttPassword) {
    el.settingsMqttPassword.value = "";
  }
  if (el.settingsMqttTopicPrefix) {
    el.settingsMqttTopicPrefix.value = mqtt.topic_prefix || "";
  }
  if (el.settingsDeviceRoom) {
    el.settingsDeviceRoom.value = (editor.settings.device && editor.settings.device.room) || "";
  }
  renderDeviceId();
  if (el.settingsMqttDeviceName) {
    el.settingsMqttDeviceName.value = mqtt.device_name || "";
  }
  if (el.settingsTopbarSlot1) {
    el.settingsTopbarSlot1.value = topbar.slot1;
  }
  if (el.settingsTopbarSlot2) {
    el.settingsTopbarSlot2.value = topbar.slot2;
  }
  if (el.settingsTopbarSlot3) {
    el.settingsTopbarSlot3.value = topbar.slot3;
  }
  // Only the 1280 px boards have room for a third item left of the clock.
  if (el.settingsTopbarSlot3Wrap) {
    el.settingsTopbarSlot3Wrap.hidden = !(editor.appScreenW >= 1000);
  }
  if (el.settingsTopbarWeatherEntity) {
    el.settingsTopbarWeatherEntity.value = topbar.weatherEntity;
  }
  if (el.settingsTopbarStockEntity) el.settingsTopbarStockEntity.value = topbar.stockEntity;
  setPercentSlider(
    el.settingsHardwareActiveBrightness,
    el.settingsHardwareActiveBrightnessValue,
    hardware.active_brightness ?? 100,
  );
  setPercentSlider(
    el.settingsHardwareDimBrightness,
    el.settingsHardwareDimBrightnessValue,
    hardware.dim_brightness ?? 50,
  );
  setPercentSlider(
    el.settingsHardwareNightBrightness,
    el.settingsHardwareNightBrightnessValue,
    hardware.night_brightness_percent ?? 1,
    0,
  );
  if (el.settingsHardwareDimTimeout) {
    el.settingsHardwareDimTimeout.value = String(clamp(Number(hardware.dim_timeout_seconds ?? 300), 5, 3600));
  }
  if (el.settingsSaverMode) el.settingsSaverMode.value = ["disabled", "off", "weather", "pages"].includes(hardware.saver_mode)
    ? hardware.saver_mode : "disabled";
  if (el.settingsSaverTimeout) el.settingsSaverTimeout.value = String(clamp(Number(hardware.saver_timeout_minutes ?? 10), 1, 120));
  if (el.settingsSaverWeatherMinutes) el.settingsSaverWeatherMinutes.value = String(clamp(Number(hardware.saver_weather_minutes ?? 30), 1, 120));
  if (el.settingsSaverPagesMinutes) el.settingsSaverPagesMinutes.value = String(clamp(Number(hardware.saver_pages_minutes ?? 30), 1, 120));
  if (el.settingsSaverPageInterval) el.settingsSaverPageInterval.value = String(clamp(Number(hardware.saver_page_interval_minutes ?? 5), 1, 30));
  if (el.settingsSaverPageWake) el.settingsSaverPageWake.value = hardware.saver_page_wake_home === false ? "stay" : "home";
  setPercentSlider(el.settingsSaverWeatherBrightness, el.settingsSaverWeatherBrightnessValue,
    hardware.saver_weather_brightness_percent ?? 5);
  if (el.settingsSaverWeatherEntity) el.settingsSaverWeatherEntity.value = hardware.saver_weather_entity_id || "";
  refreshInspectorEntityRows();
  if (el.settingsSaverPixelRefresh) el.settingsSaverPixelRefresh.checked = hardware.saver_pixel_refresh !== false;
  syncSaverFields();
  if (el.settingsSoundEnabled) el.settingsSoundEnabled.checked = hardware.sound_enabled === true;
  if (el.settingsSoundNightMute) el.settingsSoundNightMute.checked = hardware.sound_night_mute !== false;
  setPercentSlider(el.settingsSoundVolume, el.settingsSoundVolumeValue, hardware.sound_volume ?? DEFAULT_SOUND_VOLUME, 0);
  const soundSupported = hardware.sound_supported === true;
  editor.soundSupported = soundSupported;
  if (el.settingsNavButtons) setActiveSettingsSection(editor.activeSettingsSection); // hides or shows the Sound entry
  for (const input of [el.settingsSoundEnabled, el.settingsSoundNightMute, el.settingsSoundVolume, el.settingsSoundTestBtn]) {
    if (input) input.disabled = !soundSupported;
  }
  if (el.settingsSoundStatus) el.settingsSoundStatus.textContent = soundSupported ? "" : t("settings.sound.unsupported");
  if (el.settingsHardwareNightEnabled) el.settingsHardwareNightEnabled.checked = hardware.night_enabled !== false;
  if (el.settingsRotationGroup) el.settingsRotationGroup.hidden = hardware.rotation_supported !== true;
  if (el.settingsRotationRow) el.settingsRotationRow.hidden = hardware.rotation_supported !== true;
  if (el.settingsHardwareAutoRotation) el.settingsHardwareAutoRotation.checked = hardware.auto_rotation === true;
  if (el.settingsHardwareAutoBrightness) el.settingsHardwareAutoBrightness.checked = hardware.auto_brightness === true;
  if (el.settingsHardwarePresenceWake) el.settingsHardwarePresenceWake.checked = hardware.presence_wake === true;
  if (el.settingsHardwareNightPresenceWake) {
    el.settingsHardwareNightPresenceWake.checked = hardware.night_presence_wake === true;
  }
  if (el.settingsHardwarePresenceSensitivity) {
    const sens = clamp(Number(hardware.presence_sensitivity ?? 5), 1, 10);
    el.settingsHardwarePresenceSensitivity.value = String(sens);
    if (el.settingsHardwarePresenceSensitivityValue) el.settingsHardwarePresenceSensitivityValue.textContent = String(sens);
  }
  if (el.settingsHardwareNightStart) el.settingsHardwareNightStart.value = hardware.night_start || "22:00";
  if (el.settingsHardwareNightEnd) el.settingsHardwareNightEnd.value = hardware.night_end || "06:00";
  syncDisplayFields();
  el.settingsNtpServer.value = time.ntp_server || "";
  el.settingsTimezone.value = time.timezone || "";
  renderTimezonePresets();
  if (el.settingsLanguage) {
    el.settingsLanguage.value = normalizeUiLanguage(ui.language);
  }
  if (el.settingsUiStyle) {
    const style = normalizeUiStyle(ui.style);
    el.settingsUiStyle.value = style;
    applyUiStyleAttribute(style);
    updateStyleInfo();
  }
  applyTextStepToForm(ui);
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
    ...(HA_AUTH_REASONS.includes(ha.auth) ? [t(`ha_auth.${ha.auth}.title`)] : []),
    `${t("settings.info.token_stored")}: ${ha.access_token_set ? t("common.yes") : t("common.no")}`,
    `${t("settings.info.rest_fallback")}: ${ha.rest_enabled ? t("common.yes") : t("common.no")}`,
  ].join(" | ");

  if (el.settingsMqttInfo) {
    el.settingsMqttInfo.textContent = [
      `${t("settings.info.configured")}: ${mqtt.configured ? t("common.yes") : t("common.no")}`,
      `${t("settings.info.connected")}: ${mqtt.connected ? t("common.yes") : t("common.no")}`,
      `${t("settings.info.password_stored")}: ${mqtt.password_set ? t("common.yes") : t("common.no")}`,
    ].join(" | ");
  }
  editor.mqttCommandTopic = typeof mqtt.command_topic === "string" ? mqtt.command_topic : "";
  renderMqttAdvancedExample();

  if (el.settingsTopbarInfo) {
    el.settingsTopbarInfo.textContent = t("settings.topbar.info");
  }
  el.settingsTimeInfo.textContent = t("settings.time.info");
  if (el.settingsUiInfo) {
    el.settingsUiInfo.textContent = t("settings.ui.info");
  }
  if (el.settingsTranslationInfo && !el.settingsTranslationInfo.classList.contains("error")) {
    el.settingsTranslationInfo.textContent = t("settings.translation.info");
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
  }
  renderOtaUpdateCheck();
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
    resetSettingsDirty();
    renderRestoreNotice();
    if (!silent) {
      setStatus(t("status.settings_loaded"));
    }
    return editor.settings;
  } catch (err) {
    editor.settingsLoadError = String(err.message || err);
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
  el.settingsOtaProgressBar.parentElement?.classList.toggle("hidden", clamped <= 0); // the bar only shows while an update runs
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
    setOtaInfo(t("settings.ota.checking"));
  }
  try {
    const status = await apiGet("/api/ota/status");
    editor.ota.status = status;
    renderOtaStatus(status);
    applyUpdateStatus(status);
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

const STATUS_POLL_MS = 3000;
let statusTimer = null;

function formatKb(bytes) {
  if (!Number.isFinite(bytes) || bytes < 0) return "-";
  return `${Math.round(Number(bytes) / 1024)} KB`;
}

// Free-memory bar color: green with headroom, amber getting tight, red close to what this session's
// own investigation found actually starves the SDIO transport and the Room Import discovery gate
// (docs/development.md, "Memory and heap" -- int_largest pinned around 31 KB when things started dropping).
function memBarColor(freePercent) {
  if (freePercent < 15) return "#f85149";
  if (freePercent < 35) return "#d29922";
  return "#3fb950";
}

function updateMemBar(fillId, total, free) {
  const fill = document.getElementById(fillId);
  if (!fill) return;
  if (!(total > 0) || !Number.isFinite(free)) { fill.style.width = "0%"; return; }
  const pct = Math.max(0, Math.min(100, (free / total) * 100));
  fill.style.width = `${pct}%`;
  fill.style.backgroundColor = memBarColor(pct);
}

function chipFirmwareLabel(silicon) {
  const min = silicon.image_min_revision;
  const max = silicon.image_max_revision;
  if (!Number.isInteger(min) || !Number.isInteger(max) || min < 0 || max < min) {
    return t("settings.status.chip_firmware_unknown");
  }
  if (min === 100 && max === 199) return t("settings.status.chip_firmware_pre_v3");
  if (min === 300 && max === 399) return "v3.x";
  const rev = (n) => `v${Math.floor(n / 100)}.${n % 100}`;
  if (min > 300 && min <= 399 && max === 399) {
    return t("settings.status.chip_firmware_v3_from", { min: rev(min) });
  }
  return t("settings.status.chip_firmware_range", { min: rev(min), max: rev(max) });
}

async function refreshDeviceStatus() {
  const pane = document.getElementById("settingsDeviceSection");
  if (!pane || pane.classList.contains("hidden")) return;
  try {
    const response = await fetch("/api/version", { cache: "no-store" });
    if (!response.ok) return;
    const info = await response.json();
    const mem = info.memory || {};
    const cpu = info.cpu || {};
    const set = (id, text) => {
      const node = document.getElementById(id);
      if (node) node.textContent = text;
    };
    if (Array.isArray(cpu.load_percent)) set("settingsStatusCpu", cpu.load_percent.map((v) => `${v} %`).join(" / "));
    const ramKnown = Number.isFinite(mem.free_heap_internal) && mem.internal_total > 0;
    set("settingsStatusRam", ramKnown ? `${t("settings.status.free", { value: formatKb(mem.free_heap_internal) })}, ${t("settings.status.lowest", { value: formatKb(mem.min_free_heap_internal) })}, ${t("settings.status.of_total", { value: formatKb(mem.internal_total) })}` : "-");
    updateMemBar("settingsStatusRamBarFill", mem.internal_total, mem.free_heap_internal);
    const ramMinMarker = document.getElementById("settingsStatusRamBarMin");
    if (ramMinMarker) ramMinMarker.hidden = !ramKnown || !Number.isFinite(mem.min_free_heap_internal);
    if (ramMinMarker && !ramMinMarker.hidden) {
      const minPct = Math.max(0, Math.min(100, (mem.min_free_heap_internal / mem.internal_total) * 100));
      ramMinMarker.style.left = `${minPct}%`;
      ramMinMarker.title = t("settings.status.lowest", { value: formatKb(mem.min_free_heap_internal) });
    }
    if (mem.psram_total > 0 && Number.isFinite(mem.free_psram)) {
      set("settingsStatusPsram", `${t("settings.status.free", { value: formatKb(mem.free_psram) })}, ${t("settings.status.of_total", { value: formatKb(mem.psram_total) })}`);
      updateMemBar("settingsStatusPsramBarFill", mem.psram_total, mem.free_psram);
    } else { set("settingsStatusPsram", "-"); updateMemBar("settingsStatusPsramBarFill", 0, 0); }
    if (typeof info.reset_reason === "string") {
      const key = `settings.status.rr.${info.reset_reason}`;
      const text = t(key);
      set("settingsStatusReset", text === key ? info.reset_reason : text);
    }
    if (Number.isFinite(info.uptime_s)) {
      const s = Math.floor(info.uptime_s);
      set("settingsStatusUptime", `${Math.floor(s / 86400)} d ${Math.floor((s % 86400) / 3600)} h ${Math.floor((s % 3600) / 60)} min`);
    }
    const silicon = info.silicon;
    if (silicon && Number.isFinite(silicon.chip_revision)) {
      const rev = (n) => `v${Math.floor(n / 100)}.${n % 100}`;
      set("settingsStatusChip", t("settings.status.chip_value", {
        chip: silicon.chip_revision_text || rev(silicon.chip_revision),
        firmware: chipFirmwareLabel(silicon),
      }));
    }
    const boot = info.bootloader;
    if (boot && boot.built !== undefined) {
      set("settingsStatusBootloader", t(boot.rollback ? "settings.status.bootloader_ok" : "settings.status.bootloader_old"));
    } else {
      set("settingsStatusBootloader", t("settings.status.bootloader_unknown"));
    }
    const dump = info.coredump;
    renderCrashBanner(dump);
    if (el.coredumpUnsupportedHint) {
      // No coredump object at all (a failed status read) is shown like an unsupported one.
      const unsupported = !dump || dump.supported === false;
      el.coredumpUnsupportedHint.hidden = !unsupported;
      if (unsupported) {
        el.coredumpUnsupportedHint.textContent = t("settings.status.coredump_unsupported");
      }
    }
  } catch (_) {}
}

const REBOOT_LABEL_KEYS = {
  "settings restart": "settings.reboot.label.settings",
  "firmware update": "settings.reboot.label.ota",
  "factory reset": "settings.reboot.label.factory_reset",
  "coprocessor update": "settings.reboot.label.c6_update",
  "network watchdog": "settings.reboot.label.net_watchdog",
  "HTTP watchdog": "settings.reboot.label.http_watchdog",
  "panic": "settings.reboot.label.panic",
  "task watchdog": "settings.reboot.label.task_wdt",
  "interrupt watchdog": "settings.reboot.label.int_wdt",
  "watchdog": "settings.reboot.label.wdt",
  "power-on": "settings.reboot.label.power_on",
  "brownout": "settings.reboot.label.brownout",
  "software restart (untagged)": "settings.reboot.label.sw_untagged",
  "software restart": "settings.reboot.label.sw",
};

function formatRebootDuration(seconds) {
  const s = Math.max(0, Math.floor(seconds));
  if (s < 90) return `${s} s`;
  if (s < 5400) return `${Math.round(s / 60)} min`;
  if (s < 172800) return `${(s / 3600).toFixed(1)} h`;
  return `${(s / 86400).toFixed(1)} d`;
}

async function loadRebootHistory() {
  const box = document.getElementById("settingsRebootList");
  if (!box) return;
  try {
    const response = await fetch("/api/reboot-log", { cache: "no-store" });
    if (!response.ok) throw new Error(String(response.status));
    const data = await response.json();
    const entries = Array.isArray(data.entries) ? data.entries : [];
    box.textContent = "";
    if (!entries.length) {
      box.textContent = t("settings.reboot.empty");
      return;
    }
    for (const entry of entries) {
      const key = REBOOT_LABEL_KEYS[entry.label];
      const label = key && t(key) !== key ? t(key) : entry.label;
      // the wall time of the previous run's last refresh is when it ended; without a clock, name the boot instead
      const when = entry.previous_wall_epoch > 0
        ? new Date(entry.previous_wall_epoch * 1000).toLocaleString()
        : t("settings.reboot.boot", { n: entry.seq });
      const line = document.createElement("div");
      line.textContent = entry.previous_run_known
        ? t("settings.reboot.line", { when, label, ran: formatRebootDuration(entry.previous_uptime_s), ram: formatKb(entry.min_free_internal) })
        : t("settings.reboot.line_unknown", { when, label });
      box.appendChild(line);
    }
  } catch (_) {
    box.textContent = t("settings.reboot.empty");
  }
}

async function loadDeviceLog() {
  const view = document.getElementById("settingsLogText");
  if (!view) return "";
  try {
    const response = await fetch("/api/log", { cache: "no-store" });
    const text = response.ok ? await response.text() : `HTTP ${response.status}`;
    view.textContent = text;
    view.scrollTop = view.scrollHeight;
    return text;
  } catch (err) {
    view.textContent = String(err.message || err);
    return "";
  }
}

// After restoring a backup the secrets are missing: keep reminding until they are set again.
const RESTORE_NOTICE_KEY = "betta-restore-notice";

function renderRestoreNotice() {
  const banner = document.getElementById("restoreNoticeBanner");
  if (!banner) return;
  const pending = storageGet(RESTORE_NOTICE_KEY) === "1";
  const s = editor.settings;
  if (!pending || !s) {
    banner.hidden = true;
    return;
  }
  const missing = [];
  if (s.wifi && s.wifi.password_set === false) missing.push("settings.restore_notice.wifi");
  if (s.ha && s.ha.access_token_set === false) missing.push("settings.restore_notice.token");
  if (s.mqtt && s.mqtt.enabled && s.mqtt.password_set === false) missing.push("settings.restore_notice.mqtt");
  if (!missing.length) {
    storageRemove(RESTORE_NOTICE_KEY);
    banner.hidden = true;
    return;
  }
  setTextById("restoreNoticeTitle", "settings.restore_notice.title");
  setTextById("restoreNoticeHint", "settings.restore_notice.hint");
  const list = document.getElementById("restoreNoticeList");
  if (list) {
    list.replaceChildren();
    for (const key of missing) {
      const li = document.createElement("li");
      li.textContent = t(key);
      list.appendChild(li);
    }
  }
  banner.hidden = false;
}

// ---- Alerts (doorbell): saved in the layout as "alerts" ----
const MAX_ALERTS = 4;
let alertDraft = null; // working copy of the alert list, kept until the layout is saved or reloaded
let alertDraftLayout = null;

function currentAlerts() {
  if (alertDraft === null || alertDraftLayout !== editor.layout) {
    alertDraftLayout = editor.layout;
    alertDraft = (Array.isArray(editor.layout?.alerts) ? editor.layout.alerts : []).map((alert) => ({ ...alert }));
  }
  return alertDraft;
}

function openAlertEntityPicker(input, domain, titleKey, titleFallback) {
  if (!input) return;
  openLightEntityPicker(`alert_${domain}`, {
    inspectorTarget: { input, domain, titleKey, titleFallback },
    configOverride: entityPickerConfigForDomain(domain, titleKey, titleFallback),
  });
}

function renderAlertList() {
  const box = document.getElementById("settingsAlertsList");
  if (!box) return;
  const alerts = currentAlerts();
  box.replaceChildren();
  alerts.forEach((alert, index) => {
    const card = document.createElement("div");
    card.className = "settings-box alert-fields";
    const field = (labelKey, key, placeholder, type = "text") => {
      const label = document.createElement("label");
      const span = document.createElement("span");
      span.textContent = t(labelKey);
      const input = document.createElement("input");
      input.type = type;
      input.value = alert[key] ?? "";
      input.placeholder = placeholder || "";
      input.addEventListener("input", () => { alert[key] = type === "number" ? Number(input.value || 0) : input.value; });
      input.addEventListener("change", () => { alert[key] = type === "number" ? Number(input.value || 0) : input.value.trim(); });
      label.append(span, input);
      return { label, input };
    };
    const withSearch = (built, domain, titleKey, fallback) => {
      const row = document.createElement("div");
      row.className = "entity-input-row";
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "btn small entity-picker-btn";
      btn.textContent = t("settings.alerts.search");
      btn.addEventListener("click", () => openAlertEntityPicker(built.input, domain, titleKey, fallback));
      built.input.addEventListener("change", () => { /* value copied by the field's own listener */ });
      built.label.replaceChildren(built.label.firstChild, row);
      row.append(built.input, btn);
      return built.label;
    };
    const entity = field("settings.alerts.entity", "entity_id", "event.doorbell_ding");
    const title = field("settings.alerts.title", "title", t("settings.alerts.default_title"));
    const message = field("settings.alerts.message", "message", t("settings.alerts.default_message"));
    message.label.className = "alert-message-field";
    const timeout = field("settings.alerts.timeout", "timeout_sec", "30", "number");
    const action = field("settings.alerts.action", "action_entity_id", "button.door_open");
    const actionLabel = field("settings.alerts.action_label", "action_label", t("settings.alerts.default_action"));
    const camera = field("settings.alerts.camera", "camera_entity_id", "camera.front_door");
    const soundLabel = document.createElement("label");
    const soundTitle = document.createElement("span");
    soundTitle.textContent = t("settings.sound.heading");
    const sound = document.createElement("select");
    for (const [value, key] of [["none", "none"], ["chime", "chime"], ["notify", "notify"]]) {
      const option = document.createElement("option"); option.value = value;
      option.textContent = t(`settings.sound.${key}`); sound.appendChild(option);
    }
    sound.value = alert.sound || "none";
    sound.addEventListener("change", () => { alert.sound = sound.value; });
    soundLabel.append(soundTitle, sound);
    // The picker writes into the input and fires "change": keep the draft in step.
    const syncAfterPicker = (built, key) => built.input.addEventListener("change", () => { alert[key] = built.input.value.trim(); });
    syncAfterPicker(entity, "entity_id");
    syncAfterPicker(action, "action_entity_id");
    syncAfterPicker(camera, "camera_entity_id");
    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "btn small danger";
    remove.textContent = t("settings.alerts.remove");
    remove.addEventListener("click", () => {
      alerts.splice(index, 1);
      renderAlertList();
    });
    card.append(
      withSearch(entity, "event", "entity_picker.title_doorbell", "Choose an event"),
      title.label, message.label, timeout.label, soundLabel,
      withSearch(action, "button", "settings.alerts.action", "Button entity"),
      actionLabel.label,
      withSearch(camera, "camera", "settings.alerts.camera", "Camera"),
      remove,
    );
    box.appendChild(card);
  });
}

// The time zone list: the preset that matches the stored POSIX string, else "custom" with the text field shown.
function renderTimezonePresets() {
  const select = el.settingsTimezonePreset;
  if (!select) return;
  const current = (el.settingsTimezone?.value || "").trim();
  const match = TIMEZONE_PRESETS.find((preset) => preset.tz === current);
  select.replaceChildren(
    ...TIMEZONE_PRESETS.map((preset) => new Option(t(`settings.time.preset.${preset.id}`), preset.id)),
    new Option(t("settings.time.custom"), "custom"),
  );
  select.value = match ? match.id : "custom";
  if (el.settingsTimezoneCustomRow) el.settingsTimezoneCustomRow.hidden = Boolean(match);
}

function onTimezonePresetChange() {
  const preset = TIMEZONE_PRESETS.find((p) => p.id === el.settingsTimezonePreset.value);
  if (el.settingsTimezoneCustomRow) el.settingsTimezoneCustomRow.hidden = Boolean(preset);
  if (!preset) {
    el.settingsTimezone.focus();
    return;
  }
  el.settingsTimezone.value = preset.tz;
  el.settingsTimezone.dispatchEvent(new Event("change", { bubbles: true })); // applies live, like typing it
}

function bindAlerts() {
  const add = document.getElementById("settingsAlertsAdd");
  const save = document.getElementById("settingsAlertsSave");
  const msg = document.getElementById("settingsAlertsMsg");
  if (add) {
    add.addEventListener("click", () => {
      const alerts = currentAlerts();
      if (alerts.length >= MAX_ALERTS) {
        if (msg) msg.textContent = t("settings.alerts.limit");
        return;
      }
      alerts.push({ entity_id: "", sound: "chime", title: t("settings.alerts.default_title"), message: t("settings.alerts.default_message"), timeout_sec: 30, action_entity_id: "", action_label: "" });
      renderAlertList();
    });
  }
  if (save) {
    save.addEventListener("click", async () => {
      try {
        const cleaned = currentAlerts()
          .filter((alert) => (alert.entity_id || "").trim())
          .map((alert) => {
            const out = { entity_id: alert.entity_id.trim(), title: (alert.title || "").trim(), message: (alert.message || "").trim(), timeout_sec: Math.max(0, Math.round(Number(alert.timeout_sec) || 0)) };
            if (["chime", "notify"].includes(alert.sound)) out.sound = alert.sound;
            if ((alert.camera_entity_id || "").trim()) out.camera_entity_id = alert.camera_entity_id.trim();
            if ((alert.action_entity_id || "").trim()) {
              out.action_entity_id = alert.action_entity_id.trim();
              if ((alert.action_label || "").trim()) out.action_label = alert.action_label.trim();
            }
            return out;
          });
        if (cleaned.length) editor.layout.alerts = cleaned;
        else delete editor.layout.alerts;
        alertDraft = null;
        await saveLayout();
        renderAlertList();
        if (msg) msg.textContent = t("settings.alerts.saved");
      } catch (err) {
        if (msg) msg.textContent = t("settings.alerts.failed") + String(err.message || err);
      }
    });
  }
}

// ---- Backup: layout plus settings (without secrets), themes and translations in one file ----
async function backupGet(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) throw new Error(`${path}: ${response.status}`);
  return response.json();
}

function settingsForBackup(s) {
  return {
    wifi: { ssid: s.wifi?.ssid || "", country_code: s.wifi?.country_code || "DE", bssid: s.wifi?.bssid || null },
    ha: { ws_url: s.ha?.ws_url || "", rest_enabled: !!s.ha?.rest_enabled },
    mqtt: {
      enabled: !!s.mqtt?.enabled,
      broker_host: s.mqtt?.broker_host || "",
      broker_port: s.mqtt?.broker_port || 1883,
      tls_enabled: !!s.mqtt?.tls_enabled,
      username: s.mqtt?.username || "",
      topic_prefix: s.mqtt?.topic_prefix || "betta_ha_panel",
      device_name: s.mqtt?.device_name || "",
    },
    device: { room: s.device?.room || "" },
    topbar: {
      left_slots: s.topbar?.left_slots || ["date", ""],
      weather_entity_id: s.topbar?.weather_entity_id || null,
      stock_entity_id: s.topbar?.stock_entity_id || null,
    },
    hardware: { ...(s.hardware || {}) },
    time: { ntp_server: s.time?.ntp_server || "", timezone: s.time?.timezone || "" },
    ui: { language: s.ui?.language || "de", style: s.ui?.style || UI_STYLE_DEFAULT, text_step: normalizeTextStep(s.ui?.text_step) },
    // Absent on a board without update support; restoring such a backup leaves the panel's own values.
    ...(s.ota ? { ota: { update_check: !!s.ota.update_check, include_prereleases: !!s.ota.include_prereleases, auto_install: !!s.ota.auto_install } } : {}),
  };
}

async function downloadBackup() {
  const msg = document.getElementById("settingsBackupMsg");
  try {
    if (msg) msg.textContent = t("settings.backup.creating");
    const rawLayout = await backupGet("/api/layout");
    const settings = await backupGet("/api/settings");
    const themeList = await backupGet("/api/themes");
    const customThemes = [];
    for (const theme of (themeList.themes || []).filter((item) => !item.builtin)) {
      customThemes.push(await backupGet(`/api/themes/get?id=${encodeURIComponent(theme.id)}`));
    }
    const languageList = await backupGet("/api/i18n/languages");
    const customLanguages = {};
    for (const lang of (languageList.languages || []).filter((item) => item.custom)) {
      customLanguages[lang.code] = await backupGet(`/api/i18n/custom?lang=${encodeURIComponent(lang.code)}`);
    }
    const backup = {
      format: "betta-panel-backup",
      version: 1,
      created: new Date().toISOString(),
      device: { id: settings.device?.id || "", room: settings.device?.room || "" },
      layout: rawLayout.layout || rawLayout,
      settings: settingsForBackup(settings),
      themes: { active_id: themeList.active_id || "", custom: customThemes },
      languages: { custom: customLanguages },
    };
    const blob = new Blob([JSON.stringify(backup, null, 2)], { type: "application/json" });
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    const day = new Date().toISOString().slice(0, 10);
    link.download = `betta-backup-${backup.device.id || "panel"}-${day}.json`;
    link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 2000);
    if (msg) msg.textContent = t("settings.backup.created");
  } catch (err) {
    if (msg) msg.textContent = t("settings.backup.failed") + String(err.message || err);
  }
}

async function backupSend(path, method, body) {
  const response = await fetch(path, { method, headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  if (!response.ok) {
    let detail = await response.text();
    try {
      const json = JSON.parse(detail);
      detail = (json.errors || []).join(", ") || json.error || detail;
    } catch (_) {}
    throw new Error(`${path}: ${detail}`);
  }
}

async function restoreBackup() {
  const msg = document.getElementById("settingsBackupMsg");
  const input = document.getElementById("settingsBackupFile");
  try {
    if (!input || !input.files || !input.files[0]) {
      if (msg) msg.textContent = t("settings.backup.no_file");
      return;
    }
    const backup = JSON.parse(await input.files[0].text());
    if (!backup || backup.format !== "betta-panel-backup" || !backup.layout || !backup.settings) {
      if (msg) msg.textContent = t("settings.backup.invalid");
      return;
    }
    if (!window.confirm(t("settings.backup.confirm"))) return;
    if (msg) msg.textContent = t("settings.backup.restoring");
    for (const [code, texts] of Object.entries(backup.languages?.custom || {})) {
      await backupSend(`/api/i18n/custom?lang=${encodeURIComponent(code)}`, "PUT", texts);
    }
    for (const theme of backup.themes?.custom || []) {
      await backupSend("/api/themes/custom", "PUT", theme);
    }
    if (backup.themes?.active_id) {
      await backupSend("/api/themes/active", "PUT", { id: backup.themes.active_id });
    }
    normalizeLayoutWidgets(backup.layout);
    await backupSend("/api/layout", "PUT", backup.layout);
    await backupSend("/api/settings", "PUT", { ...backup.settings, reboot: true });
    storageSet(RESTORE_NOTICE_KEY, "1");
    if (msg) msg.textContent = t("settings.backup.restored");
  } catch (err) {
    if (msg) msg.textContent = t("settings.backup.failed") + String(err.message || err);
  }
}

function bindBackup() {
  bindAlerts();
  const down = document.getElementById("settingsBackupDownload");
  if (down) down.addEventListener("click", downloadBackup);
  const restore = document.getElementById("settingsBackupRestore");
  if (restore) restore.addEventListener("click", restoreBackup);
}

function bindFactoryReset() {
  bindBackup();
  const btn = document.getElementById("settingsFactoryBtn");
  const msg = document.getElementById("settingsFactoryMsg");
  if (!btn) return;
  btn.addEventListener("click", async () => {
    if (!window.confirm(t("settings.factory.confirm"))) return;
    btn.disabled = true;
    try {
      const response = await fetch("/api/factory-reset", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ confirm: "RESET" }),
      });
      if (!response.ok) throw new Error(await response.text());
      if (msg) msg.textContent = t("settings.factory.done");
    } catch (err) {
      btn.disabled = false;
      if (msg) msg.textContent = t("settings.factory.failed") + String(err.message || err);
    }
  });
}

// Restarts the panel without changing anything. It answers again after about 20 s; the page waits for that and refreshes the status.
function bindRestartPanel() {
  const btn = document.getElementById("settingsRestartBtn");
  const msg = document.getElementById("settingsRestartMsg");
  if (!btn) return;
  btn.addEventListener("click", async () => {
    btn.disabled = true;
    if (msg) msg.textContent = t("settings.restart.working");
    try {
      const response = await fetch("/api/restart", { method: "POST", headers: { "Content-Type": "application/json" } });
      if (!response.ok) throw new Error(await response.text());
    } catch (err) {
      btn.disabled = false;
      if (msg) msg.textContent = t("settings.restart.failed") + String(err.message || err);
      return;
    }
    const startedAt = Date.now();
    await new Promise((resolve) => setTimeout(resolve, 4000)); // the panel goes down first
    while (Date.now() - startedAt < 90000) {
      try {
        const response = await fetch("/api/version", { cache: "no-store" });
        if (response.ok) break;
      } catch (_) {
        // still restarting
      }
      await new Promise((resolve) => setTimeout(resolve, 2000));
    }
    btn.disabled = false;
    if (msg) msg.textContent = t("settings.restart.done");
    refreshDeviceStatus();
  });
}

function bindStatusControls() {
  bindFactoryReset();
  bindRestartPanel();
  if (!statusTimer) {
    statusTimer = setInterval(refreshDeviceStatus, STATUS_POLL_MS);
    refreshDeviceStatus();
  }
  const rebootDetails = document.getElementById("settingsRebootDetails");
  if (rebootDetails) rebootDetails.addEventListener("toggle", () => { if (rebootDetails.open) loadRebootHistory(); });
  const details = document.getElementById("settingsLogDetails");
  if (details) details.addEventListener("toggle", () => { if (details.open) loadDeviceLog(); });
  const refresh = document.getElementById("settingsLogRefresh");
  if (refresh) refresh.addEventListener("click", loadDeviceLog);
  const download = document.getElementById("settingsLogDownload");
  if (download) {
    download.addEventListener("click", async () => {
      const text = await loadDeviceLog();
      const link = document.createElement("a");
      link.href = URL.createObjectURL(new Blob([text], { type: "text/plain" }));
      link.download = "betta-panel-log.txt";
      link.click();
      setTimeout(() => URL.revokeObjectURL(link.href), 2000);
    });
  }
}

function renderSecurityStatus() {
  const on = !!(editor.settings && editor.settings.security && editor.settings.security.password_set);
  setTextById("settingsSecurityStatus", on ? "settings.security.status_on" : "settings.security.status_off");
  const clear = document.getElementById("settingsSecurityClear");
  if (clear) clear.hidden = !on;
}

async function saveAdminPassword(password) {
  const msg = document.getElementById("settingsSecurityMsg");
  try {
    await putSettings({ reboot: false, security: { admin_password: password } });
    editor.settings.security = { ...(editor.settings.security || {}), password_set: password !== "" };
    for (const id of ["settingsSecurityNew", "settingsSecurityRepeat"]) {
      const input = document.getElementById(id);
      if (input) input.value = "";
    }
    renderSecurityStatus();
    if (msg) msg.textContent = t(password ? "settings.security.saved" : "settings.security.removed");
  } catch (err) {
    if (msg) msg.textContent = String(err.message || err);
  }
}

function bindSecurityControls() {
  const setBtn = document.getElementById("settingsSecuritySet");
  const clearBtn = document.getElementById("settingsSecurityClear");
  const msg = document.getElementById("settingsSecurityMsg");
  if (setBtn) {
    setBtn.addEventListener("click", () => {
      const first = document.getElementById("settingsSecurityNew").value;
      const second = document.getElementById("settingsSecurityRepeat").value;
      if (first.length < 6) {
        if (msg) msg.textContent = t("settings.security.short");
        return;
      }
      if (first !== second) {
        if (msg) msg.textContent = t("settings.security.mismatch");
        return;
      }
      saveAdminPassword(first);
    });
  }
  if (clearBtn) {
    clearBtn.addEventListener("click", () => {
      if (window.confirm(t("settings.security.confirm_clear"))) saveAdminPassword("");
    });
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
  try {
    return await response.json(); // { ok, rebooting }
  } catch (_) {
    return {};
  }
}

// Unsaved settings. Display, appearance, language, time zone and the top bar are stored and applied as
// they change; only Wi-Fi, Home Assistant, MQTT, the time server and the room wait for "Save", and only
// those restart the panel (settings_restart.c). The Save button is highlighted while one of them
// differs from what the panel has stored, and leaving the editor asks first, like unsaved layout edits.
function settingsSaveFields() {
  return [el.settingsWifiSsid, el.settingsWifiPassword, el.settingsWifiCountryCode, el.settingsWifiBssid,
    el.settingsHaUrl, el.settingsHaToken, el.settingsHaRestEnabled, el.settingsMqttEnabled, el.settingsMqttHost,
    el.settingsMqttPort, el.settingsMqttTls, el.settingsMqttUsername, el.settingsMqttPassword,
    el.settingsMqttTopicPrefix, el.settingsMqttDeviceName, el.settingsDeviceRoom, el.settingsNtpServer].filter(Boolean);
}

function settingsFormSnapshot() {
  return JSON.stringify(settingsSaveFields().map((input) => (input.type === "checkbox" ? input.checked : input.value)));
}

function updateSettingsDirty() {
  editor.settingsDirty = editor.settingsBaseline !== undefined && settingsFormSnapshot() !== editor.settingsBaseline;
  if (el.saveSettingsBtn) el.saveSettingsBtn.classList.toggle("unsaved-highlight", editor.settingsDirty);
}

function resetSettingsDirty() {
  editor.settingsBaseline = settingsFormSnapshot();
  updateSettingsDirty();
}

// Stored secrets (Wi-Fi password, access token, MQTT password): a status row with a Change
// button instead of an empty field; typing a new value works as before.
function secretFlag(path) {
  return path.split(".").reduce((node, key) => (node && typeof node === "object" ? node[key] : undefined), editor.settings) === true;
}

function setupSecretField(input) {
  if (input._secret) return input._secret;
  const row = document.createElement("div");
  row.className = "secret-editing-row";
  input.parentNode.insertBefore(row, input);
  row.appendChild(input);
  const cancel = document.createElement("button");
  cancel.type = "button";
  cancel.className = "btn small";
  row.appendChild(cancel);
  const stored = document.createElement("div");
  stored.className = "secret-stored";
  const badge = document.createElement("span");
  badge.className = "secret-badge";
  const dots = document.createElement("span");
  dots.className = "secret-dots";
  dots.textContent = "\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022";
  const change = document.createElement("button");
  change.type = "button";
  change.className = "btn small";
  stored.append(badge, dots, change);
  row.parentNode.insertBefore(stored, row);
  const state = { row, stored, badge, change, cancel, editing: false };
  change.onclick = () => {
    state.editing = true;
    renderSecretField(input);
    input.focus();
  };
  cancel.onclick = () => {
    input.value = "";
    state.editing = false;
    renderSecretField(input);
  };
  input._secret = state;
  return state;
}

function renderSecretField(input) {
  const state = setupSecretField(input);
  const isSet = secretFlag(input.dataset.secretFlag || "");
  const showStored = isSet && !state.editing && input.value === "";
  state.stored.hidden = !showStored;
  state.row.hidden = showStored;
  state.badge.textContent = `\uD83D\uDD12 ${t("secret.stored")}`;
  state.change.textContent = t("secret.change");
  state.cancel.textContent = t("secret.cancel");
  state.cancel.hidden = !isSet;
  if (isSet) input.placeholder = t("secret.new_value");
}

function renderSecretFields() {
  for (const input of document.querySelectorAll("[data-secret-flag]")) {
    input._secret && (input._secret.editing = false);
    renderSecretField(input);
  }
}

// Dark (default) or light editor theme, remembered in the browser.
function applyEditorTheme(theme) {
  const value = theme === "light" ? "light" : "dark";
  document.documentElement.dataset.theme = value;
  storageSet("betta-editor-theme", value);
  const button = document.getElementById("themeToggleBtn");
  if (button) {
    button.textContent = value === "light" ? "\u263E" : "\u2600";
    const label = t(value === "light" ? "theme.to_dark" : "theme.to_light");
    button.title = label;
    button.setAttribute("aria-label", label);
  }
}

function initEditorTheme() {
  applyEditorTheme(storageGet("betta-editor-theme") || "dark");
  const button = document.getElementById("themeToggleBtn");
  if (button) {
    button.onclick = () => applyEditorTheme(document.documentElement.dataset.theme === "light" ? "dark" : "light");
  }
}

// Same rule as device_identity.c: lowercase ASCII, umlauts as ae/oe/ue/ss, other
// characters as single underscores, at most 16 characters.
function deviceSlug(room) {
  const map = { "\u00e4": "ae", "\u00f6": "oe", "\u00fc": "ue", "\u00df": "ss", "\u00c4": "ae", "\u00d6": "oe", "\u00dc": "ue" };
  let out = "";
  let sep = false;
  for (const ch of room) {
    const rep = map[ch] || (/[A-Za-z0-9]/.test(ch) ? ch.toLowerCase() : null);
    if (rep === null) {
      sep = out.length > 0;
      continue;
    }
    if (sep) out += "_";
    sep = false;
    out += rep;
    if (out.length >= 16) break;
  }
  return out.slice(0, 16);
}

// Device id at the top left: from the room typed in Settings, else what the panel reports.
function renderBuildTime() {
  if (!el.buildTimeLabel) return;
  const built = [editor.appBuildDate, editor.appBuildTime].filter(Boolean).join(" ");
  const commit = editor.appGitCommit || "";
  const text = commit ? `${built} (${commit})` : built;
  if (el.buildTimeValue) el.buildTimeValue.textContent = text;
  el.buildTimeLabel.hidden = built.length === 0;
}

// A short hardware tag next to the device id: which physical panel this editor session is talking to
// (useful with several panels open in different tabs). editor.appProject names the variant uniquely;
// screen size alone does not, paneljc and panel10 both report 1280x800. The full board string (with the
// paneljc panel revision detail) is the tooltip.
const BOARD_SHORT_LABELS = {
  "betta-ha-panel": "4\" (Waveshare)",
  "betta-ha-panel-10.1": "10.1\" (panel10)",
  "betta-ha-panel-jc": "10.1\" (JC8012P4A1)",
  "betta-ha-panel-jc-v2": "10.1\" (JC8012P4A1 V2)",
  "betta-ha-panel-tab5": "5\" (Tab5)",
};

function renderBoardLabel() {
  if (!el.boardLabel) return;
  const short = BOARD_SHORT_LABELS[editor.appProject] || "";
  if (el.boardLabelValue) el.boardLabelValue.textContent = short;
  el.boardLabel.title = editor.appBoard || "";
  el.boardLabel.hidden = short.length === 0;
}

function renderDeviceId() {
  renderBuildTime();
  if (!el.deviceIdLabel) return;
  const room = (el.settingsDeviceRoom?.value || "").trim();
  const slug = deviceSlug(room);
  const reported = editor.settings?.device?.id || "";
  const id = slug ? `betta_ha_panel_${slug}` : reported;
  if (el.deviceIdValue) el.deviceIdValue.textContent = id;
  el.deviceIdLabel.hidden = id.length === 0;
  el.deviceIdLabel.title = room ? room : id;
  const set = (id2, text) => {
    const node = document.getElementById(id2);
    if (node) node.textContent = text || "-";
  };
  set("settingsDeviceIdValue", id);
  set("settingsDeviceNameValue", room ? `BETTA ${room}` : (editor.settings?.device?.name || "BETTA HA Panel"));
}

// Brightness, night mode and the camera features apply on the panel without a
// reboot: send them on every change (debounced) with "reboot": false.
function syncSaverFields() {
  const mode = el.settingsSaverMode?.value || "disabled";
  if (el.settingsSaverTiming) el.settingsSaverTiming.hidden = mode === "disabled";
  if (el.settingsSaverWeatherFields) el.settingsSaverWeatherFields.hidden = mode !== "weather";
  if (el.settingsSaverPagesFields) el.settingsSaverPagesFields.hidden = mode !== "pages";
  if (el.settingsSaverPixelRefreshRow) el.settingsSaverPixelRefreshRow.hidden = mode === "disabled";
  if (el.settingsSaverAdvanced) el.settingsSaverAdvanced.hidden = mode === "disabled";
  if (el.settingsSaverHint) el.settingsSaverHint.textContent = t(`settings.saver.hint_${mode}`);
}

// Hiding dependent controls keeps their saved values, so disabling and re-enabling
// a feature restores the user's schedule and calibration instead of resetting them.
function syncDisplayFields() {
  const night = el.settingsHardwareNightEnabled?.checked === true;
  if (el.settingsNightFields) el.settingsNightFields.hidden = !night;
  if (el.settingsPresenceFields) el.settingsPresenceFields.hidden = !el.settingsHardwarePresenceWake?.checked;
  if (el.settingsNightPresenceRow) el.settingsNightPresenceRow.hidden = !night;
  if (!el.settingsHardwareAutoBrightness?.checked && el.settingsCameraBrightnessStatus) {
    el.settingsCameraBrightnessStatus.textContent = "";
  }
}

function readSoundHardware() {
  return { sound_enabled: el.settingsSoundEnabled?.checked === true,
    sound_volume: clamp(Number(el.settingsSoundVolume?.value ?? DEFAULT_SOUND_VOLUME), 0, 100),
    sound_night_mute: el.settingsSoundNightMute?.checked !== false };
}

async function testChime() {
  if (!el.settingsSoundTestBtn || el.settingsSoundTestBtn.disabled) return;
  el.settingsSoundTestBtn.disabled = true;
  try {
    const response = await fetch("/api/sound/test", { method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ volume: readSoundHardware().sound_volume }) });
    if (!response.ok) throw new Error("request failed");
    if (el.settingsSoundStatus) el.settingsSoundStatus.textContent = t("settings.sound.queued");
  } catch (_) {
    if (el.settingsSoundStatus) el.settingsSoundStatus.textContent = t("settings.sound.failed");
  } finally { el.settingsSoundTestBtn.disabled = false; }
}

function readSaverHardware() {
  return {
    saver_mode: el.settingsSaverMode?.value || "disabled",
    saver_timeout_minutes: clamp(Number(el.settingsSaverTimeout?.value || 10), 1, 120),
    saver_weather_minutes: clamp(Number(el.settingsSaverWeatherMinutes?.value || 30), 1, 120),
    saver_pages_minutes: clamp(Number(el.settingsSaverPagesMinutes?.value || 30), 1, 120),
    saver_page_interval_minutes: clamp(Number(el.settingsSaverPageInterval?.value || 5), 1, 30),
    saver_page_wake_home: el.settingsSaverPageWake?.value !== "stay",
    saver_weather_brightness_percent: clamp(Number(el.settingsSaverWeatherBrightness?.value || 5), 1, 30),
    saver_weather_entity_id: el.settingsSaverWeatherEntity?.value.trim() || "",
    saver_pixel_refresh: el.settingsSaverPixelRefresh?.checked !== false,
  };
}

let hardwareLiveTimer = null;
function pushHardwareLive() {
  clearTimeout(hardwareLiveTimer);
  hardwareLiveTimer = setTimeout(async () => {
    const hardware = {
      active_brightness: clamp(Number(el.settingsHardwareActiveBrightness?.value || 100), 1, 100),
      dim_brightness: clamp(Number(el.settingsHardwareDimBrightness?.value || 50), 1, 100),
      dim_timeout_seconds: clamp(Number(el.settingsHardwareDimTimeout?.value || 300), 5, 3600),
      night_enabled: el.settingsHardwareNightEnabled ? el.settingsHardwareNightEnabled.checked : false,
      night_brightness_percent: clamp(Number(el.settingsHardwareNightBrightness?.value ?? 1), 0, 100),
      auto_rotation: el.settingsHardwareAutoRotation ? el.settingsHardwareAutoRotation.checked : false,
      auto_brightness: el.settingsHardwareAutoBrightness ? el.settingsHardwareAutoBrightness.checked : false,
      presence_wake: el.settingsHardwarePresenceWake ? el.settingsHardwarePresenceWake.checked : false,
      night_presence_wake: el.settingsHardwareNightPresenceWake ? el.settingsHardwareNightPresenceWake.checked : false,
      presence_sensitivity: clamp(Number(el.settingsHardwarePresenceSensitivity?.value || 5), 1, 10),
      night_start: el.settingsHardwareNightStart?.value || "22:00",
      night_end: el.settingsHardwareNightEnd?.value || "06:00",
      ...readSaverHardware(),
      ...readSoundHardware(),
    };
    try {
      await putSettings({ hardware, reboot: false });
    } catch (error) {
      setStatus(String(error.message || error), true);
    }
  }, 300);
}

// Language, appearance, time zone and the top bar also apply without a reboot: the panel
// rebuilds its pages when they change.
let uiLiveTimer = null;
function pushUiLive() {
  clearTimeout(uiLiveTimer);
  uiLiveTimer = setTimeout(async () => {
    const leftSlots = topbarLeftSlotsFromForm();
    const weatherEntity = el.settingsTopbarWeatherEntity?.value.trim() || "";
    const stockEntity = el.settingsTopbarStockEntity?.value.trim() || "";
    const payload = {
      ui: {
        language: normalizeUiLanguage(el.settingsLanguage?.value),
        style: el.settingsUiStyle ? el.settingsUiStyle.value : UI_STYLE_DEFAULT,
        text_step: el.settingsUiTextStep ? normalizeTextStep(el.settingsUiTextStep.value) : 0,
      },
      time: { timezone: el.settingsTimezone?.value.trim() || "" },
      topbar: { left_slots: leftSlots, weather_entity_id: weatherEntity || null, stock_entity_id: stockEntity || null },
      reboot: false,
    };
    if (!payload.time.timezone) delete payload.time;
    try {
      await putSettings(payload);
    } catch (error) {
      setStatus(String(error.message || error), true);
    }
  }, 400);
}

function bindHardwareLive() {
  for (const input of [el.settingsUiStyle, el.settingsUiTextStep, el.settingsLanguage, el.settingsTimezone, el.settingsTopbarSlot1,
    el.settingsTopbarSlot2, el.settingsTopbarSlot3, el.settingsTopbarWeatherEntity, el.settingsTopbarStockEntity]) {
    if (input) input.addEventListener("change", pushUiLive);
  }
  if (el.settingsUiTextStep) el.settingsUiTextStep.addEventListener("change", () => {
    updateTextStepInfo();
    renderCanvas();
  });
  if (el.settingsTimezonePreset) el.settingsTimezonePreset.addEventListener("change", onTimezonePresetChange);
  if (el.settingsTimezone) el.settingsTimezone.addEventListener("input", renderTimezonePresets);
  const inputs = [
    el.settingsSoundVolume,
    el.settingsHardwareActiveBrightness,
    el.settingsHardwareDimBrightness,
    el.settingsHardwareNightBrightness,
    el.settingsHardwarePresenceSensitivity,
    el.settingsSaverWeatherBrightness,
  ];
  for (const input of inputs) {
    if (input) input.addEventListener("input", pushHardwareLive);
  }
  const changes = [
    el.settingsSoundEnabled, el.settingsSoundNightMute,
    el.settingsHardwareDimTimeout,
    el.settingsHardwareAutoRotation,
    el.settingsHardwareAutoBrightness,
    el.settingsHardwarePresenceWake,
    el.settingsHardwareNightPresenceWake,
    el.settingsHardwareNightEnabled,
    el.settingsHardwareNightStart,
    el.settingsHardwareNightEnd,
    el.settingsSaverMode,
    el.settingsSaverTimeout,
    el.settingsSaverWeatherMinutes,
    el.settingsSaverPagesMinutes,
    el.settingsSaverPageInterval,
    el.settingsSaverPageWake,
    el.settingsSaverWeatherEntity,
    el.settingsSaverPixelRefresh,
  ];
  for (const input of changes) {
    if (input) input.addEventListener("change", pushHardwareLive);
  }
  for (const input of [el.settingsHardwareNightEnabled, el.settingsHardwarePresenceWake, el.settingsHardwareAutoBrightness]) {
    if (input) input.addEventListener("change", syncDisplayFields);
  }
  if (el.settingsSoundTestBtn) el.settingsSoundTestBtn.addEventListener("click", testChime);
  if (el.settingsSoundVolume) el.settingsSoundVolume.addEventListener("input", () =>
    setPercentSlider(el.settingsSoundVolume, el.settingsSoundVolumeValue, el.settingsSoundVolume.value, 0));
  if (el.settingsSaverMode) el.settingsSaverMode.addEventListener("change", () => {
    if (el.settingsSaverMode.value === "weather" && !el.settingsSaverWeatherEntity?.value &&
        el.settingsTopbarWeatherEntity?.value) {
      el.settingsSaverWeatherEntity.value = el.settingsTopbarWeatherEntity.value;
    }
    syncSaverFields();
  });
  if (el.settingsSaverWeatherBrightness) el.settingsSaverWeatherBrightness.addEventListener("input", () =>
    setPercentSlider(el.settingsSaverWeatherBrightness, el.settingsSaverWeatherBrightnessValue,
      el.settingsSaverWeatherBrightness.value));
}

// The three "Reset to defaults" buttons: write the firmware's own defaults into the form fields and
// save through the same debounced push the fields already use on every normal edit, so this is not a
// second, separate save path to keep in sync. Values mirror runtime_settings_set_defaults() (settings/
// runtime_settings.c) and its APP_DISPLAY_* constants (app_config.h) exactly -- keep them matching if
// either changes.
function resetDisplayDefaults() {
  if (typeof stopBrightnessTest === "function" && (brightnessTestState.active || brightnessTestBusy)) {
    void stopBrightnessTest();
  }
  // JC enables camera sensing by default; Tab5 offers it but starts with it off until device calibration.
  const cameraOnByDefault = editor.appProject === "betta-ha-panel-jc" || editor.appProject === "betta-ha-panel-jc-v2";
  setPercentSlider(el.settingsHardwareActiveBrightness, el.settingsHardwareActiveBrightnessValue, 100);
  setPercentSlider(el.settingsHardwareDimBrightness, el.settingsHardwareDimBrightnessValue, 50);
  setPercentSlider(el.settingsHardwareNightBrightness, el.settingsHardwareNightBrightnessValue, 1, 0);
  if (el.settingsHardwareDimTimeout) el.settingsHardwareDimTimeout.value = "300";
  if (el.settingsSaverMode) el.settingsSaverMode.value = "disabled";
  if (el.settingsSaverTimeout) el.settingsSaverTimeout.value = "10";
  if (el.settingsSaverWeatherMinutes) el.settingsSaverWeatherMinutes.value = "30";
  if (el.settingsSaverPagesMinutes) el.settingsSaverPagesMinutes.value = "30";
  if (el.settingsSaverPageInterval) el.settingsSaverPageInterval.value = "5";
  if (el.settingsSaverPageWake) el.settingsSaverPageWake.value = "home";
  setPercentSlider(el.settingsSaverWeatherBrightness, el.settingsSaverWeatherBrightnessValue, 5);
  if (el.settingsSaverWeatherEntity) el.settingsSaverWeatherEntity.value = "";
  if (el.settingsSaverPixelRefresh) el.settingsSaverPixelRefresh.checked = true;
  syncSaverFields();
  if (el.settingsHardwareNightEnabled) el.settingsHardwareNightEnabled.checked = true;
  if (el.settingsHardwareNightStart) el.settingsHardwareNightStart.value = "22:00";
  if (el.settingsHardwareNightEnd) el.settingsHardwareNightEnd.value = "06:00";
  if (el.settingsHardwareAutoRotation) el.settingsHardwareAutoRotation.checked = editor.appProject === "betta-ha-panel-tab5";
  if (el.settingsHardwareAutoBrightness) el.settingsHardwareAutoBrightness.checked = cameraOnByDefault;
  if (el.settingsHardwarePresenceWake) el.settingsHardwarePresenceWake.checked = cameraOnByDefault;
  if (el.settingsHardwareNightPresenceWake) el.settingsHardwareNightPresenceWake.checked = false;
  if (el.settingsHardwarePresenceSensitivity) {
    el.settingsHardwarePresenceSensitivity.value = "5";
    if (el.settingsHardwarePresenceSensitivityValue) el.settingsHardwarePresenceSensitivityValue.textContent = "5";
  }
  syncDisplayFields();
  pushHardwareLive();
  setStatus(t("settings.reset.done"));
}

function resetAppearanceDefaults() {
  if (el.settingsUiStyle) {
    el.settingsUiStyle.value = UI_STYLE_DEFAULT;
    el.settingsUiStyle.dispatchEvent(new Event("change", { bubbles: true }));
  }
  setStatus(t("settings.reset.done"));
}

function resetTopbarDefaults() {
  if (el.settingsTopbarSlot1) el.settingsTopbarSlot1.value = "date";
  if (el.settingsTopbarSlot2) el.settingsTopbarSlot2.value = "";
  if (el.settingsTopbarSlot3) el.settingsTopbarSlot3.value = "";
  if (el.settingsTopbarWeatherEntity) el.settingsTopbarWeatherEntity.value = "";
  if (el.settingsTopbarStockEntity) el.settingsTopbarStockEntity.value = "";
  pushUiLive();
  setStatus(t("settings.reset.done"));
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
  if (editor.provisioningLanguage) {
    payload.ui = { language: editor.provisioningLanguage };
  }
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
  await putSettings(payload);
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
  const mqttEnabled = Boolean(el.settingsMqttEnabled?.checked);
  const mqttHost = el.settingsMqttHost?.value.trim() || "";
  const mqttPort = clamp(Number(el.settingsMqttPort?.value || 1883), 1, 65535);
  const mqttTls = Boolean(el.settingsMqttTls?.checked);
  const mqttUsername = el.settingsMqttUsername?.value.trim() || "";
  const mqttPassword = el.settingsMqttPassword?.value || "";
  const mqttTopicPrefix = el.settingsMqttTopicPrefix?.value.trim() || "";
  const mqttDeviceName = el.settingsMqttDeviceName?.value.trim() || "";
  const topbarLeftSlots = topbarLeftSlotsFromForm();
  const weatherEntity = el.settingsTopbarWeatherEntity?.value.trim() || "";
  const stockEntity = el.settingsTopbarStockEntity?.value.trim() || "";
  const hardwareActiveBrightness = clamp(Number(el.settingsHardwareActiveBrightness?.value || 100), 1, 100);
  const hardwareDimBrightness = clamp(Number(el.settingsHardwareDimBrightness?.value || 50), 1, 100);
  const hardwareNightBrightness = clamp(Number(el.settingsHardwareNightBrightness?.value ?? 1), 0, 100);
  const hardwareDimTimeout = clamp(Number(el.settingsHardwareDimTimeout?.value || 300), 5, 3600);
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
  // The panel refuses to send a stored secret to a new address: type the token / password again with it.
  const stored = editor.settings || {};
  if (stored.ha?.access_token_set && haUrl !== (stored.ha.ws_url || "") && haToken.length === 0) {
    setStatus(t("settings.ha.token_needed_for_url"), true);
    return;
  }
  if (stored.mqtt?.password_set && mqttHost !== (stored.mqtt.broker_host || "") && mqttPassword.length === 0) {
    setStatus(t("settings.mqtt.password_needed_for_host"), true);
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
    mqtt: {
      enabled: mqttEnabled,
      broker_host: mqttHost,
      broker_port: mqttPort,
      tls_enabled: mqttTls,
      username: mqttUsername,
      topic_prefix: mqttTopicPrefix,
      device_name: mqttDeviceName,
    },
    device: {
      room: el.settingsDeviceRoom?.value.trim() || "",
    },
    topbar: {
      left_slots: topbarLeftSlots,
      weather_entity_id: weatherEntity || null,
      stock_entity_id: stockEntity || null,
    },
    hardware: {
      active_brightness: hardwareActiveBrightness,
      dim_brightness: hardwareDimBrightness,
      dim_timeout_seconds: hardwareDimTimeout,
      night_enabled: el.settingsHardwareNightEnabled ? el.settingsHardwareNightEnabled.checked : false,
      night_brightness_percent: hardwareNightBrightness,
      auto_rotation: el.settingsHardwareAutoRotation ? el.settingsHardwareAutoRotation.checked : false,
      auto_brightness: el.settingsHardwareAutoBrightness ? el.settingsHardwareAutoBrightness.checked : false,
      presence_wake: el.settingsHardwarePresenceWake ? el.settingsHardwarePresenceWake.checked : false,
      night_presence_wake: el.settingsHardwareNightPresenceWake ? el.settingsHardwareNightPresenceWake.checked : false,
      presence_sensitivity: clamp(Number(el.settingsHardwarePresenceSensitivity?.value || 5), 1, 10),
      night_start: el.settingsHardwareNightStart?.value || "22:00",
      night_end: el.settingsHardwareNightEnd?.value || "06:00",
      ...readSaverHardware(),
      ...readSoundHardware(),
    },
    time: {
      ntp_server: ntpServer,
      timezone,
    },
    ui: {
      language,
      style: el.settingsUiStyle ? el.settingsUiStyle.value : UI_STYLE_DEFAULT,
    },
    reboot: "auto", // the panel restarts only for Wi-Fi, Home Assistant, MQTT, time server or room changes
  };
  if (wifiPassword.length > 0) {
    payload.wifi.password = wifiPassword;
  }
  if (haToken.length > 0) {
    payload.ha.access_token = haToken;
  }
  if (mqttPassword.length > 0) {
    payload.mqtt.password = mqttPassword;
  }

  setStatus(t("status.saving_settings"));
  const result = await putSettings(payload);
  resetSettingsDirty();
  setStatus(t(result?.rebooting === false ? "status.settings_saved" : "status.settings_saved_reboot"));
}
