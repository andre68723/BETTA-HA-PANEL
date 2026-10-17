/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 * One temporary device test for all three brightness levels. Settings keep their normal save path.
 */
let brightnessTestState = { active: false, kind: "active", remaining_s: 0 };
let brightnessTestBusy = false;
let brightnessTestError = "";
let brightnessTestErrorKind = "active";
let brightnessTestPollTimer = null;
let brightnessTestUpdateTimer = null;
let brightnessTestRequests = Promise.resolve();

function brightnessTestControls() {
  return [
    { kind: "active", input: el.settingsHardwareActiveBrightness, button: el.settingsActiveBrightnessTestBtn,
      status: el.settingsActiveBrightnessTestStatus },
    { kind: "dim", input: el.settingsHardwareDimBrightness, button: el.settingsDimBrightnessTestBtn,
      status: el.settingsDimBrightnessTestStatus },
    { kind: "night", input: el.settingsHardwareNightBrightness, button: el.settingsNightBrightnessTestBtn,
      status: el.settingsNightBrightnessTestStatus },
  ];
}

function renderBrightnessTest() {
  for (const control of brightnessTestControls()) {
    const active = brightnessTestState.active && brightnessTestState.kind === control.kind;
    if (control.button) {
      control.button.textContent = t(active ? "settings.display.test_stop" : "settings.display.test");
      control.button.title = t(active ? "settings.display.test_stop" : `settings.display.test_${control.kind}`);
      control.button.ariaLabel = control.button.title;
      control.button.disabled = brightnessTestBusy;
    }
    if (control.status) {
      const error = brightnessTestErrorKind === control.kind ? brightnessTestError : "";
      control.status.textContent = error || (active ? t(brightnessTestState.local_only
        ? "settings.display.test_local" : "settings.display.test_running", { seconds: brightnessTestState.remaining_s }) : "");
      control.status.hidden = !control.status.textContent;
    }
  }
}

function acceptBrightnessTestState(state) {
  if (typeof state?.active !== "boolean") throw new Error(t("settings.display.test_failed"));
  brightnessTestState = state;
  if (state.active && !brightnessTestPollTimer) {
    brightnessTestPollTimer = setInterval(() => void refreshBrightnessTest(), 1000);
  } else if (!state.active && brightnessTestPollTimer) {
    clearInterval(brightnessTestPollTimer);
    brightnessTestPollTimer = null;
  }
  renderBrightnessTest();
}

function requestBrightnessTest(payload) {
  // Serialize posts and status reads, so a slow slider response cannot undo Stop or a new test.
  const request = brightnessTestRequests.catch(() => {}).then(async () => {
    const options = payload ? {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload),
    } : { cache: "no-store" };
    let response;
    for (let attempt = 0; attempt < 3; attempt++) {
      response = await fetch("/api/display/test", options);
      // A busy display has not applied the request. Keep retries in the same queue slot.
      if (response.status !== 503 || attempt === 2) break;
      await new Promise(resolve => setTimeout(resolve, 300));
    }
    if (!response.ok) throw new Error(t(response.status === 404
      ? "settings.display.test_unavailable" : "settings.display.test_failed"));
    const state = await response.json();
    acceptBrightnessTestState(state);
    return state;
  });
  brightnessTestRequests = request;
  return request;
}

async function refreshBrightnessTest(silent = false) {
  try {
    await requestBrightnessTest();
    brightnessTestError = "";
  } catch (error) {
    if (!silent && brightnessTestState.active) {
      brightnessTestErrorKind = brightnessTestState.kind;
      brightnessTestError = t("settings.display.test_connection");
    }
  }
  renderBrightnessTest();
}

async function toggleBrightnessTest(kind) {
  if (brightnessTestBusy) return;
  if (brightnessTestState.active && brightnessTestState.kind === kind) return stopBrightnessTest();
  const control = brightnessTestControls().find(item => item.kind === kind);
  if (!control?.input) return;
  clearTimeout(brightnessTestUpdateTimer);
  brightnessTestBusy = true;
  brightnessTestError = "";
  brightnessTestErrorKind = kind;
  renderBrightnessTest();
  try {
    await requestBrightnessTest({ action: "start", kind, percent: clamp(Number(control.input.value), 0, 100) });
  } catch (error) {
    brightnessTestError = error.message || t("settings.display.test_failed");
  } finally {
    brightnessTestBusy = false;
    renderBrightnessTest();
  }
}

async function stopBrightnessTest() {
  clearTimeout(brightnessTestUpdateTimer);
  brightnessTestBusy = true;
  brightnessTestErrorKind = brightnessTestState.kind;
  brightnessTestError = "";
  renderBrightnessTest();
  try {
    await requestBrightnessTest({ action: "stop" });
  } catch (error) {
    brightnessTestError = error.message || t("settings.display.test_failed");
  } finally {
    brightnessTestBusy = false;
    renderBrightnessTest();
  }
}

function updateBrightnessTest() {
  if (!brightnessTestState.active) return;
  clearTimeout(brightnessTestUpdateTimer);
  brightnessTestUpdateTimer = setTimeout(async () => {
    if (!brightnessTestState.active) return;
    const control = brightnessTestControls().find(item => item.kind === brightnessTestState.kind);
    try {
      await requestBrightnessTest({ action: "update", percent: clamp(Number(control.input.value), 0, 100) });
    } catch (error) {
      brightnessTestErrorKind = brightnessTestState.kind;
      brightnessTestError = t("settings.display.test_connection");
      renderBrightnessTest();
    }
  }, 80);
}

function bindBrightnessTests() {
  for (const control of brightnessTestControls()) {
    if (control.button) control.button.onclick = () => toggleBrightnessTest(control.kind);
    // Any level can clamp another level, so re-read the tested level after the existing input handler.
    if (control.input) control.input.addEventListener("input", updateBrightnessTest);
  }
  if (el.settingsHardwareNightEnabled) el.settingsHardwareNightEnabled.addEventListener("change", () => {
    if (!el.settingsHardwareNightEnabled.checked && brightnessTestState.active && brightnessTestState.kind === "night") {
      void stopBrightnessTest();
    }
  });
  renderBrightnessTest();
  void refreshBrightnessTest(true); // Reattach after a reload without extending the device's deadline.
}
