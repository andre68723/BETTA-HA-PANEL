// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork
import test from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

function rows(components) {
  const editor = loadEditor();
  editor.run(`
    var firmwareRows = [];
    el.settingsFirmwareInfoBox = { hidden: true };
    el.settingsFirmwareInfoDetails = { hidden: true };
    el.settingsFirmwareInfoTable = { hidden: true };
    el.settingsFirmwareInfoBody = { replaceChildren() { firmwareRows = []; }, appendChild(tr) { firmwareRows.push(tr.cells); } };
    document.createElement = () => {
      const node = { cells: [], textContent: "", appendChild(child) { node.cells.push(child.textContent); } };
      return node;
    };
    editor.appComponents = ${JSON.stringify(components)};
    renderFirmwareInfo();
  `);
  return JSON.parse(editor.run("JSON.stringify(firmwareRows)"));
}

test("firmware info separates the embedded C6 image from the version the chip reports", () => {
  const both = rows({ esp_hosted: "3.0.10", esp32_c6_firmware: "3.0.10", esp32_c6_running: "3.0.9" });
  const embedded = both.find(row => row[0] === "ESP32-C6 firmware");
  const running = both.find(row => row[0] === "ESP32-C6 running");
  assert.equal(embedded[1], "3.0.10");
  assert.equal(running[1], "3.0.9");
  assert.match(running[2], /reports now/);
  // before the chip answered the row is absent instead of blank
  const none = rows({ esp_hosted: "3.0.10", esp32_c6_firmware: "3.0.10" });
  assert.equal(none.some(row => row[0] === "ESP32-C6 running"), false);
});
