// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// Loads the editor's classic scripts (components/webui/www/js) into a Node vm context, the way the
// browser shares one global scope between them, with just enough of a DOM stub for their top-level
// code (the element map in state.js looks every id up once). Only for testing pure logic: nothing
// here renders.
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const JS_DIR = path.join(path.dirname(fileURLToPath(import.meta.url)), "../../components/webui/www/js");

// Production order, excluding the async browser bootstrap; check_web ties HTML to CMake.
const HTML = fs.readFileSync(path.join(JS_DIR, "../index.html"), "utf8");
const SCRIPTS = [...HTML.matchAll(/<script src="\/js\/([^"]+)"/g)].map(([, file]) => file).filter(file => file !== "main.js");

export function loadEditor({ width = 1280, height = 680, now } = {}) {
  const document = {
    getElementById: () => null,
    querySelector: () => null,
    querySelectorAll: () => [],
    createElement: () => ({ getContext: () => ({font: "", measureText: (text) => ({width: text.length * 15})}), style: {}, dataset: {}, classList: { add() {}, remove() {}, toggle() {} }, append() {}, appendChild() {} }),
    addEventListener() {},
    documentElement: { dataset: {}, lang: "en" },
  };
  const context = vm.createContext({
    ...(now === undefined ? {} : { Date: class extends Date {
      constructor(...args) { super(...(args.length ? args : [now])); }
      static now() { return now; }
    } }),
    document,
    window: {},
    navigator: { languages: ["en"] },
    console,
    setTimeout,
    clearTimeout,
    Intl,
    URLSearchParams,
  });
  for (const file of SCRIPTS) {
    vm.runInContext(fs.readFileSync(path.join(JS_DIR, file), "utf8"), context, { filename: file });
  }
  // CANVAS_WIDTH/HEIGHT are top-level `let`s shared by every script of the context.
  vm.runInContext(`CANVAS_WIDTH = ${width}; CANVAS_HEIGHT = ${height};`, context);
  return {
    run: (code) => vm.runInContext(code, context),
    fn: (name) => vm.runInContext(name, context),
  };
}
