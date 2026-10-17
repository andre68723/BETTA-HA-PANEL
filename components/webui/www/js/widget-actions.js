/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
function validateLayoutStructure(layout) {
  if (!layout || !Array.isArray(layout.pages)) throw new Error(t("layout.status.invalid_json"));
  if (!layout.pages.length) throw new Error(t("layout.status.at_least_one_page"));
  if (layout.pages.length > maxPages()) throw new Error(t("layout.status.max_pages", { max: maxPages() }));
  const pages = new Set(), widgets = new Set();
  const validId = (id) => typeof id === "string" && id.length > 0 && !id.includes("\0") && layoutJsonBytes(id) < 32;
  for (const page of layout.pages) {
    if (!page || typeof page !== "object" || !validId(page.id) || pages.has(page.id)) {
      throw new Error(t("layout.status.invalid_ids"));
    }
    pages.add(page.id);
    if (page.widgets === undefined) page.widgets = [];
    if (!Array.isArray(page.widgets)) throw new Error(t("layout.status.invalid_json"));
    if (page.widgets.length > MAX_WIDGETS_PER_PAGE) throw new Error(t("layout.status.max_widgets_page", { max: MAX_WIDGETS_PER_PAGE }));
    for (const widget of page.widgets) {
      if (!widget || !validId(widget.id) || widgets.has(widget.id) || typeof widget.type !== "string") {
        throw new Error(t("layout.status.invalid_ids"));
      }
      widgets.add(widget.id);
    }
  }
  const totalLimit = Math.min(224, maxPages() * MAX_WIDGETS_PER_PAGE);
  if (widgets.size > totalLimit) throw new Error(t("layout.status.max_widgets_total", { max: totalLimit }));
}

// Mirror the API's projection cleanup for the stored JSON budget. A newly added
// portrait card has no landscape alias; its current rect is a conservative byte
// estimate until firmware clamps the new landscape placement.
function layoutStoredBytes(layout) {
  const stored = JSON.parse(JSON.stringify(layout));
  for (const page of stored.pages) for (const widget of page.widgets || []) {
    if (stored.orientation === "portrait") {
      widget.portrait_rect = { ...widget.rect };
      if (widget.landscape_rect) widget.rect = widget.landscape_rect;
    }
    delete widget.landscape_rect;
  }
  delete stored.orientation;
  delete stored.canvas_w;
  delete stored.canvas_h;
  return layoutJsonBytes(JSON.stringify(stored));
}

function transferWidget(sourcePageId, widgetId, targetPageId, mode) {
  if (!["copy", "move"].includes(mode)) throw new Error(t("layout.transfer.invalid_target"));
  const source = editor.layout?.pages?.find((page) => page.id === sourcePageId);
  const target = editor.layout?.pages?.find((page) => page.id === targetPageId);
  const original = source?.widgets?.find((widget) => widget.id === widgetId);
  if (!original || !target || isFixedPage(source) || isFixedPage(target) || source === target) {
    throw new Error(t("layout.transfer.invalid_target"));
  }
  const candidate = JSON.parse(JSON.stringify(editor.layout));
  const from = candidate.pages.find((page) => page.id === sourcePageId);
  const to = candidate.pages.find((page) => page.id === targetPageId);
  const copy = JSON.parse(JSON.stringify(original));
  if (mode === "copy") copy.id = createWidgetIdForPage(target, copy.type);
  copy.rect = clampRectToCanvas(copy.rect || {}, widgetSizeType(copy));
  copy.rect = { ...copy.rect, ...findFreeSpot(to, copy.rect.w, copy.rect.h) };
  if (candidate.orientation === "portrait") copy.portrait_rect = { ...copy.rect };
  else if (mode === "copy") delete copy.portrait_rect; // Let firmware place the independent portrait copy.
  if (mode === "move") from.widgets = from.widgets.filter((widget) => widget.id !== widgetId);
  to.widgets.push(copy);
  normalizeLayoutWidgets(candidate);
  validateLayoutStructure(candidate);
  const bytes = layoutStoredBytes(candidate);
  if (bytes > MAX_LAYOUT_BYTES) throw new Error(t("layout.status.too_large", { size: bytes, max: MAX_LAYOUT_BYTES }));
  const overlaps = to.widgets.some((widget) => widget !== copy && widget.rect &&
    copy.rect.x < widget.rect.x + widget.rect.w && copy.rect.x + copy.rect.w > widget.rect.x &&
    copy.rect.y < widget.rect.y + widget.rect.h && copy.rect.y + copy.rect.h > widget.rect.y);
  layoutHistoryBoundary();
  editor.layout = candidate;
  editor.selectedPageId = targetPageId;
  editor.selectedWidgetId = copy.id;
  editor.widgetFilter = "";
  if (el.widgetSearchInput) el.widgetSearchInput.value = "";
  renderAll();
  setStatus(t(overlaps ? "layout.transfer.overlap" : mode === "move" ? "layout.transfer.moved" : "layout.transfer.copied",
    { name: original.title || original.id, page: target.title || target.id }));
  return copy;
}

function focusSelectedWidgetAction() {
  const button = [...document.querySelectorAll(".widget-actions-toggle")]
    .find((node) => node.dataset.widgetId === editor.selectedWidgetId);
  button?.focus({ preventScroll: true });
}

let widgetActions = null;
let widgetTransfer = null;

function closeWidgetActions(restoreFocus = false) {
  const menu = document.getElementById("widgetActionsMenu");
  if (menu) menu.classList.add("hidden");
  const anchor = widgetActions?.anchor;
  anchor?.setAttribute("aria-expanded", "false");
  if (restoreFocus && anchor?.isConnected) anchor.focus();
  widgetActions = null;
}

function openWidgetActions(pageId, widgetId, anchor, point) {
  const menu = document.getElementById("widgetActionsMenu");
  if (!menu) return;
  if (widgetActions?.anchor === anchor && !point) { closeWidgetActions(true); return; }
  closeWidgetActions();
  const page = editor.layout?.pages?.find((item) => item.id === pageId);
  if (isFixedPage(page) || !page?.widgets?.some((widget) => widget.id === widgetId)) return;
  layoutHistoryBoundary();
  widgetActions = { pageId, widgetId, anchor };
  menu.setAttribute("aria-label", t("layout.transfer.actions"));
  menu.replaceChildren();
  const targets = editor.layout.pages.filter((item) => item !== page && !isFixedPage(item));
  for (const [key, action] of [
    ["layout.widgets.duplicate", () => {
      editor.selectedPageId = pageId;
      duplicateWidget(widgetId);
      focusSelectedWidgetAction();
    }],
    ["layout.transfer.copy", () => openWidgetTransfer(pageId, widgetId, "copy", anchor)],
    ["layout.transfer.move", () => openWidgetTransfer(pageId, widgetId, "move", anchor)],
  ]) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "widget-action-item";
    button.setAttribute("role", "menuitem");
    button.textContent = t(key);
    button.disabled = key !== "layout.widgets.duplicate" && targets.length === 0;
    button.onclick = () => { closeWidgetActions(); action(); };
    menu.appendChild(button);
  }
  menu.classList.remove("hidden");
  anchor?.setAttribute("aria-expanded", "true");
  const rect = anchor?.getBoundingClientRect() || {};
  menu.style.left = `${Math.max(8, Math.min(point?.x ?? rect.left ?? 8, window.innerWidth - menu.offsetWidth - 8))}px`;
  menu.style.top = `${Math.max(8, Math.min(point?.y ?? rect.bottom ?? 8, window.innerHeight - menu.offsetHeight - 8))}px`;
  menu.querySelector("button:not(:disabled)")?.focus({ preventScroll: true });
}

function closeWidgetTransfer() {
  const dialog = document.getElementById("widgetTransferDialog");
  if (dialog) dialog.classList.add("hidden");
  const anchor = widgetTransfer?.anchor;
  widgetTransfer = null;
  if (anchor?.isConnected) anchor.focus();
  else focusSelectedWidgetAction();
}

function openWidgetTransfer(pageId, widgetId, mode, anchor = document.activeElement) {
  const source = editor.layout.pages.find((page) => page.id === pageId);
  const widget = source?.widgets?.find((item) => item.id === widgetId);
  const dialog = document.getElementById("widgetTransferDialog");
  const select = document.getElementById("widgetTransferPage");
  if (!widget || !dialog || !select) return;
  widgetTransfer = { pageId, widgetId, mode, anchor };
  document.getElementById("widgetTransferTitle").textContent = t(mode === "move" ? "layout.transfer.move" : "layout.transfer.copy");
  document.getElementById("widgetTransferName").textContent = widget.title || widget.id;
  document.getElementById("widgetTransferPageLabel").textContent = t("layout.transfer.target");
  document.getElementById("widgetTransferCancel").textContent = t("layout.transfer.cancel");
  document.getElementById("widgetTransferConfirm").textContent = t(mode === "move" ? "layout.transfer.move_button" : "layout.transfer.copy_button");
  document.getElementById("widgetTransferError").textContent = "";
  select.replaceChildren();
  for (const page of editor.layout.pages.filter((page) => page !== source && !isFixedPage(page))) {
    const option = document.createElement("option");
    option.value = page.id;
    option.textContent = page.title || page.id;
    select.appendChild(option);
  }
  document.getElementById("widgetTransferConfirm").disabled = select.options.length === 0;
  dialog.classList.remove("hidden");
  select.focus();
}

function bindWidgetActions() {
  document.getElementById("widgetTransferCancel")?.addEventListener("click", closeWidgetTransfer);
  document.getElementById("widgetTransferConfirm")?.addEventListener("click", () => {
    if (!widgetTransfer) return;
    try {
      const { pageId, widgetId, mode } = widgetTransfer;
      transferWidget(pageId, widgetId, document.getElementById("widgetTransferPage").value, mode);
      closeWidgetTransfer();
    } catch (err) {
      document.getElementById("widgetTransferError").textContent = err.message;
    }
  });
  document.getElementById("widgetTransferDialog")?.addEventListener("click", (event) => {
    if (event.target.id === "widgetTransferDialog") closeWidgetTransfer();
  });
  document.addEventListener("click", (event) => {
    if (!event.target.closest?.("#widgetActionsMenu, .widget-actions-toggle")) closeWidgetActions();
  });
  document.addEventListener("keydown", (event) => {
    if (widgetTransfer) {
      if (event.key === "Escape") { event.preventDefault(); closeWidgetTransfer(); }
      if (event.key === "Tab") {
        const controls = [...document.getElementById("widgetTransferDialog").querySelectorAll("select, button:not(:disabled)")];
        const next = (controls.indexOf(document.activeElement) + (event.shiftKey ? -1 : 1) + controls.length) % controls.length;
        event.preventDefault(); controls[next]?.focus();
      }
      return;
    }
    if (!widgetActions) return;
    if (event.key === "Escape") { event.preventDefault(); closeWidgetActions(true); }
    else if (["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) {
      const buttons = [...document.getElementById("widgetActionsMenu").querySelectorAll("button:not(:disabled)")];
      const direction = event.key === "ArrowUp" ? -1 : 1;
      const index = event.key === "Home" ? 0 : event.key === "End" ? buttons.length - 1 :
        (buttons.indexOf(document.activeElement) + direction + buttons.length) % buttons.length;
      event.preventDefault(); buttons[index]?.focus();
    } else if (event.key === "Tab") closeWidgetActions();
  });
  window.addEventListener?.("resize", () => closeWidgetActions());
  document.addEventListener("scroll", () => closeWidgetActions(), true);
}
