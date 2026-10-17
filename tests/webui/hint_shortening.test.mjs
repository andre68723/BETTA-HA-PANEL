// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// Long hints show their first sentence with an info button. In the settings that only happens at a real sentence end:
// status lines, warnings marked .no-shorten and one long sentence stay whole, and "Optional." alone is no summary.
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

const hint = (inInspector = false, classes = []) => ({
  classList: { contains: (c) => classes.includes(c) },
  closest: (sel) => (inInspector && sel === "#inspectorSection" ? {} : null),
});
const LONG = "Optional. Ein Passwort schützt den Editor, die Einstellungen und das Panel vor fremdem Zugriff im Netzwerk und gilt sofort.";

test("a short first sentence is joined by the next one", () => {
  const { fn } = loadEditor();
  const shown = fn("hintFirstSentence")(LONG, hint());
  assert.ok(shown.startsWith("Optional. Ein Passwort"), shown);
  assert.ok(shown.endsWith("Netzwerk und gilt sofort."), shown);
});

test("settings status lines, warnings and endless sentences stay whole", () => {
  const { fn } = loadEditor();
  const s = fn("hintShortenable");
  assert.equal(s(hint(), "Konfiguriert: ja | Verbunden: ja | Passwort gespeichert: ja | Land: DE | RSSI (verbunden): -52 dBm | Kanal 11"), false);
  assert.equal(s(hint(false, ["no-shorten"]), "Nicht geschützt: Jeder im Netzwerk kann den Editor öffnen. Ein Passwort schützt Editor und Einstellungen vor Fremden."), false);
  assert.equal(s(hint(), "Hat 10 Minuten lang niemand das Panel benutzt, lädt es zwischen 03:00 und 04:00 eine neuere Version herunter und startet danach ohne Rückfrage neu und das ist ein sehr langer Satz ohne Punkt"), false);
  assert.equal(s(hint(), "Ein kurzer Hinweis."), false);
});

test("a long hint with an early sentence end is shortened, in the inspector with the older ellipsis rule", () => {
  const { fn } = loadEditor();
  assert.equal(fn("hintShortenable")(hint(), LONG), true);
  const endless = "x".repeat(150);
  assert.equal(fn("hintShortenable")(hint(true), endless), true, "inspector keeps its rule");
  assert.ok(fn("hintFirstSentence")(endless, hint(true)).endsWith("…"));
});
