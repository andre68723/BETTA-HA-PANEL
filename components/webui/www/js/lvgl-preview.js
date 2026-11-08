/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Live view / Preview: one toolbar action and dialog for two environments.
 *  - Editor served by a panel: one GET /api/screenshot.jpg snapshot (JPEG from the panel's hardware encoder) of the panel's LVGL content, fetched on open and on Refresh.
 *  - Local editor (body[data-local-ui], enabled by the local startup): SDL/LVGL render of the edited page via /api/local/simulator.
 * Never switches the panel page, saves the layout or polls. Part of the web editor: classic scripts loaded in the order of
 * index.html, sharing one global scope (see docs/editor-source.md).
 */

const LIVE_VIEW_TIMEOUT_MS = 30000;

// Browser services behind one object so tests can replace them.
const liveViewEnv = {
  fetch: (...args) => fetch(...args),
  createObjectURL: (blob) => URL.createObjectURL(blob),
  revokeObjectURL: (url) => URL.revokeObjectURL(url),
  decode: (url) => new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve();
    image.onerror = () => reject(new Error("decode"));
    image.src = url;
  }),
  now: () => new Date(),
  setTimeout: (fn, ms) => setTimeout(fn, ms),
  clearTimeout: (id) => clearTimeout(id),
  newAbort: () => new AbortController(),
};

const liveView = {
  localConfig: null, // set by the local startup once it succeeded; the local action stays disabled until then
  load: null, // the single outstanding request, owned by its own timer, Blob URL and abort controller
  seq: 0,
  current: null, // {blob, url, time, name}: the image the dialog shows
  previous: false,
  dialog: null,
  trigger: null,
};

function liveViewMode() {
  return document.body?.dataset?.localUi ? "local" : "device";
}

function liveViewAvailable() {
  return liveViewMode() === "device" || liveView.localConfig !== null;
}

function liveViewKey(name) {
  return `live.${name}.${liveViewMode()}`;
}

function liveViewEl(id) {
  return document.getElementById(id);
}

function liveViewStatus(text, kind = "") {
  const node = liveViewEl("liveViewStatus");
  if (!node) return;
  node.textContent = text;
  node.dataset.kind = kind;
}

function liveViewRefreshUi() {
  const busy = liveView.load !== null;
  const refresh = liveViewEl("liveViewRefresh");
  if (refresh) refresh.disabled = busy;
  for (const id of ["liveViewStyle", "liveViewStep", "liveViewLang"]) {
    const control = liveViewEl(id);
    if (control) control.disabled = busy;
  }
  const download = liveViewEl("liveViewDownload");
  if (download) download.disabled = !liveView.current;
  const body = liveViewEl("liveViewBody");
  if (body) {
    body.classList.toggle("is-busy", busy);
    body.classList.toggle("is-previous", Boolean(liveView.current) && (busy || liveView.previous));
  }
  const image = liveViewEl("liveViewImage");
  if (image) image.hidden = !liveView.current;
}

function liveViewApplyTexts() {
  const button = liveViewEl("liveViewBtn");
  if (button) {
    const label = t(liveViewKey("button"));
    button.title = label;
    button.setAttribute("aria-label", label);
    const text = liveViewEl("liveViewBtnText");
    if (text) text.textContent = label;
  }
  const set = (id, key) => { const node = liveViewEl(id); if (node) node.textContent = t(key); };
  set("liveViewTitle", liveViewKey("title"));
  set("liveViewNote", liveViewKey("note"));
  set("liveViewRefresh", "live.refresh");
  set("liveViewDownload", "live.download");
  set("liveViewClose", "live.close");
  set("liveViewStyleLabel", "live.style");
  set("liveViewStepLabel", "live.step");
  set("liveViewLangLabel", "live.language");
  const image = liveViewEl("liveViewImage");
  if (image) image.alt = t(liveViewKey("alt"));
  const dialog = liveViewEl("liveViewDialog");
  if (dialog) dialog.dataset.mode = liveViewMode();
}

function liveViewFailure(error, load) {
  if (load.timedOut) return t("live.err.timeout", { seconds: LIVE_VIEW_TIMEOUT_MS / 1000 });
  if (error?.liveStatus) {
    const status = error.liveStatus;
    if (liveViewMode() === "local") return t("live.err.local", { detail: error.message });
    if (status === 503) return t("live.err.busy");
    if (status === 401 || status === 403) return t("live.err.auth");
    if (status === 404 || status === 405 || status === 501) return t("live.err.unsupported");
    return t("live.err.failed", { status });
  }
  if (error?.message === "decode") return t("live.err.decode");
  return t("live.err.network");
}

async function liveViewFetchBlob(load) {
  if (liveViewMode() === "local") {
    const page = editor.layout?.pages?.find((item) => item.id === editor.selectedPageId);
    const response = await liveViewEnv.fetch("/api/local/simulator", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      signal: load.controller.signal,
      body: JSON.stringify({
        layout: editor.layout, page_id: editor.selectedPageId,
        style: liveViewEl("liveViewStyle")?.value || "glow",
        text_step: Number(liveViewEl("liveViewStep")?.value || 0),
        language: liveViewEl("liveViewLang")?.value || "de",
      }),
    });
    if (!response.ok) {
      let detail = response.statusText || String(response.status);
      try { detail = (await response.json()).error || detail; } catch (_) { /* keep the status text */ }
      const error = new Error(detail);
      error.liveStatus = response.status;
      throw error;
    }
    load.caption = t("live.local.caption", { page: page?.title || editor.selectedPageId, variant: liveView.localConfig.variant });
    return response.blob();
  }
  const response = await liveViewEnv.fetch("/api/screenshot.jpg", {
    cache: "no-store", credentials: "same-origin", signal: load.controller.signal,
  });
  if (!response.ok) {
    const error = new Error(response.statusText || String(response.status));
    error.liveStatus = response.status;
    throw error;
  }
  return response.blob();
}

function liveViewFinish(load) {
  liveViewEnv.clearTimeout(load.timer);
  if (liveView.load === load) liveView.load = null;
}

// One request at a time. A load owns its timer, controller and Blob URL; a superseded or cancelled load only cleans up
// what it owns and never touches the dialog state of a newer load.
async function liveViewLoad() {
  if (liveView.load || !liveViewAvailable()) return;
  const load = { id: ++liveView.seq, controller: liveViewEnv.newAbort(), timer: null, url: null, timedOut: false, caption: "" };
  liveView.load = load;
  const device = liveViewMode() === "device";
  liveViewStatus(t(device ? "live.loading.device" : "live.loading.local"), "busy");
  liveViewRefreshUi();
  if (device) {
    load.timer = liveViewEnv.setTimeout(() => {
      if (liveView.load !== load) return;
      load.timedOut = true;
      liveView.load = null;
      load.controller.abort();
      liveView.previous = Boolean(liveView.current);
      liveViewStatus(liveViewFailure(null, load) + (liveView.current ? " " + t("live.previous") : ""), "error");
      liveViewRefreshUi();
    }, LIVE_VIEW_TIMEOUT_MS);
  }
  try {
    const blob = await liveViewFetchBlob(load);
    if (liveView.load !== load) return;
    load.url = liveViewEnv.createObjectURL(blob);
    await liveViewEnv.decode(load.url);
    if (liveView.load !== load) { liveViewEnv.revokeObjectURL(load.url); return; }
    const old = liveView.current;
    const time = liveViewEnv.now();
    liveView.current = { blob, url: load.url, time, name: `betta-panel-${liveViewStamp(time)}.jpg` };
    liveView.previous = false;
    liveViewRelease(old);
    const image = liveViewEl("liveViewImage");
    if (image) image.src = load.url;
    liveViewFinish(load);
    liveViewStatus(device ? t("live.fetched", { time: time.toLocaleTimeString() }) : load.caption, "ok");
  } catch (error) {
    if (load.url && !(liveView.current && liveView.current.url === load.url)) liveViewEnv.revokeObjectURL(load.url);
    if (liveView.load !== load) { liveViewEnv.clearTimeout(load.timer); return; }
    liveViewFinish(load);
    liveView.previous = Boolean(liveView.current);
    liveViewStatus(liveViewFailure(error, load) + (liveView.current ? " " + t("live.previous") : ""), "error");
  }
  liveViewRefreshUi();
}

function liveViewStamp(date) {
  const pad = (value) => String(value).padStart(2, "0");
  return `${date.getFullYear()}${pad(date.getMonth() + 1)}${pad(date.getDate())}-${pad(date.getHours())}${pad(date.getMinutes())}${pad(date.getSeconds())}`;
}

// Closing cancels the request in the browser; the device may still finish a capture it already started.
function liveViewCancel() {
  const load = liveView.load;
  if (!load) return;
  liveView.load = null;
  liveViewEnv.clearTimeout(load.timer);
  load.controller.abort();
}

// Frees a shown image. A download the browser may still be reading keeps its Blob URL a little longer.
const LIVE_VIEW_DOWNLOAD_GRACE_MS = 3000;
function liveViewRelease(shot) {
  if (!shot) return;
  if (shot.downloaded) liveViewEnv.setTimeout(() => liveViewEnv.revokeObjectURL(shot.url), LIVE_VIEW_DOWNLOAD_GRACE_MS);
  else liveViewEnv.revokeObjectURL(shot.url);
}

function liveViewDownload() {
  const shot = liveView.current;
  if (!shot) return;
  shot.downloaded = true;
  const link = document.createElement("a");
  link.href = shot.url;
  link.download = shot.name;
  document.body.appendChild(link);
  link.click();
  link.remove();
}

function liveViewClose() {
  liveViewCancel();
  // Nothing stays behind a closed dialog: the shown image, its Blob URL and the status are released; reopening takes a new shot.
  const shot = liveView.current;
  liveView.current = null;
  liveView.previous = false;
  liveViewRelease(shot);
  liveViewEl("liveViewImage")?.removeAttribute("src");
  liveViewStatus("", "");
  liveViewRefreshUi();
  liveViewEl("liveViewDialog")?.classList.add("hidden");
  if (liveView.trigger?.focus) liveView.trigger.focus();
}

function liveViewOpen() {
  if (!liveViewAvailable()) return;
  liveViewBuildDialog();
  liveViewApplyTexts();
  liveViewEl("liveViewDialog").classList.remove("hidden");
  liveViewRefreshUi();
  liveViewEl("liveViewClose")?.focus();
  void liveViewLoad();
}

function liveViewBuildDialog() {
  if (liveViewEl("liveViewDialog")) return;
  const dialog = document.createElement("div");
  dialog.id = "liveViewDialog";
  dialog.className = "live-view hidden";
  dialog.setAttribute("role", "dialog");
  dialog.setAttribute("aria-modal", "true");
  dialog.setAttribute("aria-labelledby", "liveViewTitle");
  dialog.innerHTML =
    '<div class="live-view-card"><div class="live-view-head"><strong id="liveViewTitle"></strong>' +
    '<span class="live-view-local-controls">' +
    '<label><span id="liveViewStyleLabel"></span> <select id="liveViewStyle"><option>glow</option><option>flat</option><option>glass</option><option>matte</option></select></label>' +
    '<label><span id="liveViewStepLabel"></span> <select id="liveViewStep"><option value="0">0</option><option value="1">1</option><option value="2">2</option></select></label>' +
    '<label><span id="liveViewLangLabel"></span> <select id="liveViewLang"><option value="de">de</option><option value="en">en</option></select></label></span>' +
    '<button type="button" class="btn small" id="liveViewRefresh"></button>' +
    '<button type="button" class="btn small" id="liveViewDownload" disabled></button>' +
    '<button type="button" class="btn small" id="liveViewClose"></button></div>' +
    '<p class="live-view-note" id="liveViewNote"></p><p class="live-view-status" id="liveViewStatus" role="status"></p>' +
    '<div class="live-view-body" id="liveViewBody"><img id="liveViewImage" hidden alt=""></div></div>';
  document.body.appendChild(dialog);
  liveViewEl("liveViewRefresh").onclick = () => { void liveViewLoad(); };
  liveViewEl("liveViewDownload").onclick = liveViewDownload;
  liveViewEl("liveViewClose").onclick = liveViewClose;
  for (const id of ["liveViewStyle", "liveViewStep", "liveViewLang"]) liveViewEl(id).onchange = () => { void liveViewLoad(); };
  dialog.addEventListener("click", (event) => { if (event.target === dialog) liveViewClose(); });
  dialog.addEventListener("keydown", (event) => { if (event.key === "Escape") liveViewClose(); });
}

// Called by the local startup after its configuration loaded and offers the render.
function liveViewEnableLocal(config) {
  if (config?.lvgl_preview !== true) return;
  liveView.localConfig = config;
  const button = liveViewEl("liveViewBtn");
  if (button) button.disabled = false;
  liveViewApplyTexts();
}

function initLiveView() {
  const button = liveViewEl("liveViewBtn");
  if (!button) return;
  liveView.trigger = button;
  button.disabled = !liveViewAvailable();
  liveViewApplyTexts();
  button.onclick = liveViewOpen;
}
