#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
"""Test layouts: every widget and variant, by topic, for each panel size, in the simulator and on a panel.

  tools/test_layouts.py check
  tools/test_layouts.py generate <board> [--entities FILE] [--out DIR]
  tools/test_layouts.py simulate <board> [--suite NAME] [--text-step N] [--out DIR]
  tools/test_layouts.py view [<board> ...] [--text-step N] [--no-open]
  tools/test_layouts.py load <panel-ip> <suite-file>
  tools/test_layouts.py restore <panel-ip> [--force]
  tools/test_layouts.py status <panel-ip>

check (part of scripts/check.sh) needs no simulator: every widget type and enumerated value has a case, and every
board's layouts fit its pages, 32 KB and canvas without overlaps.

The suites are in tools/test_layout_cases.json: widgets at their 10-inch size with "{role}" entities. generate places
them on pages for a board (paneljc, tab5, panel4), splitting a suite where it exceeds the board's pages or the 32 KB
layout limit, and replaces the roles from an entity map: the local .local/test-layouts/entities.json (real Home
Assistant entities, not committed) when it exists, else tools/test_layout_entities_demo.json. A "grenzen" suite adds
every widget type at its minimum size, with a long title, and a few with a missing entity.

simulate renders the suites with the demo entities in the SDL simulator: tools/check_widget_layout.py checks every
page and a PNG per page goes to the output directory (default /private/tmp/betta-testenv/sim/<board>). view does that
for all boards (or the ones named), writes an overview page with the images and findings and opens it in the browser.

load replaces a panel's layout with a suite after saving the panel's own layout once to
.claude/layout-backups/testenv-<ip>.json; further loads keep that saved layout. restore puts it back, checks it and
removes the file; it stops when the panel's layout was edited during the test, unless --force.
Uses curl (a Python socket can fail with "No route to host" on macOS). Set BETTA_PASSWORD when the panel needs the
editor login.
"""
import argparse
import copy
import hashlib
import html
import json
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from widget_cases import catalogue, expand, state_cases, state_fixture, substitute, widget_fields

REPO = Path(__file__).resolve().parent.parent
DEMO_ENTITIES = REPO / "tools/test_layout_entities_demo.json"
LOCAL_ENTITIES = REPO / ".local/test-layouts/entities.json"
BACKUPS = REPO / ".claude/layout-backups"
BOARDS = {"paneljc": (1280, 680, 12), "tab5": (1280, 600, 12), "panel4": (720, 600, 8)}
MAX_BYTES = 32768
FIXED_NOW = "2026-10-15T12:00"  # generate_simulator_gallery.FIXED_NOW, the default of --now
MARGIN = GAP = 10
LONG_TITLE = "Ein sehr langer Titel, der nicht hineinpasst"

def minimum_sizes():
    """Minimum (w, h) per widget type, and per (type, variant), from main/layout/widget_types.c."""
    source = (REPO / "main/layout/widget_types.c").read_text(encoding="utf-8")
    sizes = {name: (int(w), int(h)) for name, w, h in re.findall(r'\{"(\w+)",\s*\{(\d+),\s*(\d+),', source)}
    sizes.update({(name, variant): (int(w), int(h))
                  for name, variant, w, h in re.findall(r'\{"(\w+)",\s*"(\w+)",\s*\{(\d+),\s*(\d+),', source)})
    return sizes


def widget_sizes():
    """(minimum, maximum) per widget type and per (type, variant) from main/layout/widget_types.c; a maximum of None
    is the content box (the board's canvas)."""
    source = (REPO / "main/layout/widget_types.c").read_text(encoding="utf-8")
    number = r"(\w+)"
    sizes = {}
    for name, variant, *dims in re.findall(r'\{"(\w+)",\s*(?:"(\w+)",\s*)?\{' + r",\s*".join([number] * 4) + r"\}", source):
        w, h, mw, mh = (int(d) if d.isdigit() else None for d in dims)
        sizes[(name, variant) if variant else name] = ((w, h), (mw, mh))
    return sizes


def first_cases(suites):
    first = {}
    for suite in suites.values():
        for case in suite["cases"]:
            if "type" in case and case["type"] not in first:
                first[case["type"]] = case
    return first


def limits_cases(suites):
    """Every widget type (first case of that type) at its minimum size with a long name, without its name, and with a
    missing entity."""
    sizes = widget_sizes()
    cases = []
    for wtype, case in first_cases(suites).items():
        (w, h), _ = sizes.get((wtype, case.get("style_variant")), sizes.get(wtype, ((case["w"], case["h"]), None)))
        # a minimum-size tile may leave extras out and a missing entity shows none: no expected texts there
        bare = {key: value for key, value in case.items() if key != "expect"}
        cases.append(dict(bare, w=w, h=h, title=LONG_TITLE))
        if wtype != "empty_tile":
            cases.append(dict(case, title=f"Ohne Name: {wtype}", show_name=False))
        if "entity_id" in case and wtype != "empty_tile":
            domain = substitute(case["entity_id"], DEMO_ROLES).split(".")[0]
            cases.append(dict(bare, title=f"Fehlt: {wtype}", entity_id=f"{domain}.betta_test_missing"))
    return cases


def maximum_cases(suites):
    """Every widget type at its maximum size (the board's content area where the type has no own limit)."""
    sizes = widget_sizes()
    cases = []
    for wtype, case in first_cases(suites).items():
        _, (mw, mh) = sizes.get((wtype, case.get("style_variant")), sizes.get(wtype, (None, (None, None))))
        cases.append(dict(case, w=mw or 10000, h=mh or 10000, title=f"Maximal: {case.get('title') or wtype}"))
    return cases


DEMO_ROLES = json.loads(DEMO_ENTITIES.read_text(encoding="utf-8"))


def all_suites():
    suites = catalogue()["suites"]
    suites["zustaende"] = {"title": "Zustände", "cases": state_cases(suites)}
    suites["grenzen"] = {"title": "Grenzen", "cases": limits_cases(suites)}
    suites["maximal"] = {"title": "Maximal", "cases": maximum_cases(suites)}
    return suites


def validated_choices():
    """{field: {values}} for the enumerated widget fields of main/layout/layout_validate.c (its "must be a|b" and
    "must be a, b or c" messages and its choice tables)."""
    source = (REPO / "main/layout/layout_validate.c").read_text(encoding="utf-8")
    choices = {}
    for field, values in re.findall(r'(?:widget %s: |")(\w+) must be (?:one of )?([\w|, ]+?)"', source):
        parts = [v for v in re.split(r"\||, | or ", values) if v]
        if len(parts) >= 2 and all(re.fullmatch(r"[a-z0-9_]+", v) for v in parts) and not {"true", "false"} & set(parts):
            choices.setdefault(field, set()).update(parts)
    for row in re.findall(r'\{"(\w+)",((?:\s*"[\w]+",?)+)', source):
        values = re.findall(r'"(\w+)"', row[1])
        if len(values) >= 2 and row[0].endswith(("_view", "_period", "_delta")):
            choices.setdefault(row[0], set()).update(values)
    return choices


def coverage(suites):
    """Widget types and validated field values that no case uses ("default" counts as covered)."""
    source = (REPO / "main/layout/widget_types.c").read_text(encoding="utf-8")
    types = set(re.findall(r'^\s*\{"(\w+)",\s*\{', source, re.M)) - {"roborock_tile"}
    cases = [case for suite in suites.values() for case in suite["cases"] if "type" in case]
    missing = [f"widget type {t}" for t in sorted(types - {case["type"] for case in cases})]
    for field, values in sorted(validated_choices().items()):
        used = {str(case[field]) for case in cases if field in case}
        missing += [f"{field}={value}" for value in sorted(values - used - {"default"})]
    return missing


def place(cases, board):
    """First-fit rows in case order: a case goes into the first row of the current page with room left (the last row
    may grow), else a new row, else a new page. A page case takes a page of its own. Returns a list of pages, each a
    list of (case, rect)."""
    width, height, _ = BOARDS[board]
    pages, current, rows = [], [], []  # rows: [y, height, next x]

    def new_page():
        nonlocal current, rows
        if current:
            pages.append(current)
        current, rows = [], []

    for case in cases:
        if "page" in case:
            new_page()
            pages.append([(case, None)])
            continue
        w, h = min(case["w"], width - 2 * MARGIN), min(case["h"], height - 2 * MARGIN)
        spot = None
        for index, row in enumerate(rows):
            last = index == len(rows) - 1
            if row[2] + w <= width - MARGIN and (h <= row[1] or (last and row[0] + h <= height - MARGIN)):
                spot = row
                break
        if spot is None:
            y = rows[-1][0] + rows[-1][1] + GAP if rows else MARGIN
            if y + h > height - MARGIN:
                new_page()
                y = MARGIN
            spot = [y, h, MARGIN]
            rows.append(spot)
        current.append((case, dict(x=spot[2], y=spot[0], w=w, h=h)))
        spot[2] += w + GAP
        spot[1] = max(spot[1], h)
    new_page()
    return pages


def by_height(cases):
    """Page cases first, then the tallest widgets: fewer, fuller pages than the catalogue order."""
    return sorted(cases, key=lambda case: (0, 0) if "page" in case else (1, -case["h"]))


EXPECT = {}  # widget id -> texts its case expects to be visible (the case's "expect")
SIM_STATES = {}  # widget id -> binding field/state overrides, applied to independent simulator-only entities
SIM_FIELDS = {}  # widget id -> simulator-only fields (the case's "sim", e.g. to-do "items"); never in a panel layout
PLACEHOLDER = re.compile(r"lädt\s*(…|\.\.\.)|loading|nicht verfügbar|unavailable|unbekannt|unknown", re.I)


def entity_ids(value):
    """Entity ids in a widget or page definition (strings shaped domain.object_id)."""
    if isinstance(value, dict):
        return [found for key, item in value.items() if key not in ("evcc_loadpoint_id", "id", "title") for found in entity_ids(item)]
    if isinstance(value, list):
        return [found for item in value for found in entity_ids(item)]
    return [value] if isinstance(value, str) and re.fullmatch(r"[a-z_]+\.[a-z0-9_]+", value) else []


def demo_gaps(layouts, states):
    """Entities the demo pages use without a demo state (they render as unavailable), except deliberately missing ones."""
    known = {state["entity_id"] for state in states}
    used = {entity for layout in layouts.values() for page in layout["pages"] for entity in entity_ids(page)}
    return sorted(entity for entity in used - known if "betta_test_missing" not in entity)


def content_problems(layout, dumps, with_expected=True):
    """Content findings for the rendered pages: a widget with a real entity that shows a loading or unavailable
    placeholder, and, with_expected, texts its case expects ("expect") that are not drawn. Expected texts hold at the
    standard text size; larger text may leave optional rows out by design. dumps: {page id: --dump-layout records}."""
    problems = []
    for page in layout["pages"]:
        records = dumps.get(page["id"], [])
        for widget in page["widgets"]:
            texts = [r.get("text", "") for r in records if r["widget"] == widget["id"] and r["kind"] == "label"]
            where = f"{page.get('title', page['id'])}: {widget['id']} ({widget['type']}) '{widget.get('title', '')}'"
            missing_entity = "betta_test_missing" in json.dumps(widget)
            placeholders = sorted({t for t in texts if PLACEHOLDER.search(t)})
            if placeholders and not missing_entity and not any(value in ("unknown", "unavailable") for value in SIM_STATES.get(widget["id"], {}).values()):
                problems.append(f"{where} shows {', '.join(repr(t) for t in placeholders)}")
            problems += [f"{where} does not show '{text}'" for text in (EXPECT.get(widget["id"], []) if with_expected else [])
                         if not any(text in t for t in texts)]
    return problems


def build_page(suite_id, title, number, placed, entities):
    page_id = f"t_{suite_id}_{number}"
    if placed[0][1] is None:
        page = substitute(placed[0][0]["page"], entities)
        return dict(page, id=page_id, widgets=[])
    widgets = []
    for index, (case, rect) in enumerate(placed):
        widget = widget_fields(case, entities)
        widgets.append(dict(widget, id=f"{page_id}_{index}", rect=rect))
        EXPECT[f"{page_id}_{index}"] = case.get("expect", [])
        SIM_STATES[f"{page_id}_{index}"] = case.get("sim_state", {})
        if "sim" in case:
            SIM_FIELDS[f"{page_id}_{index}"] = case["sim"]
    return {"id": page_id, "title": f"{title} {number}", "widgets": widgets}


def compact(layout):
    return json.dumps(layout, separators=(",", ":"), ensure_ascii=False)


def generate(board, entities):
    """{file stem: layout} for every suite, split by the board's page limit and the layout size limit."""
    _, _, max_pages = BOARDS[board]
    layouts = {}
    for suite_id, suite in all_suites().items():
        pages = [build_page(suite_id, suite["title"], n + 1, placed, entities)
                 for n, placed in enumerate(place(by_height(suite["cases"]), board))]
        parts, part = [], []
        for page in pages:
            if part and (len(part) == max_pages or len(compact({"version": 1, "pages": part + [page]}).encode()) > MAX_BYTES):
                parts.append(part)
                part = []
            part.append(page)
        parts.append(part)
        for n, part in enumerate(parts):
            layouts[suite_id if len(parts) == 1 else f"{suite_id}-{n + 1}"] = {"version": 1, "test_board": board, "pages": part}
    return layouts


def write_layouts(board, entities, out):
    out.mkdir(parents=True, exist_ok=True)
    layouts = generate(board, entities)
    for stem, layout in layouts.items():
        (out / f"{stem}.json").write_text(json.dumps(layout, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        print(f"{out / (stem + '.json')}: {len(layout['pages'])} pages, {len(compact(layout).encode())} bytes")
    return layouts


def demo_states(board):
    """The simulator gallery's demo states plus the states of the widget fixtures the demo map uses."""
    sys.path.insert(0, str(REPO / "tools"))
    from generate_simulator_gallery import DATA, prepare_fixture
    _, gallery = prepare_fixture(json.loads(DATA.read_text(encoding="utf-8")), board)
    states = {state["entity_id"]: state for state in gallery["states"]}
    for state in DEMO_ROLES.get("_states", []):  # the demo map's own states, for entities the gallery has none of
        states.setdefault(state["entity_id"], state)
    extra = {}
    for name in ("energy_price_forecast", "stock", "energy_timeline", "parqet", "bambu_printer"):
        fixture = json.loads((REPO / f"tests/simulator/{name}.json").read_text(encoding="utf-8"))
        for state in fixture.get("states", []):
            states.setdefault(state["entity_id"], state)
        for key in ("parqet_perf", "graph_history"):
            if key in fixture:
                extra.setdefault(key, fixture[key])
    return list(states.values()), dict(extra, weather_hourly=gallery.get("weather_hourly", {}), parqet_perf=gallery.get("parqet_perf", extra.get("parqet_perf", [])))


STYLES = ("flat", "glow", "glass", "matte")
LANGUAGES = ("de", "en")


def use_clock(now):
    """The simulator clock shared with the gallery (generate_simulator_gallery.use_fixed_clock)."""
    sys.path.insert(0, str(REPO / "tools"))
    from generate_simulator_gallery import use_fixed_clock
    return use_fixed_clock(now)


def render_board(board, styles, steps, languages, out, suite=None, theme="dark_v2"):
    """Render every page of the board's suites with the demo entities for each style, text step and language, and
    check them per text step: the widget layout check, and content_problems() on the dumps of the first style in German.
    Returns (layouts, {step: {stem: [problems]}})."""
    sys.path.insert(0, str(REPO / "tools"))
    from generate_simulator_gallery import build_simulator
    from PIL import Image
    if out.exists():
        shutil.rmtree(out)  # images of an earlier run would show pages that no longer exist
    out.mkdir(parents=True)
    layouts = {stem: layout for stem, layout in generate(board, DEMO_ROLES).items() if not suite or stem.split("-")[0] == suite}
    states, extra = demo_states(board)
    gaps = demo_gaps(layouts, states)
    binary = build_simulator(board)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy")

    content_language = "de" if "de" in languages else languages[0]

    def shot(job):
        fixture_path, page, style, step, language = job
        png = out / f"{style}-{step}-{language}" / f"{page['id']}.png"
        dump = ["--dump-layout", str(out / f"dump-{step}" / f"{page['id']}.jsonl")] \
            if style == styles[0] and language == content_language else []
        subprocess.run([str(binary), "--fixture", str(fixture_path), "--page", page["id"], "--style", style, "--theme", theme,
                        "--language", language, "--text-step", str(step), "--exit-after-ms", "600", "--screenshot", str(png),
                        *dump], env=env, check=True, capture_output=True)
        Image.open(png).convert("RGB").save(png, optimize=True)

    jobs = []
    for stem, layout in layouts.items():
        fixture_path = out / f"{stem}.fixture.json"
        pages = [dict(page, widgets=[dict(widget, **SIM_FIELDS.get(widget["id"], {})) for widget in page["widgets"]])
                 for page in layout["pages"]]
        pages, fixture_states = state_fixture(pages, states, SIM_STATES)
        fixture = dict(layout, pages=pages, states=fixture_states, ha_online=True, **extra)
        fixture_path.write_text(json.dumps(fixture, ensure_ascii=False), encoding="utf-8")
        for style in styles:
            for step in steps:
                for language in languages:
                    (out / f"{style}-{step}-{language}").mkdir(exist_ok=True)
                    (out / f"dump-{step}").mkdir(exist_ok=True)
                    jobs += [(fixture_path, page, style, step, language) for page in layout["pages"]]
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        list(pool.map(shot, jobs))

    def check(job):
        stem, step = job
        result = subprocess.run([sys.executable, str(REPO / "tools/check_widget_layout.py"), "--fixture", str(out / f"{stem}.fixture.json"),
                                 "--variant", board, "--text-step", str(step)], capture_output=True, text=True)
        lines = result.stdout.strip().splitlines()
        found = [line.strip() for line in lines[:-1]]
        if result.returncode or not lines:
            found.append(f"layout checker exited {result.returncode}: {result.stderr.strip() or (lines[-1] if lines else 'no diagnostic output')}")
        return step, stem, found

    problems = {step: {} for step in steps}
    with ThreadPoolExecutor(max_workers=4) as pool:
        for step, stem, found in pool.map(check, [(stem, step) for stem in layouts for step in steps]):
            dumps = {page["id"]: [json.loads(line) for line in (out / f"dump-{step}" / f"{page['id']}.jsonl").read_text().splitlines() if line]
                     for page in layouts[stem]["pages"] if page["widgets"]}
            problems[step][stem] = found + [f"Inhalt: {problem}" for problem in content_problems(layouts[stem], dumps, step == 0)]
    for step in steps:
        problems[step]["Demo-Daten"] = [f"no demo state for {entity}" for entity in gaps]
    return layouts, problems


def simulate(board, suite, styles, steps, languages, out, theme="dark_v2"):
    layouts, problems = render_board(board, styles, steps, languages, out, suite, theme)
    failed = False
    for step in steps:
        for stem, found in problems[step].items():
            failed |= bool(found)
            pages = f"{len(layouts[stem]['pages'])} pages, " if stem in layouts else ""
            print(f"{stem} (text step {step}): {pages}{'ok' if not found else f'{len(found)} problem(s)'}")
            for problem in found:
                print("  " + problem)
    missing = coverage(all_suites())
    for item in missing:
        print(f"no case: {item}")
    print(f"images: {out}")
    return 1 if failed or missing else 0


PAGE_STYLE = """:root{--bg:#f4f5f7;--fg:#1d2026;--muted:#5d6470;--card:#fff;--line:#d9dce2;--bad:#b3261e;--ok:#1e7b3a;--accent:#1f6feb}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#121418;--fg:#e8eaed;--muted:#9aa0a8;--card:#1c1f25;
--line:#30343c;--bad:#ff8a80;--ok:#7ee2a0;--accent:#6ea8ff}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,sans-serif}
html{scroll-padding-top:calc(var(--header-h,90px) + var(--nav-h,80px) + 12px)}
header{position:sticky;top:0;background:var(--card);border-bottom:1px solid var(--line);padding:10px 16px;z-index:3}
header h1{font-size:17px;margin:0 0 8px}
.bar{display:flex;flex-wrap:wrap;gap:6px 14px;align-items:center}
.group{display:flex;flex-wrap:wrap;gap:4px;align-items:center}
.group span{color:var(--muted);font-size:13px;margin-right:2px}
button{font:inherit;font-size:14px;padding:4px 10px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);cursor:pointer}
button[aria-pressed=true]{border-color:var(--accent);color:var(--accent)}
nav{position:sticky;top:var(--header-h,90px);z-index:2;background:var(--card);border-bottom:1px solid var(--line);max-height:22vh;overflow:auto;display:flex;flex-wrap:wrap;gap:6px 10px;padding:10px 16px}
nav a{color:var(--accent);text-decoration:none;padding:4px 8px;border-radius:6px}nav a:hover,nav a:focus-visible{background:var(--bg);text-decoration:underline}
main{padding:0 16px 40px}
section{margin:18px 0 28px}
h2{font-size:16px;margin:0 0 6px}
.ok{color:var(--ok)}.bad{color:var(--bad)}
ul{margin:4px 0 10px;padding-left:20px;font-size:13px;overflow-wrap:anywhere}
.pages{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(100%,var(--w)),1fr));gap:12px}
figure{margin:0;background:var(--card);border:1px solid var(--line);border-radius:6px;padding:6px}
figure img{width:100%;height:auto;display:block;cursor:zoom-in}
figure.zoom img{cursor:zoom-out}
figure.zoom{grid-column:1/-1}
figcaption{font-size:13px;color:var(--muted);padding-top:4px}
.board,.step{display:none}.board.shown,.step.shown{display:block}"""

PAGE_SCRIPT = """const state={board:null,style:null,step:null,lang:null};
function load(){try{return JSON.parse(localStorage.getItem('testlayouts')||'{}')}catch(e){return {}}}
function save(){try{localStorage.setItem('testlayouts',JSON.stringify(state))}catch(e){}}
function measureNavigation(){
 document.documentElement.style.setProperty('--header-h',`${document.querySelector('header').getBoundingClientRect().height}px`);
 const nav=document.querySelector('.board.shown nav');
 document.documentElement.style.setProperty('--nav-h',`${nav?.getBoundingClientRect().height||0}px`);
}
function apply(){
 document.querySelectorAll('header button').forEach(b=>b.setAttribute('aria-pressed',state[b.dataset.key]===b.dataset.value));
 document.querySelectorAll('.board').forEach(b=>b.classList.toggle('shown',b.dataset.board===state.board));
 document.querySelectorAll('.step').forEach(b=>b.classList.toggle('shown',b.dataset.step===state.step));
 document.querySelectorAll('.board.shown figure img').forEach(i=>{i.src=`${i.dataset.board}/${state.style}-${state.step}-${state.lang}/${i.dataset.page}.png`});
 measureNavigation();save();}
const saved=load();
document.querySelectorAll('header .group').forEach(g=>{const key=g.dataset.key,values=[...g.querySelectorAll('button')].map(b=>b.dataset.value);
 state[key]=values.includes(saved[key])?saved[key]:values[0];});
document.querySelectorAll('header button').forEach(b=>b.onclick=()=>{state[b.dataset.key]=b.dataset.value;apply()});
document.querySelectorAll('figure img').forEach(i=>i.onclick=()=>i.parentElement.classList.toggle('zoom'));
const navigationSize=new ResizeObserver(measureNavigation);
 navigationSize.observe(document.querySelector('header'));
 document.querySelectorAll('nav').forEach(nav=>navigationSize.observe(nav));
apply();"""


SUITE_LABELS = {
    "energie": "Energie & Prognosen", "klima": "Klima & Wetter", "steuerung": "Schalten & Bedienen",
    "status": "Sensoren & Status", "geraete": "Fahrzeuge & Geräte", "finanzen": "Finanzen & Kurse",
    "zustaende": "Zustände & Bedienung", "grenzen": "Kleine Kacheln & Sonderfälle", "maximal": "Große Kacheln",
}


def suite_label(stem):
    key, _, part = stem.partition("-")
    return SUITE_LABELS.get(key, key.capitalize()) + (f" · Teil {part}" if part else "")


def page_label(page):
    match = re.fullmatch(r"t_(\w+)_(\d+)", page["id"])
    if match and match[1] in SUITE_LABELS:
        return f"{SUITE_LABELS[match[1]]} · Seite {match[2]}"
    return page.get("title", page["id"])


def view(boards, styles, steps, languages, out, open_browser, theme="dark_v2"):
    """Render the boards' suites, write an overview page and open it."""
    names = {"paneljc": '10" 1280 × 680', "tab5": '5" Tab5 1280 × 600', "panel4": '4" 720 × 600'}
    sections, total = [], 0
    for board in boards:
        layouts, problems = render_board(board, styles, steps, languages, out / board, theme=theme)
        links = " ".join(f'<a href="#{board}-{stem}">{html.escape(suite_label(stem))}</a>' for stem in layouts)
        findings = []
        for step in steps:
            items = "".join(f"<li>{html.escape(suite_label(stem))}: {html.escape(problem)}</li>" for stem, found in problems[step].items() for problem in found)
            count = sum(len(found) for found in problems[step].values())
            total += count
            findings.append(f'<div class="step" data-step="{step}"><p class="{"bad" if count else "ok"}">'
                            f'{count} Fund(e) bei Textstufe {step}: Layout in allen Stilen auf Deutsch und Englisch, Inhalt im ersten Stil</p>'
                            f'{f"<ul>{items}</ul>" if items else ""}</div>')
        body = []
        for stem, layout in layouts.items():
            figures = "".join(f'<figure><img data-board="{board}" data-page="{page["id"]}" alt="{html.escape(page_label(page))}" '
                              f'loading="lazy"><figcaption>{html.escape(page_label(page))}</figcaption></figure>'
                              for page in layout["pages"])
            body.append(f'<section id="{board}-{stem}"><h2>{html.escape(suite_label(stem))}</h2><div class="pages">{figures}</div></section>')
        width = "420px" if board == "panel4" else "560px"
        sections.append(f'<div class="board" data-board="{board}" style="--w:{width}"><nav>{links}</nav><main>{"".join(findings)}{"".join(body)}</main></div>')
        print(f"{board}: {sum(len(l['pages']) for l in layouts.values())} pages, "
              + ", ".join(f"text step {step}: {sum(len(f) for f in problems[step].values())} problem(s)" for step in steps))
    missing = coverage(all_suites())
    for item in missing:
        print(f"no case: {item}")

    def group(key, label, values, names=None):
        buttons = "".join(f'<button data-key="{key}" data-value="{value}">{html.escape((names or {}).get(value, str(value)))}</button>' for value in values)
        return f'<div class="group" data-key="{key}"><span>{label}</span>{buttons}</div>'

    coverage_note = (f'<span class="bad">Ohne Testfall: {html.escape(", ".join(missing))}</span>' if missing
                     else '<span class="ok">Alle Typen und Varianten haben einen Testfall</span>')
    header = (f'<header><h1>Testseiten im Simulator</h1><div class="bar">{group("board", "Größe", boards, names)}'
              f'{group("style", "Stil", styles)}{group("step", "Textstufe", [str(s) for s in steps])}'
              f'{group("lang", "Sprache", languages)}<span>Theme: {html.escape(theme)}</span>{coverage_note}</div></header>')
    page = (f'<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
            f"<title>Test layouts</title><style>{PAGE_STYLE}</style></head><body>{header}"
            f'{"".join(sections)}<script>{PAGE_SCRIPT}</script></body></html>')
    index = out / "index.html"
    index.write_text(page, encoding="utf-8")
    print(f"overview: {index}")
    if open_browser:
        subprocess.run(["open", str(index)], check=False)
    return 1 if total or missing else 0


def curl(ip, path, data=None, method=None, timeout=30):
    cmd = ["curl", "-sS", "-m", str(timeout)]
    if os.environ.get("BETTA_PASSWORD"):
        cmd += ["-u", f"admin:{os.environ['BETTA_PASSWORD']}"]
    if data is not None:
        cmd += ["-X", method or "POST", "-H", "Content-Type: application/json", "--data-binary", "@-"]
    result = subprocess.run(cmd + [f"http://{ip}{path}"], input=data.encode() if data is not None else None,
                            capture_output=True, check=True)
    return result.stdout.decode()


def put_layout(ip, layout):
    layout = {key: value for key, value in layout.items() if key != "test_board"}
    reply = json.loads(curl(ip, "/api/layout", compact(layout), "PUT"))
    if not reply.get("ok"):
        sys.exit(f"{ip} refused the layout: {'; '.join(reply.get('errors', [])) or reply}")


def digest(layout):
    return hashlib.sha256(compact(layout).encode()).hexdigest()


def board_of(ip):
    version = json.loads(curl(ip, "/api/version"))
    size = (version["canvas_w"], version["canvas_h"])
    return next((name for name, (w, h, _) in BOARDS.items() if (w, h) == size), None)


def state_path(ip):
    return BACKUPS / f"testenv-{ip}.json"


def load(ip, suite_file, discard_history=False):
    layout = json.loads(Path(suite_file).read_text(encoding="utf-8"))
    board = board_of(ip)
    if layout.get("test_board") != board:
        sys.exit(f"{suite_file} is for {layout.get('test_board')}, {ip} is {board}")
    path = state_path(ip)
    state = json.loads(path.read_text(encoding="utf-8")) if path.exists() else None
    if state is None:
        own = json.loads(curl(ip, "/api/layout"))
        local = [w.get("title") or w["id"] for p in own.get("pages", []) for w in p.get("widgets", [])
                 if w.get("type") == "graph" and not w.get("graph_ha_history")]
        if local and not discard_history:
            sys.exit(f"{ip}: saving a test layout deletes the panel-only history of {len(local)} graph(s) "
                     f"({', '.join(local)}); restore cannot bring it back. Use --discard-graph-history to go on.")
        BACKUPS.mkdir(parents=True, exist_ok=True)
        state = {"layout": own}
        print(f"saved the panel's layout: {path}")
    put_layout(ip, layout)
    state.update(loaded=digest(json.loads(curl(ip, "/api/layout"))), suite=str(suite_file))
    path.write_text(json.dumps(state, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{ip}: loaded {suite_file} ({len(layout['pages'])} pages)")


def restore(ip, force):
    path = state_path(ip)
    if not path.exists():
        sys.exit(f"{ip}: no test layout loaded")
    state = json.loads(path.read_text(encoding="utf-8"))
    if digest(json.loads(curl(ip, "/api/layout"))) != state.get("loaded") and not force:
        sys.exit(f"{ip}: the layout changed since {state.get('suite')} was loaded; restore --force discards that change")
    put_layout(ip, state["layout"])
    if json.loads(curl(ip, "/api/layout")) != state["layout"]:
        sys.exit(f"{ip}: the restored layout differs from the saved one; kept {path}")
    path.unlink()
    print(f"{ip}: restored the panel's layout")


def status(ip):
    path = state_path(ip)
    if not path.exists():
        print(f"{ip}: own layout")
        return
    state = json.loads(path.read_text(encoding="utf-8"))
    changed = digest(json.loads(curl(ip, "/api/layout"))) != state.get("loaded")
    print(f"{ip}: test layout {state.get('suite')}{' (edited since)' if changed else ''}; own layout saved in {path}")


def check_catalogue():
    """Fast check for scripts/check.sh, without rendering: every widget type and validated value has a case, and every
    board's layouts stay within its pages and the 32 KB limit with no tile outside the canvas or on another."""
    problems = [f"no case: {item}" for item in coverage(all_suites())]
    from gallery_content import build_spec
    try:
        build_spec(json.loads((REPO / "tests/simulator/gallery_data.json").read_text())["entities"])
    except (ValueError, KeyError) as error:
        problems.append(f"gallery cases: {error}")
    for board, (width, height, max_pages) in BOARDS.items():
        for stem, layout in generate(board, DEMO_ROLES).items():
            size = len(compact(layout).encode())
            if len(layout["pages"]) > max_pages or size > MAX_BYTES:
                problems.append(f"{board}/{stem}: {len(layout['pages'])} pages, {size} bytes")
            for page in layout["pages"]:
                rects = [(w["id"], w["rect"]) for w in page["widgets"]]
                for wid, r in rects:
                    if r["x"] < 0 or r["y"] < 0 or r["x"] + r["w"] > width or r["y"] + r["h"] > height:
                        problems.append(f"{board}/{stem}: {wid} outside the canvas")
                for i, (a, ra) in enumerate(rects):
                    for b, rb in rects[i + 1:]:
                        if ra["x"] < rb["x"] + rb["w"] and rb["x"] < ra["x"] + ra["w"] and ra["y"] < rb["y"] + rb["h"] and rb["y"] < ra["y"] + ra["h"]:
                            problems.append(f"{board}/{stem}: {a} overlaps {b}")
    for problem in problems:
        print(problem)
    print(f"test pages: {'ok' if not problems else f'{len(problems)} problem(s)'}")
    return 1 if problems else 0


def add_matrix(parser, styles, steps, languages):
    def listed(choices, kind=str):
        def parse(text):
            values = [kind(value) for value in text.split(",")]
            if any(value not in choices for value in values):
                raise argparse.ArgumentTypeError(f"choose from {', '.join(map(str, choices))}")
            return values
        return parse
    parser.add_argument("--styles", type=listed(STYLES), default=list(styles), help=f"comma-separated (default: {','.join(styles)})")
    parser.add_argument("--text-steps", type=listed((0, 1, 2), int), default=list(steps),
                        help=f"comma-separated 0,1,2 (default: {','.join(map(str, steps))})")
    parser.add_argument("--languages", type=listed(LANGUAGES), default=list(languages),
                        help=f"comma-separated de,en (default: {','.join(languages)})")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    gen = commands.add_parser("generate")
    gen.add_argument("board", choices=BOARDS)
    gen.add_argument("--entities", type=Path)
    gen.add_argument("--out", type=Path)
    sim = commands.add_parser("simulate")
    sim.add_argument("board", choices=BOARDS)
    sim.add_argument("--suite")
    sim.add_argument("--theme", default="dark_v2")
    add_matrix(sim, ("flat",), (0,), ("de",))
    sim.add_argument("--now", default=FIXED_NOW, help=f"simulator clock, YYYY-MM-DDTHH:MM Berlin time or 'real' (default {FIXED_NOW})")
    sim.add_argument("--out", type=Path)
    vw = commands.add_parser("view")
    vw.add_argument("boards", nargs="*", metavar="board", help="paneljc, tab5 or panel4 (default: all)")
    add_matrix(vw, STYLES, (0,), ("de",))
    vw.add_argument("--now", default=FIXED_NOW, help=f"simulator clock, YYYY-MM-DDTHH:MM Berlin time or 'real' (default {FIXED_NOW})")
    vw.add_argument("--out", type=Path, default=Path("/private/tmp/betta-testenv/view"))
    vw.add_argument("--no-open", action="store_true")
    vw.add_argument("--theme", default="dark_v2", help="theme ID, e.g. classic_v1 for gold active controls")
    ld = commands.add_parser("load")
    ld.add_argument("ip")
    ld.add_argument("suite_file", type=Path)
    ld.add_argument("--discard-graph-history", action="store_true",
                    help="go on although the panel's graphs with panel-only history lose it")
    rs = commands.add_parser("restore")
    rs.add_argument("ip")
    rs.add_argument("--force", action="store_true")
    commands.add_parser("check", help="cases cover every type and value; layouts fit every board (no rendering)")
    st = commands.add_parser("status")
    st.add_argument("ip")
    args = parser.parse_args()
    if args.command in ("simulate", "view"):
        try:
            use_clock(args.now)
        except ValueError:
            parser.error("--now: YYYY-MM-DDTHH:MM or real")
    if args.command == "check":
        return check_catalogue()
    if args.command == "generate":
        source = args.entities or (LOCAL_ENTITIES if LOCAL_ENTITIES.exists() else DEMO_ENTITIES)
        write_layouts(args.board, json.loads(source.read_text(encoding="utf-8")),
                      args.out or REPO / ".local/test-layouts" / args.board)
    elif args.command == "simulate":
        return simulate(args.board, args.suite, args.styles, args.text_steps, args.languages,
                        args.out or Path("/private/tmp/betta-testenv/sim") / args.board, args.theme)
    elif args.command == "view":
        if any(board not in BOARDS for board in args.boards):
            parser.error(f"boards: {', '.join(BOARDS)}")
        return view(args.boards or list(BOARDS), args.styles, args.text_steps, args.languages, args.out, not args.no_open, args.theme)
    elif args.command == "load":
        load(args.ip, args.suite_file, args.discard_graph_history)
    elif args.command == "restore":
        restore(args.ip, args.force)
    else:
        status(args.ip)
    return 0


if __name__ == "__main__":
    sys.exit(main())
