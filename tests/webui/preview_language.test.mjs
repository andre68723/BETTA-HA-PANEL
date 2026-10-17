// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// The tile previews take their sample texts from the editor language: the English editor must not show German
// words (several previews used to be German-only), and the German one shows the German samples.
import { test } from "node:test";
import assert from "node:assert/strict";
import { loadEditor } from "./load_editor.mjs";

const GERMAN = /Geoeffnet|Zuhause|Szene|Offen\b|Spielt|Licht an|Anwesend|Kuehlen|Luefter|Jetzt|Verriegelt|Vorklima|Schwenken|Natur\b|\bAN \d/;

test("previews show no German sample text in the English editor, and German ones in the German editor", () => {
  const { fn, run } = loadEditor();
  const types = Array.from(run("WIDGET_NAME_TYPES")).filter((type) => type !== "roborock_tile");
  const render = (type, rect, extra = {}) => {
    const widget = { type, id: "w", title: "T", entity_id: "x.y", rect, room_extra_mode: "cover", ...extra };
    const renderer = type === "car_widget" ? fn("carPreviewMarkup") : fn("tilePreviewMarkup");
    return renderer(widget) || "";
  };
  run(`editor.i18nMap=WEB_I18N_BUILTIN.en;`);
  for (const type of types) {
    const markup = render(type, { w: 260, h: 340 }, { lock_entity_id: "lock.a", precond_entity_id: "climate.a" });
    assert.ok(!GERMAN.test(markup), `${type}: ${markup.match(GERMAN)}`);
  }
  run(`editor.i18nMap=WEB_I18N_BUILTIN.de;`);
  assert.match(render("climate_tile", { w: 240, h: 200 }), /Heizen/);
  assert.match(render("contact_tile", { w: 180, h: 70 }), /Offen/);
});

test("the media player preview drops the cover where the panel does", () => {
  const { fn, run } = loadEditor();
  run(`editor.i18nMap=WEB_I18N_BUILTIN.en;`);
  const markup = (h) => fn("tilePreviewMarkup")({ type: "media_player", id: "m", title: "M", rect: { w: 300, h } });
  assert.ok(!markup(280).includes("tp-cover"), "300x280 has no room for a cover on the panel");
  assert.ok(markup(420).includes("tp-cover"), "300x420 does");
});
