#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Static documentation, editor, widget registration and translation checks.

Run by scripts/check.sh without a device or ESP-IDF. Failures exit nonzero.
"""
import html.parser
import gzip
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WWW = ROOT / "components" / "webui" / "www"
errors = []


def fail(msg):
    errors.append(msg)


def check_html():
    class Balance(html.parser.HTMLParser):
        VOID = {"input", "br", "meta", "link", "img", "hr", "source"}

        def __init__(self):
            super().__init__()
            self.stack = []

        def handle_starttag(self, tag, attrs):
            if tag not in self.VOID:
                self.stack.append((tag, self.getpos()[0]))

        def handle_endtag(self, tag):
            if tag in self.VOID:
                return
            if not self.stack or self.stack[-1][0] != tag:
                fail(f"index.html line {self.getpos()[0]}: unexpected </{tag}>")
                return
            self.stack.pop()

    parser = Balance()
    parser.feed((WWW / "index.html").read_text(encoding="utf-8"))
    for tag, line in parser.stack:
        fail(f"index.html line {line}: <{tag}> is never closed")


def check_editor_files():
    """Compare ordered editor sources/HTML and exercise bundling in a temporary directory."""
    cmake = (ROOT / "components" / "webui" / "CMakeLists.txt").read_text(encoding="utf-8")
    sources = re.search(r"set\(WEBUI_JS_SOURCES(.*?)\)", cmake, re.S)
    if not sources:
        fail("components/webui/CMakeLists.txt: missing WEBUI_JS_SOURCES")
        return
    if 'set(WEBUI_ASSETS index.html "${WEBUI_CSS_NAME}" "${WEBUI_JS_NAME}")' not in cmake:
        fail("components/webui/CMakeLists.txt: unexpected embedded asset list")
    listed_js = sources.group(1).split()
    css_sources = re.search(r"set\(WEBUI_CSS_SOURCES(.*?)\)", cmake, re.S)
    if not css_sources:
        fail("components/webui/CMakeLists.txt: missing WEBUI_CSS_SOURCES")
        return
    listed_css = css_sources.group(1).split()
    for name in listed_css:
        if not (WWW / name).exists():
            fail(f"WEBUI_CSS_SOURCES lists {name}, but www/{name} does not exist")
        elif not (WWW / name).read_bytes().endswith(b"\n"):
            fail(f"www/{name} must end with a newline, the build concatenates the stylesheets")
    for path in sorted([WWW / "styles.css", *(WWW / "css").glob("*.css")]):
        if str(path.relative_to(WWW)) not in listed_css:
            fail(f"www/{path.relative_to(WWW)} is not in WEBUI_CSS_SOURCES")
    for name in listed_js:
        if not (WWW / name).exists():
            fail(f"WEBUI_JS_SOURCES lists {name}, but www/{name} does not exist")
    html = (WWW / "index.html").read_text(encoding="utf-8")
    tags = re.findall(r'<script src="/(js/[^"]+)"', html)
    if tags != listed_js:
        fail(f"index.html <script> order {tags} differs from WEBUI_JS_SOURCES {listed_js}")
    links = re.findall(r'<link rel="stylesheet" href="/([^"]+)"', html)
    if links != listed_css:
        fail(f"index.html stylesheet order {links} differs from WEBUI_CSS_SOURCES {listed_css}")
    for path in sorted((WWW / "js").glob("*.js")):
        if f"js/{path.name}" not in listed_js:
            fail(f"www/js/{path.name} is not in WEBUI_JS_SOURCES")
    if tags != listed_js or links != listed_css or any(
            not (WWW / name).exists() for name in listed_js + listed_css):
        return

    generator = ROOT / "components/webui/bundle_assets.py"
    fingerprint = "".join(
        f"{name}:{hashlib.sha256((WWW / name).read_bytes()).hexdigest()}\n"
        for name in listed_js
    )
    generator_line = f"bundle_assets.py:{hashlib.sha256(generator.read_bytes()).hexdigest()}\n"
    fingerprint += generator_line
    js_hash = hashlib.sha256(fingerprint.encode()).hexdigest()[:16]
    css_fingerprint = "".join(
        f"{name}:{hashlib.sha256((WWW / name).read_bytes()).hexdigest()}\n"
        for name in listed_css
    ) + generator_line
    css_hash = hashlib.sha256(css_fingerprint.encode()).hexdigest()[:16]
    js_name, css_name = f"js/app.{js_hash}.js", f"styles.{css_hash}.css"

    with tempfile.TemporaryDirectory(prefix="betta-webui-check-") as tmp:
        command = [sys.executable, str(generator), str(WWW), tmp,
                   js_name, css_name, *listed_css, *listed_js]
        generated = subprocess.run(command, capture_output=True, text=True)
        if generated.returncode != 0:
            fail(f"webui bundle generation failed: {generated.stderr.strip()}")
            return
        built_html = (Path(tmp) / "index.html").read_text(encoding="utf-8")
        built_tags = re.findall(r'<script src="/(js/[^"]+)"', built_html)
        if built_tags != [js_name]:
            fail(f"generated index.html has unexpected scripts: {built_tags}")
        if re.findall(r'<link rel="stylesheet" href="/([^"]+)"', built_html) != [css_name]:
            fail("generated index.html has an unexpected stylesheet")
        if (Path(tmp) / css_name).read_bytes() != b"".join((WWW / name).read_bytes() for name in listed_css):
            fail("generated stylesheet is not the ordered concatenation of WEBUI_CSS_SOURCES")
        bundle = Path(tmp) / js_name
        if not bundle.is_file():
            fail(f"generated bundle {js_name} is missing")
            return
        bundle_text = bundle.read_text(encoding="utf-8")
        markers = [f"/* {name} */" for name in listed_js]
        positions = [bundle_text.find(marker) for marker in markers]
        if -1 in positions or positions != sorted(positions):
            fail("generated bundle is missing scripts or has the wrong order")
        if shutil.which("node"):
            syntax = subprocess.run(["node", "--check", str(bundle)], capture_output=True, text=True)
            if syntax.returncode != 0:
                fail(f"generated bundle {js_name} has a syntax error: {syntax.stderr.strip()}")
        for name in ("index.html", css_name, js_name):
            source = Path(tmp) / name
            compressed = Path(tmp) / f"{name}.gz"
            compressed.parent.mkdir(parents=True, exist_ok=True)
            result = subprocess.run([sys.executable, str(ROOT / "components/webui/gzip_asset.py"),
                                     str(source), str(compressed)], capture_output=True, text=True)
            if result.returncode != 0:
                fail(f"gzip generation failed for {name}: {result.stderr.strip()}")
            elif gzip.decompress(compressed.read_bytes()) != source.read_bytes():
                fail(f"gzip round-trip differs for {name}")


def web_i18n_source():
    paths = [WWW / "js" / "i18n.js", *sorted((WWW / "js").glob("i18n-*.js"))]
    return "\n".join(path.read_text(encoding="utf-8") for path in paths)


def language_tables(text):
    """Return {lang: [keys in order]} across the built-in language files."""
    tables, current = {}, None
    for line in text.splitlines():
        m = re.match(r"^  (\w\w): \{$", line)
        if m:
            current = m.group(1)
            tables[current] = []
            continue
        if current:
            k = re.match(r'^    "([^"]+)":', line)
            if k:
                tables[current].append(k.group(1))
    return tables


def check_web_i18n():
    text = web_i18n_source()
    tables = language_tables(text)
    values, current = {}, None
    for line in text.splitlines():
        header = re.match(r"^  (\w\w): \{$", line)
        if header:
            current = header.group(1)
            values[current] = {}
        entry = re.match(r'^    ("(?:[^"\\]|\\.)*"): ("(?:[^"\\]|\\.)*"),?$', line)
        if current and entry:
            key, value = (json.loads(part) for part in entry.groups())
            values[current][key] = value
            # This state deliberately hides its message when automatic installation is disabled.
            if not value.strip() and key != "update.auto.state.wait_disabled":
                fail(f"web i18n {current}: {key!r} is empty")
    for lang, keys in tables.items():
        seen = set()
        for key in keys:
            if key in seen:
                fail(f"js/i18n.js: key {key!r} appears twice in the {lang} table")
            seen.add(key)
    en, de = set(tables.get("en", [])), set(tables.get("de", []))
    for key in sorted(en - de):
        fail(f"js/i18n.js: key {key!r} is in en but not in de")
    for key in sorted(de - en):
        fail(f"js/i18n.js: key {key!r} is in de but not in en")
    for key in sorted(en & de):
        placeholders = [sorted(re.findall(r"\{[a-zA-Z0-9_]+\}", values[lang].get(key, ""))) for lang in ("en", "de")]
        if placeholders[0] != placeholders[1]:
            fail(f"web i18n: {key!r} has different en/de placeholders: {placeholders}")


def check_widget_help_summaries():
    """The inspector's widget popup repeats the summary of the widget guide (tools/widget_docs_content.py)."""
    from widget_docs_content import WIDGETS
    text = web_i18n_source()
    english = text[text.index("const WEB_I18N_BUILTIN = {"):text.index("\n  de: {")]
    for kind, widget in WIDGETS.items():
        m = re.search(r'"inspector\.help\.widget_' + kind + r'":\s*"((?:[^"\\]|\\.)*)"', english)
        expected = widget["summary"].replace("`", "")
        if not m:
            fail(f"js/i18n.js: no inspector.help.widget_{kind}")
        elif json.loads(f'"{m.group(1)}"') != expected:
            fail(f"js/i18n.js: inspector.help.widget_{kind} differs from the guide summary: {expected!r}")


def check_markdown_links():
    tracked = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard", "*.md"], cwd=ROOT, capture_output=True, text=True).stdout.split()
    link = re.compile(r"\]\(([^)\s]+)\)")
    for name in tracked:
        if name.startswith("components/esp_cam_sensor/"):
            continue
        path = ROOT / name
        if not path.is_file():
            continue  # git ls-files still includes files deleted in the working tree.
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for target in link.findall(line):
                if re.match(r"^(https?:|mailto:|#)", target):
                    continue
                target = target.split("#")[0]
                if target and not (path.parent / target).exists():
                    fail(f"{name} line {n}: link to {target!r} does not exist")


def check_firmware_i18n():
    text = (ROOT / "main" / "settings" / "i18n_store.c").read_text(encoding="utf-8")
    # I18N_BUILTIN_DE writes real German (umlauts, ß) since it is real German either way -- the
    # ASCII substitutes ("verfuegbar" for "verfügbar") only ever existed to dodge this rule, at
    # the cost of every accented word looking broken. EN still holds to plain ASCII.
    de_first_line = text[: text.index('static const char *I18N_BUILTIN_DE =')].count("\n") + 1
    de_last_line = text[: text.index('static const char *I18N_BUILTIN_EN =')].count("\n")
    # "°" (heating/climate temperature format strings): a degree sign has no reasonable ASCII
    # spelling to substitute the way an umlaut does, so it stays even where the rest doesn't.
    allowed = "°"
    for n, line in enumerate(text.splitlines(), 1):
        if de_first_line <= n <= de_last_line:
            continue
        stripped = line.replace(allowed, "")
        if any(ord(c) > 127 for c in stripped):
            fail(f"i18n_store.c line {n}: non-ASCII text (the EN built-in table is ASCII on purpose)")


def firmware_translation_values(text, table):
    """Decode the C strings separately, then check the actual JSON including duplicate members."""
    start = text.index(f"static const char *{table} =")
    end = text.index("\n\n", start)
    parts = re.findall(r'^\s*("(?:[^"\\]|\\.)*")\s*;?\s*$', text[start:end], re.M)
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                fail(f"{table}: duplicate JSON member {key!r}")
            result[key] = value
        return result
    document = json.loads("".join(json.loads(part) for part in parts), object_pairs_hook=unique_object)
    if set(document) != {"lvgl"} or not isinstance(document["lvgl"], dict):
        fail(f"{table}: the root must contain only the lvgl object")
        return {}
    values = {}

    def walk(node, prefix):
        for key, value in node.items():
            if isinstance(value, dict):
                walk(value, prefix + key + ".")
            else:
                if not isinstance(value, str) or not value.strip():
                    fail(f"{table}: {prefix + key!r} is empty or not a string")
                values[prefix + key] = value

    walk(document["lvgl"], "")
    return values


def firmware_translation_keys(text, table):
    return set(firmware_translation_values(text, table))


def check_firmware_translation_keys():
    """Every ui_i18n_get("key", ...) in main/ needs an entry in both built-in tables: a missing one silently shows the
    English fallback on a German panel."""
    text = (ROOT / "main" / "settings" / "i18n_store.c").read_text(encoding="utf-8")
    de, en = (firmware_translation_values(text, table) for table in ("I18N_BUILTIN_DE", "I18N_BUILTIN_EN"))
    for key in sorted(de.keys() ^ en.keys()):
        fail(f"i18n_store.c: key {key!r} is in only one of the two built-in tables")
    for key in sorted(de.keys() & en.keys()):
        formats = [re.findall(r"%(?:[-+ #0]*\d*(?:\.\d+)?(?:hh|ll|[hlzjt])?[diuoxXfFeEgGcsp]|%)", table[key])
                   for table in (en, de)]
        if formats[0] != formats[1]:
            fail(f"i18n_store.c: {key!r} has different en/de printf formats: {formats}")
    used = {}
    for path in sorted((ROOT / "main").rglob("*.c")):
        for key in re.findall(r'ui_i18n_get\(\s*"([^"]+)"', path.read_text(encoding="utf-8", errors="ignore")):
            used.setdefault(key, path.relative_to(ROOT))
    for key, path in sorted(used.items()):
        if key not in en:
            fail(f"{path}: ui_i18n_get key {key!r} has no entry in the built-in tables (i18n_store.c)")


def entity_picker_widget_types(text):
    """Read picker keys and their optional widgetType override."""
    start = text.index("const ENTITY_PICKER_CONFIGS = {")
    body = text[start:text.index("\n};", start)]
    types = {}
    for key, config in re.findall(r'^  (\w+): \{\n(.*?)^  \},?$', body, re.M | re.S):
        override = re.search(r'^\s*widgetType: "([a-z_0-9]+)"', config, re.M)
        types[key] = override.group(1) if override else key
    return types


def switch_case_types(text, func_signature):
    """Read case labels up to the next top-level function."""
    start = text.index(func_signature)
    end = text.index("\nfunction ", start + len(func_signature))
    return set(re.findall(r'case "([a-z_0-9]+)":', text[start:end]))


def tile_preview_types(canvas_js, previews_js):
    """Read dedicated canvas previews and the shared tilePreviewMarkup switch."""
    special = set(re.findall(
        r'(?:if|else if) \(widget\.type === "([a-z_0-9]+)"\) \{\s*\n\s*box\.innerHTML = \w+PreviewMarkup\(widget\);',
        canvas_js))
    return special | switch_case_types(previews_js, "function tilePreviewMarkup(widget) {")


def creatable_widget_types(bind_js, config_js, custom_js=""):
    """Bound plain/picker creators and one level of named custom Add handlers; picker aliases remain valid."""
    from check_simulator_fields import function_body
    source = bind_js
    for name in re.findall(r'\.onclick = \(\) => (?:void )?(\w+)\(\)', bind_js):
        signature = f"function {name}("
        if signature in custom_js:
            source += function_body(custom_js, signature)
    types = set(re.findall(r'addWidget\("([a-z_0-9]+)"', source))
    pickers = entity_picker_widget_types(config_js)
    for key in re.findall(r'openLightEntityPicker\("([a-z_0-9]+)"', source):
        if key in pickers:
            types.update((key, pickers[key]))
    return types


def check_widget_type_tables():
    """Report missing registrations per supported C type; this is not behavioral or visual parity."""
    from check_simulator_fields import function_body
    from generate_widget_docs import size_limits, WIDGETS, GROUPS
    scripts = {p.stem: p.read_text(encoding="utf-8") for p in (WWW / "js").glob("*.js")}
    factory = (ROOT / "main/ui/ui_widget_factory.c").read_text(encoding="utf-8")
    registrations = {
        "editor.add": creatable_widget_types(scripts["bind"], scripts["config"], "\n".join(scripts.values())),
        "editor.preview": tile_preview_types(scripts["canvas"], scripts["tile-previews"]),
        "editor.sizes": switch_case_types(scripts["state"], "function widgetSizeLimitsForCanvas(type) {"),
        "editor.names": set(re.findall(r'"(\w+)"', re.search(r'const WIDGET_NAME_TYPES = new Set\(\[(.*?)\]\)', scripts["config"]).group(1))),
        "docs.content": set(WIDGETS),
    }
    for operation in ("create", "apply_state", "mark_unavailable"):
        body = function_body(factory, f"ui_widget_factory_{operation}_inner(")
        registrations[f"factory.{operation}"] = set(re.findall(r'strcmp\([^,]+,\s*"(\w+)"', body))
    groups = [kind for _, _, kinds in GROUPS for kind in kinds]
    for kind in sorted(set(size_limits()) - {"roborock_tile"}):
        missing = [place for place, types in registrations.items() if kind not in types]
        if groups.count(kind) != 1:
            missing.append("docs.group (expected once)")
        if missing:
            fail(f"widget {kind}: missing " + ", ".join(missing))


def check_style_registry():
    """Compare firmware, editor, settings and gallery style registries and defaults."""
    import gallery_content
    import ui_styles

    registry = (ROOT / "main/ui/theme/ui_style_registry.c").read_text(encoding="utf-8")
    header = (ROOT / "main/ui/theme/ui_style_registry.h").read_text(encoding="utf-8")
    kinds = re.findall(r"^\s*(UI_STYLE_[A-Z]+) = \d+,", header, re.M)

    def c_table(name):
        body = re.search(rf"static const ui_style_entry_t {name}\[\] = \{{(.*?)\n\}};", registry, re.S)
        if body is None:
            fail(f"ui_style_registry.c: table {name} not found")
            return []
        return re.findall(r'\{"([a-z_]+)", (UI_STYLE_[A-Z]+)\}', body.group(1))

    def c_define(name):
        match = re.search(rf"#define {name} (UI_STYLE_[A-Z]+)", header)
        return match.group(1) if match else None

    styles = c_table("s_styles")
    kind_ids = {kind: style_id for style_id, kind in styles}
    c_ids = [style_id for style_id, _ in styles]
    if sorted(kind_ids) != sorted(kinds):
        fail(f"ui_style_registry.c lists {sorted(kind_ids)}, ui_style_kind_t has {sorted(kinds)}")
    c_default = kind_ids.get(c_define("UI_STYLE_DEFAULT"))
    c_fallback = kind_ids.get(c_define("UI_STYLE_FALLBACK"))

    config_js = (WWW / "js" / "config.js").read_text(encoding="utf-8")

    def js_value(name):
        match = re.search(rf"^const {name} = (.*);$", config_js, re.M)
        if match is None:
            fail(f"js/config.js: {name} not found")
            return None
        return json.loads(match.group(1))

    index = (WWW / "index.html").read_text(encoding="utf-8")
    select = re.search(r'<select id="settingsUiStyle">(.*?)</select>', index, re.S)
    html_ids = re.findall(r'<option value="([a-z_]+)"', select.group(1)) if select else []

    expected = ("ids", c_ids), ("default", c_default), ("fallback", c_fallback)
    others = {
        "js/config.js": {"ids": js_value("UI_STYLES"), "default": js_value("UI_STYLE_DEFAULT"),
                         "fallback": js_value("UI_STYLE_FALLBACK")},
        "tools/ui_styles.py": {"ids": list(ui_styles.STYLES), "default": ui_styles.DEFAULT,
                               "fallback": ui_styles.FALLBACK},
        "index.html #settingsUiStyle": {"ids": html_ids},
    }
    for where, values in others.items():
        for key, want in expected:
            if key in values and values[key] != want:
                fail(f"{where}: style {key} is {values[key]!r}, ui_style_registry.c has {want!r}")

    # One stylesheet per supported style (style-depth.css is shared by Glow and Glass); a leftover file
    # of a removed style would keep its rules in the editor.
    css_files = {path.stem.removeprefix("style-") for path in (WWW / "css").glob("style-*.css")}
    for style_id in sorted(set(c_ids) - css_files):
        fail(f"css/style-{style_id}.css is missing for the supported style {style_id!r}")
    for stem in sorted(css_files - set(c_ids) - {"depth"}):
        fail(f"css/style-{stem}.css belongs to no supported style (ui_style_registry.c)")

    i18n = web_i18n_source()
    tables = language_tables(i18n)
    for style_id in c_ids:
        for key in (f"settings.appearance.{style_id}", f"settings.appearance.{style_id}_info"):
            for language in ("en", "de"):  # only en and de are built in
                if key not in tables.get(language, []):
                    fail(f"i18n.js {language}: {key} is missing")
        for table_name in ("STYLE_TITLE", "STYLE_SUMMARY"):
            if style_id not in getattr(gallery_content, table_name):
                fail(f"tools/gallery_content.py {table_name} has no {style_id!r}")


def check_toolchains():
    """Validate the unavoidable pre-checkout/installer literals against the CI targets."""
    expected = tomllib.loads((ROOT / "pyproject.toml").read_text())["tool"]["betta"]["ci"]
    for name in ("check.yml", "docs.yml"):
        workflow = (ROOT / ".github/workflows" / name).read_text()
        for key, target in (("python-version", "python"), ("version", "uv")):
            values = re.findall(r"^\s+" + key + r":\s*['\"]?([\d.]+)['\"]?\s*$", workflow, re.M)
            if values != [expected[target]]:
                fail(f"{name}: {key} {values} differs from pyproject.toml target {expected[target]}")
        if name == "check.yml":
            values = re.findall(r"node-version:\s*['\"]?(\d+)", workflow)
            if values != [expected["node"]]:
                fail(f"{name}: node-version differs from pyproject.toml")
    workflow = (ROOT / ".github/workflows/build.yml").read_text()
    if re.findall(r"container:\s*espressif/idf:v([^@\s]+)", workflow) != [expected["idf"]]:
        fail("build.yml: IDF image version differs from pyproject.toml")


if __name__ == "__main__":
    check_toolchains()
    check_markdown_links()
    check_editor_files()
    check_html()
    check_web_i18n()
    check_widget_help_summaries()
    check_firmware_i18n()
    check_firmware_translation_keys()
    check_widget_type_tables()
    check_style_registry()
    for e in errors:
        print("FAIL", e)
    print("web checks:", "FAILED" if errors else "ok")
    sys.exit(1 if errors else 0)
