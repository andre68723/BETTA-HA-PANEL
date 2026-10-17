/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Firmware update check: the panel asks the fork's GitHub releases once a day (main/net/update_check.c)
 * and reports the result in /api/ota/status. This shows it as a banner above the workspace and in
 * Settings > Firmware, fills the OTA URL with the release's asset and binds the two settings.
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
const UPDATE_BANNER_DISMISS_KEY = "betta.updateBannerDismissed";
const UPDATE_CHECK_POLL_MS = 2000;
const UPDATE_CHECK_POLL_LIMIT = 20;

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

// Called with every /api/ota/status answer (loadOtaStatus): the "update" object is the panel's last check.
function applyUpdateStatus(status) {
  const update = status && typeof status.update === "object" && status.update ? status.update : null;
  editor.ota.update = update;
  editor.ota.autoUpdate = status && typeof status.auto_update === "object" ? status.auto_update : null;
  if (update?.available && typeof update.url === "string") {
    applyLatestOtaUrl(update.url);
  }
  renderUpdateBanner();
  renderOtaUpdateCheck();
}

function updateVersionLabel(update) {
  return update.prerelease ? t("update.version_prerelease", { version: update.version }) : update.version;
}

// The automatic install: the checkbox follows the panel, the line says why it waits or what the last automatic update came to.
function renderAutoUpdate() {
  const auto = editor.ota.autoUpdate;
  if (el.settingsOtaAutoInstall && auto) {
    el.settingsOtaAutoInstall.checked = auto.enabled === true;
  }
  const line = el.settingsOtaAutoInstallStatus;
  if (!line || !auto) return;
  const parts = [];
  if (auto.enabled && auto.state) {
    // The panel looks for the night window, not after every check: a release found by a manual check just now means it waits for the night.
    const state = auto.state === "wait_no_update" && editor.ota.update?.available ? "wait_outside_window" : auto.state;
    const key = `update.auto.state.${state}`;
    const text = t(key);
    parts.push(text === key ? state : text);
  }
  const version = auto.result_version || "";
  if (auto.result === "installed") parts.push(t("update.auto.result_installed", { version, time: formatUpdateCheckedAt(auto.result_at) }));
  if (auto.result === "rolled_back") parts.push(t("update.auto.result_rolled_back", { version }));
  if (auto.result === "failed") parts.push(t("update.auto.result_failed", { version }));
  if (auto.installing) parts.push(t("update.auto.installing", { version: auto.installing }));
  line.textContent = parts.join(" ");
  line.hidden = parts.length === 0;
  line.classList.toggle("error", auto.result === "rolled_back");
}

function formatUpdateCheckedAt(seconds) {
  const n = Number(seconds);
  if (!Number.isFinite(n) || n <= 0) return "";
  return new Date(n * 1000).toLocaleString(editor.i18nLanguage || undefined, {
    dateStyle: "short",
    timeStyle: "short",
  });
}

function renderUpdateBanner() {
  const banner = el.updateBanner;
  if (!banner) return;
  const update = editor.ota.update;
  const show = Boolean(update?.enabled && update.available && update.version) &&
    storageGet(UPDATE_BANNER_DISMISS_KEY) !== update.version;
  banner.hidden = !show;
  if (!show) return;
  if (el.updateBannerTitle) {
    el.updateBannerTitle.textContent = t("update.banner_title", { version: updateVersionLabel(update) });
  }
  if (el.updateBannerHint) {
    el.updateBannerHint.textContent = t("update.banner_hint", { current: editor.appVersion || "?" });
  }
}

// One line under the OTA buttons: what the panel's last check found.
function renderOtaUpdateCheck() {
  const line = el.settingsOtaUpdateCheck;
  if (!line) return;
  const update = editor.ota.update;
  if (el.settingsOtaUpdateCheckEnabled && update) {
    el.settingsOtaUpdateCheckEnabled.checked = update.enabled !== false;
  }
  if (el.settingsOtaIncludePrereleases && update) {
    el.settingsOtaIncludePrereleases.checked = update.include_prereleases === true;
  }
  renderAutoUpdate();
  if (el.checkUpdateNowBtn) {
    el.checkUpdateNowBtn.disabled = editor.ota.checkPolling || update?.state === "checking";
  }
  let text = "";
  let isError = false;
  if (!update) {
    text = "";
  } else if (update.state === "checking" || editor.ota.checkPolling) {
    text = t("update.checking");
  } else if (update.available) {
    text = t("update.available", { version: updateVersionLabel(update) });
  } else if (update.state === "error") {
    text = t("update.failed", { error: update.error || t("common.unknown_error") });
    isError = true;
  } else if (update.state === "ok" && update.found) {
    text = t("update.up_to_date", { version: updateVersionLabel(update), time: formatUpdateCheckedAt(update.checked_at) });
  } else if (update.state === "ok") {
    text = t("update.no_release", { time: formatUpdateCheckedAt(update.checked_at) });
  } else if (!update.enabled) {
    text = t("update.disabled");
  } else {
    text = t("update.pending");
  }
  line.textContent = text;
  line.hidden = text.length === 0;
  line.classList.toggle("error", isError);
}

async function checkForUpdateNow() {
  if (editor.ota.checkPolling) return;
  const before = editor.ota.update?.checked_at ?? 0;
  editor.ota.checkPolling = true;
  renderOtaUpdateCheck();
  try {
    const response = await fetch("/api/ota/check", { method: "POST", headers: { "Content-Type": "application/json" }, body: "{}" });
    if (!response.ok) {
      const payload = parseJsonMaybe(await response.text()) || {};
      throw new Error(payload.error || `${response.status} ${response.statusText}`);
    }
    // The panel answers at once and checks in its own task: poll until the result changes.
    let seenChecking = false;
    for (let i = 0; i < UPDATE_CHECK_POLL_LIMIT; i++) {
      await new Promise((resolve) => setTimeout(resolve, UPDATE_CHECK_POLL_MS));
      const status = await loadOtaStatus(true);
      const update = status?.update;
      if (!update) break;
      if (update.state === "checking") {
        seenChecking = true;
      } else if (update.checked_at !== before || ((seenChecking || i >= 2) && update.state === "error")) {
        break;
      }
    }
  } catch (err) {
    editor.ota.checkPolling = false;
    renderOtaUpdateCheck();
    setOtaInfo(t("update.failed", { error: err.message }), true);
    return;
  }
  editor.ota.checkPolling = false;
  renderOtaUpdateCheck();
}

async function saveUpdateCheckSettings() {
  const ota = {
    update_check: el.settingsOtaUpdateCheckEnabled ? el.settingsOtaUpdateCheckEnabled.checked : true,
    include_prereleases: el.settingsOtaIncludePrereleases ? el.settingsOtaIncludePrereleases.checked : false,
    auto_install: el.settingsOtaAutoInstall ? el.settingsOtaAutoInstall.checked : false,
  };
  try {
    await putSettings({ ota, reboot: false });
    if (editor.settings) editor.settings.ota = ota;
    // A changed setting clears the last result; a new check starts a few seconds later.
    await loadOtaStatus(true);
    if (ota.update_check) {
      setTimeout(() => void loadOtaStatus(true), 8000);
      setTimeout(() => void loadOtaStatus(true), 20000);
    }
  } catch (err) {
    setOtaInfo(t("settings.ota.request_failed", { error: err.message }), true);
  }
}

function openUpdateSettings() {
  setActivePane("settings");
  setActiveSettingsSection("settingsOtaSection");
  el.settingsOtaUrl?.scrollIntoView({ block: "center" });
}

function bindUpdateCheck() {
  if (el.checkUpdateNowBtn) {
    el.checkUpdateNowBtn.onclick = () => void checkForUpdateNow();
  }
  for (const box of [el.settingsOtaUpdateCheckEnabled, el.settingsOtaIncludePrereleases]) {
    if (box) box.onchange = () => void saveUpdateCheckSettings();
  }
  if (el.settingsOtaAutoInstall) {
    el.settingsOtaAutoInstall.onchange = () => {
      // Turning it on needs a conscious yes: the panel then changes its own firmware.
      if (el.settingsOtaAutoInstall.checked && !window.confirm(t("update.auto.confirm"))) {
        el.settingsOtaAutoInstall.checked = false;
        return;
      }
      void saveUpdateCheckSettings();
    };
  }
  if (el.updateBannerOpen) {
    el.updateBannerOpen.onclick = openUpdateSettings;
  }
  if (el.updateBannerDismiss) {
    el.updateBannerDismiss.onclick = () => {
      const version = editor.ota.update?.version;
      if (version) storageSet(UPDATE_BANNER_DISMISS_KEY, version);
      renderUpdateBanner();
    };
  }
}
