/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
// Layout JSON and selection only: no HA readings, credentials or view settings.
const LAYOUT_HISTORY_STEPS = 50;
const LAYOUT_HISTORY_BYTES = 2 * 1024 * 1024;
const layoutHistory = {
  states: [], cursor: 0, bytes: 0, suspended: false,
  group: null, lastGroup: null, geometry: "",
};

function layoutJsonBytes(json) {
  let bytes = 0;
  for (const char of json) {
    const code = char.codePointAt(0);
    bytes += code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
  }
  return bytes;
}

function layoutHistoryState() {
  const json = layoutSnapshot();
  return json === null ? null : { json, bytes: layoutJsonBytes(json),
    pageId: editor.selectedPageId, widgetId: editor.selectedWidgetId };
}

function layoutHistoryControls() {
  const undo = document.getElementById("layoutUndoBtn");
  const redo = document.getElementById("layoutRedoBtn");
  if (undo) { undo.disabled = layoutHistory.cursor === 0; undo.title = t("layout.history.undo"); undo.setAttribute("aria-label", undo.title); }
  if (redo) { redo.disabled = layoutHistory.cursor >= layoutHistory.states.length - 1; redo.title = t("layout.history.redo"); redo.setAttribute("aria-label", redo.title); }
}

function layoutHistoryReset() {
  const state = layoutHistoryState();
  layoutHistory.states = state ? [state] : [];
  layoutHistory.cursor = 0;
  layoutHistory.bytes = state?.bytes || 0;
  layoutHistory.group = layoutHistory.lastGroup = null;
  layoutHistory.geometry = `${CANVAS_WIDTH}:${CANVAS_HEIGHT}`;
  layoutHistoryControls();
}

function layoutHistoryRecord() {
  if (layoutHistory.suspended || !editor.layout) return;
  const next = layoutHistoryState();
  if (!next) return;
  if (!layoutHistory.states.length || layoutHistory.geometry !== `${CANVAS_WIDTH}:${CANVAS_HEIGHT}`) {
    layoutHistoryReset();
    return;
  }
  const previous = layoutHistory.states[layoutHistory.cursor];
  if (next.json === previous.json) {
    previous.pageId = next.pageId;
    previous.widgetId = next.widgetId;
    return; // Selection and live state refreshes do not create history.
  }
  const discarded = layoutHistory.states.splice(layoutHistory.cursor + 1);
  layoutHistory.bytes -= discarded.reduce((sum, state) => sum + state.bytes, 0);
  if (layoutHistory.group && layoutHistory.group === layoutHistory.lastGroup && layoutHistory.cursor > 0) {
    layoutHistory.bytes -= previous.bytes;
    layoutHistory.states[layoutHistory.cursor] = next;
  } else {
    layoutHistory.states.push(next);
    layoutHistory.cursor++;
  }
  layoutHistory.bytes += next.bytes;
  layoutHistory.lastGroup = layoutHistory.group;
  while (layoutHistory.states.length > 1 &&
      (layoutHistory.states.length > LAYOUT_HISTORY_STEPS + 1 || layoutHistory.bytes > LAYOUT_HISTORY_BYTES)) {
    layoutHistory.bytes -= layoutHistory.states.shift().bytes;
    layoutHistory.cursor--;
  }
  layoutHistoryControls();
}

function layoutHistoryBoundary() {
  layoutHistoryRecord();
  layoutHistory.group = layoutHistory.lastGroup = null;
}

function layoutHistoryStep(delta) {
  if (editor.activePane !== "layout" || editor.canvasInteractionActive) return false;
  layoutHistoryBoundary();
  const index = layoutHistory.cursor + delta;
  if (index < 0 || index >= layoutHistory.states.length) return false;
  const state = layoutHistory.states[index];
  layoutHistory.suspended = true;
  try {
    editor.layout = JSON.parse(state.json);
    editor.selectedPageId = state.pageId;
    editor.selectedWidgetId = state.widgetId;
    const page = selectedPage();
    if (!page) editor.selectedPageId = editor.layout.pages[0]?.id || null;
    if (!selectedWidget()) editor.selectedWidgetId = null;
    hideUndoToast();
    closeWidgetActions();
    renderAll();
    layoutHistory.cursor = index;
  } finally {
    layoutHistory.suspended = false;
  }
  layoutHistoryControls();
  setStatus(t(delta < 0 ? "layout.history.undone" : "layout.history.redone"));
  return true;
}

function layoutShortcutAllowed(event) {
  if (editor.activePane !== "layout" || editor.canvasInteractionActive || event.defaultPrevented) return false;
  if (document.querySelector("dialog[open], .modal-overlay:not(.hidden)")) return false;
  const active = event.target || document.activeElement;
  return !(active?.isContentEditable || ["INPUT", "TEXTAREA", "SELECT"].includes(active?.tagName));
}

function bindLayoutHistory() {
  document.getElementById("layoutUndoBtn")?.addEventListener("click", () => layoutHistoryStep(-1));
  document.getElementById("layoutRedoBtn")?.addEventListener("click", () => layoutHistoryStep(1));
  document.addEventListener("keydown", (event) => {
    if (!layoutShortcutAllowed(event) || !(event.ctrlKey || event.metaKey) || event.altKey) return;
    const key = event.key.toLowerCase();
    if (key !== "z" && !(key === "y" && !event.shiftKey)) return;
    event.preventDefault();
    layoutHistoryStep(key === "y" || event.shiftKey ? 1 : -1);
  });
  document.addEventListener("focusin", (event) => {
    layoutHistoryBoundary();
    const field = event.target;
    const continuous = field?.tagName === "TEXTAREA" || (field?.tagName === "INPUT" &&
      !["checkbox", "radio", "button", "file", "search"].includes(field.type));
    if (editor.activePane === "layout" && continuous) layoutHistory.group = field;
  });
  document.addEventListener("focusout", (event) => {
    // The field's blur handler may apply a final normalized value first.
    Promise.resolve().then(() => {
      if (layoutHistory.group === event.target) layoutHistoryBoundary();
    });
  });
  layoutHistoryControls();
}
