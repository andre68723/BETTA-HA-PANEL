/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Room import: a new, freely editable page for one Home Assistant area, laid out in fixed zones
 * instead of wherever the next free spot happens to be. Step 1 picks the area (registry only),
 * step 2 assigns the area's entities to roles (with a live preview of the page), and the zone
 * packer below places them: a status strip on top, a side column with the large tiles, a light
 * grid and wide cover/fan rows on the right. Unlike the house page nothing is tracked back to
 * Home Assistant afterwards; "reset" is deleting the page and importing again.
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */

// A "light" entity that is really a wall panel's own screen backlight or a decorative accent strip,
// not a room lamp (found importing a real "Arbeitszimmer": 4 of these next to 2 real lamps). No
// device_class tells them apart, so this matches the entity_id. They stay listed, just not preselected.
const ROOM_IMPORT_LIGHT_EXCLUDE_RE = /(display[_-]?backlight|accent[_-]?color)/i;

// The roles in dialog order. zone decides where the packer puts a role's tiles; cap is how many of the
// found entities start checked (the rest stay listed, unchecked), so a fresh page is not overloaded;
// "best" picks the area's designated sensor or the best scoring one instead of the first.
const ROOM_IMPORT_ROLES = [
  { key: "temperature", zone: "status", type: "sensor", cap: 1, best: true,
    match: (d, dc) => d === "sensor" && dc === "temperature" },
  { key: "humidity", zone: "status", type: "sensor", cap: 1, best: true,
    match: (d, dc) => d === "sensor" && dc === "humidity" },
  { key: "presence", zone: "status", type: "binary_tile", cap: 1,
    match: (d, dc) => d === "binary_sensor" && ["motion", "occupancy", "presence", "moving"].includes(dc) },
  // A window/door contact is a binary tile in the door look. The panel draws that look with its contact
  // renderer, whose minimum size (contact_tile) is larger than a plain binary tile's, so plan with it.
  { key: "contact", zone: "status", type: "binary_tile", displayMode: "door", sizeType: "contact_tile", cap: 2,
    match: (d, dc) => d === "binary_sensor" && ["window", "door", "opening", "garage_door"].includes(dc) },
  { key: "climate", zone: "side", type: "climate_tile", cap: 1, match: (d) => d === "climate" },
  { key: "media", zone: "side", type: "media_player", cap: 1, match: (d) => d === "media_player" },
  { key: "light", zone: "grid", type: "light_tile", cap: 4, match: (d) => d === "light" },
  { key: "cover", zone: "rows", type: "cover_tile", cap: 4, match: (d) => d === "cover" },
  { key: "fan", zone: "rows", type: "fan_tile", cap: 2, match: (d) => d === "fan" },
  { key: "vacuum", zone: "bottom", type: "vacuum_tile", cap: 1, match: (d) => d === "vacuum" },
];

const ROOM_IMPORT_PAD = 10;
const ROOM_IMPORT_GAP = 10;

let roomImport = null; // { rooms: [area], roomName, area, roles: [{ role, entries: [{ item, selected }] }] }

function roomImportRole(item) {
  const domain = item.id.split(".")[0];
  const dc = item.device_class || "";
  return ROOM_IMPORT_ROLES.find((role) => role.match(domain, dc)) || null;
}

// "Wohnzimmer Deckenlicht" or "EG-Wohnzimmer_Fenster_1" in the area "Wohnzimmer" become "Deckenlicht"
// and "Fenster 1" on the tile: the room name (after a short floor prefix such as "EG-") is dropped.
function roomImportTitle(item, roomName) {
  const name = String(item.name || item.id);
  const lower = name.toLowerCase();
  const plain = String(roomName || "").toLowerCase();
  // Entity names often spell the room without umlauts ("EG-Kueche_Fenster" in the area "Küche").
  const spelled = plain.replace(/\u00e4/g, "ae").replace(/\u00f6/g, "oe").replace(/\u00fc/g, "ue").replace(/\u00df/g, "ss");
  const room = [plain, spelled].find((r) => r && lower.indexOf(r) >= 0 && lower.indexOf(r) <= 4) || "";
  const at = room ? lower.indexOf(room) : -1;
  if (at >= 0) {
    const rest = name.slice(at + room.length).replace(/^[\s\-_:\u00b7]+/, "").replace(/_/g, " ").trim();
    if (/\p{L}/u.test(rest)) return rest; // "Henri (2)" keeps its name, not just "(2)"
  }
  return name;
}

function roomImportBuildRoles(items, roomName, area) {
  const roles = ROOM_IMPORT_ROLES.map((role) => ({ role, entries: [] }));
  for (const item of items) {
    if (item.room !== roomName) continue;
    const role = roomImportRole(item);
    if (role) roles[ROOM_IMPORT_ROLES.indexOf(role)].entries.push({ item, selected: false });
  }
  for (const { role, entries } of roles) {
    if (!entries.length) continue;
    if (role.best) {
      // The area's own designated sensor (Home Assistant area settings) wins, else the best score.
      const designated = area && (role.key === "temperature" ? area.temperature_entity_id : area.humidity_entity_id);
      let best = entries.find((entry) => designated && entry.item.id === designated);
      if (!best) {
        best = entries.reduce((a, b) => (roomSensorScore(b.item.id, roomName) > roomSensorScore(a.item.id, roomName) ? b : a));
      }
      entries.splice(entries.indexOf(best), 1);
      entries.unshift(best);
    }
    // Backlights and accent strips go to the end of the light list and never start checked.
    const eligible = (entry) => !(role.key === "light" && ROOM_IMPORT_LIGHT_EXCLUDE_RE.test(entry.item.id));
    entries.sort((a, b) => Number(eligible(b)) - Number(eligible(a)));
    entries.forEach((entry, index) => {
      entry.selected = index < role.cap && eligible(entry);
    });
  }
  return roles.filter((group) => group.entries.length);
}

function roomImportSelectedByZone(roles) {
  const zones = { status: [], side: [], grid: [], rows: [], bottom: [] };
  for (const { role, entries } of roles) {
    for (const entry of entries) {
      if (entry.selected) {
        zones[role.zone].push({ entry, type: role.type, role: role.key, displayMode: role.displayMode, sizeType: role.sizeType });
      }
    }
  }
  return zones;
}

// Lays the selected tiles out on a W x H canvas; returns { placed: [{ tile, rect }], overflow: [tile] }.
// Everything uses the widgets' own minimum sizes (widgetSizeLimits) so a page never contains a tile the
// panel would reject, and grows only where there is room.
function roomImportPack(zones, W, H) {
  const P = ROOM_IMPORT_PAD;
  const G = ROOM_IMPORT_GAP;
  const snapDown = (v) => Math.floor(v / GRID) * GRID;
  const placed = [];
  const overflow = [];
  const place = (tile, x, y, w, h) => placed.push({ tile, rect: { x, y, w, h } });
  const min = (tile) => widgetSizeLimits(tile.sizeType || tile.type);

  // Status strip: small value tiles in up to two rows, each at least its minimum width. 90 px holds a
  // sensor's title and value once its "updated ..." line is off (createRoomImportPage does that);
  // with it, the 80 px minimum overlapped on the panel.
  const statusH = 90;
  const availW = W - 2 * P;
  const statusRows = [];
  let row = [];
  let rowW = 0;
  for (const tile of zones.status) {
    const w = min(tile).minW;
    if (row.length && rowW + G + w > availW) {
      statusRows.push(row);
      row = [];
      rowW = 0;
    }
    if (statusRows.length >= 2) {
      overflow.push(tile);
      continue;
    }
    rowW += (row.length ? G : 0) + w;
    row.push(tile);
  }
  if (row.length) statusRows.push(row);
  // One extra width for every status tile, so both rows line up.
  const share = Math.min(...statusRows.map((tiles) => {
    const sumMin = tiles.reduce((sum, tile) => sum + min(tile).minW, 0) + G * (tiles.length - 1);
    return Math.floor((availW - sumMin) / tiles.length);
  }));
  let y = P;
  for (const tiles of statusRows) {
    let x = P;
    for (const tile of tiles) {
      const w = snapDown(Math.min(min(tile).minW + share, Math.max(260, min(tile).minW)));
      place(tile, x, y, w, statusH);
      tile.statusRow = statusRows.indexOf(tiles);
      x += w + G;
    }
    y += statusH + G;
  }
  const top = y;
  const bottomLimit = H - P;

  const rightTiles = zones.grid.length + zones.rows.length + zones.bottom.length;
  const narrow = W < 640;
  const useSide = zones.side.length > 0 && !narrow && rightTiles > 0;

  // Side column: climate and media stacked, sharing the height (never below their minimum).
  let rightX = P;
  if (useSide) {
    const sideW = W >= 1000 ? 380 : 300;
    const availH = bottomLimit - top;
    const fitting = [];
    let used = 0;
    for (const tile of zones.side) {
      const need = min(tile).minH + (fitting.length ? G : 0);
      if (used + need > availH) {
        overflow.push(tile);
        continue;
      }
      fitting.push(tile);
      used += need;
    }
    const extra = Math.floor((availH - used) / Math.max(1, fitting.length));
    let sy = top;
    for (const tile of fitting) {
      const h = snapDown(Math.min(min(tile).minH + extra, min(tile).minH + 140, min(tile).maxH));
      place(tile, P, sy, sideW, h);
      sy += h + G;
    }
    rightX = P + sideW + G;
  }
  const RW = W - rightX - P;
  y = top;

  // Without a right-hand area (or on a narrow canvas) the large tiles go first, side by side.
  if (!useSide && zones.side.length) {
    const perRow = narrow ? 1 : Math.max(1, Math.min(zones.side.length, 3));
    const w = snapDown(Math.min((RW - G * (perRow - 1)) / perRow, 480));
    for (let i = 0; i < zones.side.length; i += perRow) {
      const tiles = zones.side.slice(i, i + perRow);
      const h = snapDown(Math.max(...tiles.map((tile) => min(tile).minH)) + (narrow ? 0 : 60));
      if (y + h > bottomLimit || w < Math.max(...tiles.map((tile) => min(tile).minW))) {
        overflow.push(...tiles);
        continue;
      }
      tiles.forEach((tile, k) => place(tile, rightX + k * (w + G), y, w, h));
      y += h + G;
    }
  }

  // Height the cover/fan rows and the vacuum below the lights will need.
  const rowsMinW = zones.rows.length ? Math.max(...zones.rows.map((tile) => min(tile).minW)) : 0;
  const rowsPerRow = zones.rows.length && RW >= 2 * rowsMinW + G ? 2 : 1;
  const rowH = 90;
  const belowLights = Math.ceil(zones.rows.length / rowsPerRow) * (rowH + G) +
    zones.bottom.reduce((sum, tile) => sum + 300 + G, 0);

  // Light grid: tiles at the minimum height, stretched to fill the row width; a single row may be
  // taller (up to 220) when the page has the room for it.
  if (zones.grid.length) {
    const ls = min(zones.grid[0]).minW;
    const cols = Math.max(0, Math.floor((RW + G) / (ls + G)));
    const w = cols ? snapDown((RW - G * (cols - 1)) / cols) : 0;
    const minH = min(zones.grid[0]).minH;
    const tall = 220;
    const h = cols && zones.grid.length <= cols && y + tall + G + belowLights <= bottomLimit ? tall : minH;
    zones.grid.forEach((tile, i) => {
      const r = cols ? Math.floor(i / cols) : 0;
      const ty = y + r * (h + G);
      if (!cols || ty + h > bottomLimit) {
        overflow.push(tile);
        return;
      }
      place(tile, rightX + (i % cols) * (w + G), ty, w, h);
    });
    const usedRows = cols ? Math.ceil(zones.grid.filter((tile) => !overflow.includes(tile)).length / cols) : 0;
    if (usedRows) y += usedRows * (h + G);
  }

  // Covers and fans: wide rows, two per row where they fit.
  if (zones.rows.length) {
    const perRow = rowsPerRow;
    const w = snapDown((RW - G * (perRow - 1)) / perRow);
    const h = rowH;
    zones.rows.forEach((tile, i) => {
      const ty = y + Math.floor(i / perRow) * (h + G);
      if (w < min(tile).minW || ty + h > bottomLimit) {
        overflow.push(tile);
        return;
      }
      place(tile, rightX + (i % perRow) * (w + G), ty, w, h);
    });
    const fitted = zones.rows.filter((tile) => !overflow.includes(tile)).length;
    if (fitted) y += Math.ceil(fitted / perRow) * (h + G);
  }

  // Vacuum last, up to 360 wide and at its default height: at the 220 px minimum its name and status
  // overlapped on the panel.
  for (const tile of zones.bottom) {
    const w = snapDown(Math.min(RW, 360));
    const h = 300;
    if (w < min(tile).minW || y + h > bottomLimit) {
      overflow.push(tile);
      continue;
    }
    place(tile, rightX, y, w, h);
    y += h + G;
  }
  return { placed, overflow };
}

// Start from a page that fits, with a single status row: whatever the capped preselection still could
// not place, and the windows/doors or presence tiles of a second status row, start unchecked.
function roomImportFitPreselection(roles) {
  const first = roomImportPack(roomImportSelectedByZone(roles), CANVAS_WIDTH, CANVAS_HEIGHT);
  for (const tile of first.overflow) tile.entry.selected = false;
  for (const { tile } of first.placed) {
    if (tile.statusRow > 0 && (tile.role === "contact" || tile.role === "presence")) tile.entry.selected = false;
  }
}

function roomImportLayout() {
  return roomImportPack(roomImportSelectedByZone(roomImport.roles), CANVAS_WIDTH, CANVAS_HEIGHT);
}

// options.targetPageId: fill this (empty, regular) page instead of adding a new one -- the empty-page
// hint on the canvas passes it.
async function openRoomImportPicker(options = {}) {
  if (!el.roomImportOverlay) return;
  el.roomImportOverlay.classList.remove("hidden");
  roomImport = { rooms: [], roomName: "", area: null, roles: [], targetPageId: options.targetPageId || null };
  roomImportShowStep("room");
  if (el.roomImportSelect) el.roomImportSelect.innerHTML = "";
  if (el.roomImportConfirmBtn) el.roomImportConfirmBtn.disabled = true;
  setRoomImportStatus(t("room_import.loading"));
  try {
    roomImport.rooms = await fetchRoomAreas();
  } catch (error) {
    setRoomImportStatus(String(error.message || error));
    return;
  }
  if (!roomImport.rooms.length) {
    setRoomImportStatus(t("room_import.none"));
    return;
  }
  if (el.roomImportSelect) {
    for (const area of roomImport.rooms) {
      const opt = document.createElement("option");
      opt.value = area.name;
      opt.textContent = area.floor ? `${area.name} (${area.floor})` : area.name;
      el.roomImportSelect.appendChild(opt);
    }
  }
  if (el.roomImportConfirmBtn) el.roomImportConfirmBtn.disabled = false;
  setRoomImportStatus("");
}

// The areas from the Home Assistant registry alone, without reading any entity.
async function fetchRoomAreas() {
  for (let poll = 0; poll < 30; poll++) {
    const data = await apiGet("/api/ha/light_entities?domain=room&areas_only=1" + (poll === 0 ? "&refresh=1" : ""));
    if (data.status === "ready") {
      const seen = new Set();
      return (data.areas || [])
        .filter((a) => a && a.name && !seen.has(a.name) && seen.add(a.name))
        .sort((a, b) => a.name.localeCompare(b.name));
    }
    if (data.status === "disconnected") throw new Error(t("entity_picker.disconnected"));
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
  throw new Error(t("entity_picker.pending"));
}

function closeRoomImportPicker() {
  if (el.roomImportOverlay) el.roomImportOverlay.classList.add("hidden");
  roomImport = null;
}

function setRoomImportStatus(text) {
  if (el.roomImportStatus) el.roomImportStatus.textContent = text;
}

function roomImportShowStep(step) {
  const assign = step === "assign";
  if (roomImport) roomImport.step = step;
  el.roomImportStepRoom?.classList.toggle("hidden", assign);
  el.roomImportStepAssign?.classList.toggle("hidden", !assign);
  el.roomImportBackBtn?.classList.toggle("hidden", !assign);
  el.roomImportCard?.classList.toggle("room-import-wide", assign);
  if (el.roomImportHint) el.roomImportHint.textContent = t(assign ? "room_import.hint_assign" : "room_import.hint");
  if (el.roomImportConfirmBtn) el.roomImportConfirmBtn.textContent = t(assign ? "room_import.create" : "room_import.confirm");
}

// Confirm button: "Next" on the room step, "Create page" on the assignment step.
async function confirmRoomImport() {
  if (!roomImport) return;
  if (roomImport.step === "assign") {
    createRoomImportPage();
    return;
  }
  const roomName = el.roomImportSelect ? el.roomImportSelect.value : "";
  if (!roomName) return;
  if (el.roomImportConfirmBtn) el.roomImportConfirmBtn.disabled = true;
  setRoomImportStatus(t("room_import.reading", { room: roomName }));
  let items;
  try {
    // The search matches parts of area names ("Bad" also finds "Badezimmer"); exact matches are kept below.
    ({ items } = await fetchAllRoomItems({ search: roomName }));
  } catch (error) {
    setRoomImportStatus(String(error.message || error));
    if (el.roomImportConfirmBtn) el.roomImportConfirmBtn.disabled = false;
    return;
  }
  if (!roomImport) return; // closed meanwhile
  const area = roomImport.rooms.find((a) => a.name === roomName) || null;
  const roles = roomImportBuildRoles(items, roomName, area);
  if (el.roomImportConfirmBtn) el.roomImportConfirmBtn.disabled = false;
  if (!roles.length) {
    setRoomImportStatus(t("room_import.empty", { room: roomName }));
    return;
  }
  Object.assign(roomImport, { roomName, area, roles });
  roomImportFitPreselection(roles);
  setRoomImportStatus("");
  roomImportShowStep("assign");
  renderRoomImportAssign();
}

function roomImportBack() {
  if (!roomImport) return;
  roomImportShowStep("room");
  setRoomImportStatus("");
}

function renderRoomImportAssign() {
  if (!roomImport || !el.roomImportRoles) return;
  const layout = roomImportLayout();
  const overflowEntries = new Set(layout.overflow.map((tile) => tile.entry));
  el.roomImportRoles.innerHTML = "";
  for (const group of roomImport.roles) {
    const section = document.createElement("div");
    section.className = "room-import-role";
    const head = document.createElement("h3");
    const label = document.createElement("span");
    label.textContent = t(`room_import.role.${group.role.key}`);
    const count = document.createElement("span");
    count.textContent = `${group.entries.filter((entry) => entry.selected).length}/${group.entries.length}`;
    head.append(label, count);
    section.appendChild(head);
    group.entries.forEach((entry, index) => {
      const row = document.createElement("label");
      row.className = "room-import-entry" + (overflowEntries.has(entry) ? " overflow" : "");
      const box = document.createElement("input");
      box.type = "checkbox";
      box.checked = entry.selected;
      box.onchange = () => {
        entry.selected = box.checked;
        renderRoomImportAssign();
      };
      const text = document.createElement("span");
      text.className = "room-import-name";
      text.textContent = roomImportTitle(entry.item, roomImport.roomName);
      const id = document.createElement("small");
      id.textContent = entry.item.id;
      text.appendChild(id);
      row.append(box, text);
      const moves = group.entries.length > 1
        ? [[-1, "\u2191", "room_import.move_up"], [1, "\u2193", "room_import.move_down"]]
        : [];
      for (const [delta, glyph, key] of moves) {
        const btn = document.createElement("button");
        btn.type = "button";
        btn.className = "btn small";
        btn.textContent = glyph;
        btn.title = t(key);
        const target = index + delta;
        btn.disabled = target < 0 || target >= group.entries.length;
        btn.onclick = (event) => {
          event.preventDefault();
          group.entries.splice(index, 1);
          group.entries.splice(target, 0, entry);
          renderRoomImportAssign();
        };
        row.appendChild(btn);
      }
      section.appendChild(row);
    });
    el.roomImportRoles.appendChild(section);
  }
  renderRoomImportPreview(layout);
  const nothing = layout.placed.length === 0;
  if (el.roomImportFit) {
    const names = layout.overflow.map((tile) => roomImportTitle(tile.entry.item, roomImport.roomName));
    el.roomImportFit.textContent = layout.overflow.length
      ? t("room_import.fit_overflow", { names: names.join(", ") })
      : nothing ? t("room_import.nothing_selected") : t("room_import.fit_ok", { count: layout.placed.length });
    el.roomImportFit.classList.toggle("room-import-warn", layout.overflow.length > 0);
  }
  if (el.roomImportConfirmBtn) el.roomImportConfirmBtn.disabled = nothing || layout.overflow.length > 0;
}

function renderRoomImportPreview(layout) {
  const preview = el.roomImportPreview;
  if (!preview) return;
  preview.innerHTML = "";
  preview.style.aspectRatio = `${CANVAS_WIDTH} / ${CANVAS_HEIGHT}`;
  const pct = (v, total) => `${(v * 100) / total}%`;
  for (const { tile, rect } of layout.placed) {
    const box = document.createElement("div");
    box.className = "room-import-box";
    box.dataset.role = tile.role;
    box.style.left = pct(rect.x, CANVAS_WIDTH);
    box.style.top = pct(rect.y, CANVAS_HEIGHT);
    box.style.width = pct(rect.w, CANVAS_WIDTH);
    box.style.height = pct(rect.h, CANVAS_HEIGHT);
    box.textContent = roomImportTitle(tile.entry.item, roomImport.roomName);
    box.title = `${t(`room_import.role.${tile.role}`)}: ${tile.entry.item.id}`;
    preview.appendChild(box);
  }
}

function createRoomImportPage() {
  if (!roomImport || roomImport.step !== "assign") return;
  const layout = roomImportLayout();
  if (!layout.placed.length || layout.overflow.length) return;
  const roomName = roomImport.roomName;
  const target = editor.layout.pages.find((page) =>
    page.id === roomImport.targetPageId && !isFixedPage(page) && !page.widgets.length);
  if (target) {
    target.title = roomName.slice(0, 63);
    editor.selectedPageId = target.id;
  } else {
    if (editor.layout.pages.length >= maxPages()) {
      setStatus(t("layout.status.max_pages", { max: maxPages() }), true);
      return;
    }
    const pageId = uniqueId("room", editor.layout.pages);
    editor.layout.pages.push({ id: pageId, title: roomName.slice(0, 63), widgets: [] });
    editor.selectedPageId = pageId;
  }
  editor.selectedWidgetId = null;
  let added = 0;
  for (const { tile, rect } of layout.placed) {
    const widget = addWidget(tile.type, {
      entityId: tile.entry.item.id,
      title: roomImportTitle(tile.entry.item, roomName),
      displayMode: tile.displayMode,
    });
    if (!widget) continue;
    widget.rect = clampRectToCanvas(rect, tile.sizeType || tile.type);
    // A status value needs no "updated ..." line, and it does not fit the strip's height.
    if (tile.type === "sensor") widget.sensor_show_age = false;
    added++;
  }
  editor.selectedWidgetId = null;
  closeRoomImportPicker();
  renderAll();
  setStatus(t("room_import.imported", { count: added, room: roomName }));
}
