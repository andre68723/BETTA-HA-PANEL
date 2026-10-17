/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Dropdown menus
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
// ============================================================
// Simplified layout UI: + Add dropdowns for Pages / Widgets.
// The original buttons are kept hidden (.legacy-hidden) so every
// existing handler keeps working — menu items just click them.
// ============================================================
function initSimpleUiMenus() {
  bindDropdown("addPageMenuBtn", "addPageMenu");
  bindDropdown("layoutJsonMenuBtn", "layoutJsonMenu");

  const pageNormal = document.getElementById("addPageMenuNormal");
  if (pageNormal) {
    pageNormal.onclick = () => {
      closeAllDropdowns();
      if (el.addPageBtn) el.addPageBtn.click();
    };
  }
  const pageHouse = document.getElementById("addPageMenuHouse");
  if (pageHouse) {
    pageHouse.onclick = () => {
      closeAllDropdowns();
      addHousePage();
    };
  }
  if (el.houseImportBtn) el.houseImportBtn.onclick = () => void importHouseRooms();
  const pageRoom = document.getElementById("addPageMenuRoom");
  if (pageRoom) {
    pageRoom.onclick = () => {
      closeAllDropdowns();
      void openRoomImportPicker();
    };
  }
  if (el.roomImportCloseBtn) el.roomImportCloseBtn.onclick = () => closeRoomImportPicker();
  if (el.roomImportConfirmBtn) el.roomImportConfirmBtn.onclick = () => void confirmRoomImport();
  if (el.roomImportBackBtn) el.roomImportBackBtn.onclick = () => roomImportBack();
  const pageEnergy = document.getElementById("addPageMenuEnergy");
  if (pageEnergy) {
    pageEnergy.onclick = () => {
      closeAllDropdowns();
      if (el.addEnergyPageBtn) el.addEnergyPageBtn.click();
    };
  }

  document.addEventListener("click", (ev) => {
    const target = ev.target;
    if (!(target instanceof Element)) return;
    if (target.closest(".dropdown")) return;
    closeAllDropdowns();
  });
  document.addEventListener("keydown", (ev) => {
    if (ev.key === "Escape") {
      closeAllDropdowns();
      const textSizes = document.getElementById("layoutTextSizeDetails");
      if (textSizes) textSizes.open = false;
    }
  });
  // Delete / Backspace remove the selected widget (after the usual confirmation).
  document.addEventListener("keydown", (ev) => {
    if (ev.key !== "Delete" && ev.key !== "Backspace") return;
    if (ev.ctrlKey || ev.metaKey || ev.altKey || ev.shiftKey) return;
    if (!layoutShortcutAllowed(ev)) return;
    const active = document.activeElement;
    if (active instanceof HTMLElement &&
        (active.isContentEditable || ["INPUT", "TEXTAREA", "SELECT"].includes(active.tagName))) return;
    if (document.querySelector(".modal-overlay:not(.hidden)")) return; // a dialog is open
    const page = selectedPage();
    if (!page || isFixedPage(page) || !selectedWidget()) return;
    ev.preventDefault(); // Backspace must not navigate back
    deleteWidget();
  });
  // Arrow keys nudge the selected widget (Shift = 1 px, otherwise one grid step).
  document.addEventListener("keydown", (ev) => {
    const arrows = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };
    const dir = arrows[ev.key];
    if (!dir || ev.ctrlKey || ev.metaKey || ev.altKey) return;
    if (!layoutShortcutAllowed(ev)) return;
    const active = document.activeElement;
    if (active instanceof HTMLElement &&
        (active.isContentEditable || ["INPUT", "TEXTAREA", "SELECT"].includes(active.tagName))) return;
    const widget = selectedWidget();
    const page = selectedPage();
    if (!widget || !widget.rect || !page || isFixedPage(page)) return;
    const step = ev.shiftKey ? 1 : GRID;
    const nextX = clamp(widget.rect.x + dir[0] * step, 0, CANVAS_WIDTH - widget.rect.w);
    const nextY = clamp(widget.rect.y + dir[1] * step, 0, CANVAS_HEIGHT - widget.rect.h);
    ev.preventDefault();
    if (nextX === widget.rect.x && nextY === widget.rect.y) return;
    widget.rect.x = nextX;
    widget.rect.y = nextY;
    renderCanvas();
    renderInspector();
    markDirty(); // this never goes through renderAll(), which would otherwise catch this
  });
}

// Items in alphabetical order of their translated labels (in the language of the editor), within
// each group if the container has groups; used for the widget types of the "+ Widget" dialog.
function sortItemsAlphabetically(container, itemClass, groupClass) {
  const language = String((editor.settings && editor.settings.ui && editor.settings.ui.language) || document.documentElement.lang || "en");
  const collator = new Intl.Collator(language, { sensitivity: "base", numeric: true });
  const groups = container.querySelectorAll(`.${groupClass}`);
  const sortItems = (parent) => {
    const items = Array.from(parent.querySelectorAll(`:scope > .${itemClass}`));
    items.sort((x, y) => collator.compare(x.textContent.trim(), y.textContent.trim()));
    for (const item of items) parent.appendChild(item);
  };
  if (groups.length) groups.forEach(sortItems);
  else sortItems(container);
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
      if (menu.dataset.floating === "true") {
        // The wide widget menu escapes the narrow sidebar: fixed position next to the button.
        const rect = toggle.getBoundingClientRect();
        menu.style.position = "fixed";
        menu.style.top = `${rect.bottom + 4}px`;
        menu.style.left = `${Math.max(8, Math.min(rect.left, window.innerWidth - menu.offsetWidth - 8))}px`;
        menu.style.right = "auto";
        // A small window: the menu never runs past the bottom edge, it scrolls instead.
        menu.style.maxHeight = "";
        menu.style.overflowY = "auto";
        let top = rect.bottom + 4;
        if (top + menu.offsetHeight > window.innerHeight - 8) {
          // Not enough room below the button: use the room above it if that is larger, else scroll below.
          const roomBelow = window.innerHeight - top - 8;
          const roomAbove = rect.top - 12;
          if (roomAbove > roomBelow && menu.offsetHeight <= roomAbove) {
            top = rect.top - menu.offsetHeight - 4;
          } else if (roomAbove > roomBelow) {
            menu.style.maxHeight = `${roomAbove}px`;
            top = 8;
          } else {
            menu.style.maxHeight = `${Math.max(120, roomBelow)}px`;
          }
        }
        menu.style.top = `${Math.max(8, top)}px`;
      }
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
