# SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
import copy
from pathlib import Path
import sys
import unittest
import json
import tempfile
import subprocess
import os
import shutil
import importlib.util
import contextlib
import io
import time
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from widget_cases import state_fixture
import test_layouts
import gallery_content
import gallery_freshness
import check_simulator_fields
import check_widget_layout
import check_readability
from PIL import Image
from widget_cases import catalogue


class StateFixturesTest(unittest.TestCase):
    def test_overrides_are_isolated_and_keep_room_membership(self):
        pages = [{"widgets": [{"id": "a", "entity_id": "sensor.temp", "room_extra_entity_id": "media_player.music",
                               "room_entities": ["media_player.music"]},
                              {"id": "b", "entity_id": "media_player.music"}]}]
        states = [{"entity_id": "media_player.music", "state": "playing", "attributes": {"supported_features": 1}}]
        original = copy.deepcopy((pages, states))
        result, readings = state_fixture(pages, states, {"a": {"room_extra_entity_id": "paused"}})
        clone = result[0]["widgets"][0]["room_extra_entity_id"]
        self.assertIn(clone, result[0]["widgets"][0]["room_entities"])
        self.assertEqual(result[0]["widgets"][1]["entity_id"], "media_player.music")
        self.assertEqual(readings[-1]["state"], "paused")
        self.assertEqual(readings[-1]["attributes"]["supported_features"], 1)
        self.assertEqual((pages, states), original)

    def test_synthetic_states_never_enter_generated_device_layouts(self):
        suites = test_layouts.all_suites()
        self.assertGreater(len(suites["zustaende"]["cases"]), 40)
        self.assertTrue(any(c["type"] == "roborock_tile" for c in suites["zustaende"]["cases"]))
        for board in test_layouts.BOARDS:
            for layout in test_layouts.generate(board, test_layouts.DEMO_ROLES).values():
                for page in layout["pages"]:
                    for widget in page["widgets"]:
                        self.assertNotIn("sim_state", widget)
                        self.assertNotIn(".betta_state_", str(widget))


class SharedCatalogueTest(unittest.TestCase):
    def test_gallery_only_cannot_grow_packs_or_satisfy_test_coverage(self):
        data = catalogue()
        expected = {board: test_layouts.generate(board, test_layouts.DEMO_ROLES) for board in test_layouts.BOARDS}
        data["gallery_only"].append({"type": "sensor", "w": 10000, "h": 10000, "entity_id": "{temp}"})
        with patch.object(test_layouts, "catalogue", return_value=data):
            actual = {board: test_layouts.generate(board, test_layouts.DEMO_ROLES) for board in test_layouts.BOARDS}
        self.assertEqual(actual, expected)
        for suite in data["suites"].values():
            suite["cases"] = [case for case in suite["cases"] if case.get("type") != "sensor"]
        with patch.object(test_layouts, "catalogue", return_value=data):
            self.assertIn("widget type sensor", test_layouts.coverage(test_layouts.all_suites()))

    def test_shared_options_reach_both_gallery_and_pack(self):
        data = catalogue()
        case = next(c for suite in data["suites"].values() for c in suite["cases"]
                    if c.get("type") == "sensor" and "gallery" in c)
        case["sensor_show_age"] = False
        entities = json.loads((gallery_content.REPO_ROOT / "tests/simulator/gallery_data.json").read_text())["entities"]
        with patch.object(gallery_content, "catalogue", return_value=data):
            spec = gallery_content.build_spec(entities)
        tile = next(t for page in spec for t in page["tiles"] if t["title"] == case["gallery"]["title"])
        self.assertIs(tile["fields"]["sensor_show_age"], False)
        with patch.object(test_layouts, "catalogue", return_value=data):
            layouts = test_layouts.generate("panel4", test_layouts.DEMO_ROLES)
        widgets = [w for layout in layouts.values() for page in layout["pages"] for w in page["widgets"]
                   if w["type"] == "sensor" and w["title"] == case["title"]]
        self.assertTrue(widgets)
        self.assertTrue(all(w["sensor_show_age"] is False for w in widgets))
        self.assertTrue(all("gallery" not in w for w in widgets))

    def test_missing_gallery_tag_fails_instead_of_silently_dropping_image(self):
        data = catalogue()
        case = next(c for suite in data["suites"].values() for c in suite["cases"]
                    if c.get("gallery", {}).get("order") == 0)
        del case["gallery"]
        with patch.object(gallery_content, "catalogue", return_value=data):
            with self.assertRaisesRegex(ValueError, "missing or duplicate tag"):
                gallery_content.build_spec({})

    def test_overview_opens_browser_after_all_requested_boards(self):
        layouts = test_layouts.generate("panel4", test_layouts.DEMO_ROLES)
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            with patch.object(test_layouts, "render_board", return_value=(layouts, {0: {}})) as render, \
                 patch.object(test_layouts.subprocess, "run") as browser, contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(test_layouts.view(["panel4", "tab5", "paneljc"], ["flat"], [0], ["de", "en"],
                                                  output, True, "classic_v1"), 0)
                self.assertEqual([call.args[0] for call in render.call_args_list], ["panel4", "tab5", "paneljc"])
                self.assertTrue(all(call.kwargs["theme"] == "classic_v1" for call in render.call_args_list))
                browser.assert_called_once_with(["open", str(output / "index.html")], check=False)
                self.assertTrue((output / "index.html").is_file())

    def test_old_bambu_style_outputs_are_not_kept(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            for style in ("flat", "glow", "glass", "matte"):
                (folder / f"gallery_bambu_detail_{style}.png").touch()
            kept = gallery_content.collect_images(folder, [])
            self.assertEqual(set(kept), {("glow", "gallery_bambu_detail")})

    def test_catalogue_and_helper_changes_invalidate_gallery_stamp(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "tests/simulator").mkdir(parents=True)
            (root / "tests/simulator/CMakeLists.txt").write_text("")
            (root / "tools").mkdir()
            inputs = ["tools/test_layout_cases.json", "tools/widget_cases.py"]
            for name in inputs:
                (root / name).write_text("before")
            with patch.object(gallery_freshness, "REPO", root), patch.object(gallery_freshness, "SOURCES_JSON", root / "sources.json"):
                gallery_freshness.write_stamp()
                self.assertEqual(gallery_freshness.changed_since_stamp(), ([], [], []))
                for name in inputs:
                    (root / name).write_text("after")
                changed, added, removed = gallery_freshness.changed_since_stamp()
                self.assertEqual(changed, inputs)
                self.assertFalse(added or removed)


class CheckerFailuresTest(unittest.TestCase):
    def test_shared_clock_preserves_explicit_real_mode(self):
        from generate_simulator_gallery import use_fixed_clock, FIXED_TZ
        try:
            with patch.dict(os.environ, {}, clear=True):
                self.assertEqual(use_fixed_clock(), 1792058400)
                self.assertEqual(os.environ["TZ"], FIXED_TZ)
                self.assertEqual(use_fixed_clock("1792058400"), 1792058400)
                self.assertIsNone(use_fixed_clock("real"))
                self.assertEqual(os.environ["BETTA_SIM_NOW"], "real")
        finally:
            time.tzset()

    def test_layout_child_failure_cannot_pass_the_overview(self):
        layout = {"one": {"pages": [{"id": "one", "widgets": []}]}}
        for rc, stdout, stderr in ((0, "widget layout: ok\n", ""), (0, "", ""), (2, "", "synthetic crash"),
                                   (1, "widget layout: ok\n", ""), (1, "bad field\nFAILED\n", ""), (2, "", "")):
            with self.subTest(rc=rc, stdout=stdout), tempfile.TemporaryDirectory() as folder:
                def run(argv, **kwargs):
                    if "--screenshot" in argv:
                        Image.new("RGB", (1, 1)).save(argv[argv.index("--screenshot") + 1], format="PNG")
                        return subprocess.CompletedProcess(argv, 0)
                    return subprocess.CompletedProcess(argv, rc, stdout, stderr)
                with patch.object(test_layouts, "generate", return_value=layout), \
                     patch.object(test_layouts, "demo_states", return_value=([], {})), \
                     patch("generate_simulator_gallery.build_simulator", return_value="synthetic-renderer"), \
                     patch.object(test_layouts.subprocess, "run", side_effect=run), \
                     patch.object(test_layouts, "coverage", return_value=[]), contextlib.redirect_stdout(io.StringIO()) as output:
                    result = test_layouts.simulate("panel4", None, ["flat"], [0], ["de"], Path(folder) / "out")
                self.assertEqual(result, int(rc != 0 or not stdout.strip()), output.getvalue())
                if rc or not stdout.strip():
                    self.assertIn(f"layout checker exited {rc}", output.getvalue())
                    if stderr:
                        self.assertIn(stderr, output.getvalue())

    def test_field_name_elsewhere_does_not_replace_parsing(self):
        with tempfile.TemporaryDirectory() as folder:
            firmware, simulator = (Path(folder) / name for name in ("firmware.c", "simulator.c"))
            firmware.write_text('static bool ui_runtime_widget_from_json() { read(widget_json, "sensor_format"); }')
            for body, expected in (('read("sensor_format");', 0), ('read("other");', 1)):
                simulator.write_text('static void parse_widget_def() {' + body + '}\n// "sensor_format" elsewhere')
                with patch.object(check_simulator_fields, "FIRMWARE", firmware), \
                     patch.object(check_simulator_fields, "SIMULATOR", simulator), contextlib.redirect_stdout(io.StringIO()) as output:
                    self.assertEqual(check_simulator_fields.main(), expected)
                if expected:
                    self.assertIn("sensor_format", output.getvalue())

    def test_widget_registration_gaps_name_the_type_and_place(self):
        import check_web
        import generate_widget_docs as guides
        check_web.errors.clear()
        check_web.check_widget_type_tables()
        self.assertEqual(check_web.errors, [])  # All current types, aliases and custom creators need no exceptions.
        read = Path.read_text
        mutations = (
            ("ui_widget_factory.c", '"sensor"', '"missing_sensor"', "factory.create, factory.apply_state, factory.mark_unavailable"),
            ("tile-previews.js", 'case "sensor":', 'case "missing_sensor":', "editor.preview"),
            ("state.js", 'case "sensor":', 'case "missing_sensor":', "editor.sizes"),
            ("config.js", 'Set(["wallbox_evcc", "sensor", ', 'Set(["wallbox_evcc", ', "editor.names"),
            ("bind.js", 'openLightEntityPicker("sensor")', 'openLightEntityPicker("missing_picker")', "editor.add"),
            ("bind.js", 'addBambuPrinterWidget()', 'missingCustomCreator()', "editor.add"),
        )
        for suffix, needle, replacement, places in mutations:
            kind = "bambu_printer" if "Custom" in replacement else "sensor"
            with self.subTest(place=places, kind=kind):
                def mutate(path, *args, **kwargs):
                    text = read(path, *args, **kwargs)
                    return text.replace(needle, replacement) if path.name == suffix else text
                with patch.object(Path, "read_text", mutate):
                    check_web.errors.clear()
                    check_web.check_widget_type_tables()
                self.assertEqual(check_web.errors, [f"widget {kind}: missing {places}"])
        with patch.object(guides, "size_limits", return_value={"unregistered_widget": ()}):
            check_web.errors.clear()
            check_web.check_widget_type_tables()
        self.assertEqual(len(check_web.errors), 1)
        self.assertTrue(check_web.errors[0].startswith("widget unregistered_widget: missing "))
        self.assertEqual(len(check_web.errors[0].split(": missing ")[1].split(", ")), 9)
        for count in (0, 2):
            groups = [("test", "", [k for _, _, ks in guides.GROUPS for k in ks if k != "sensor"] + ["sensor"] * count)]
            with patch.object(guides, "WIDGETS", {k: v for k, v in guides.WIDGETS.items() if k != "sensor"}), \
                 patch.object(guides, "GROUPS", groups):
                check_web.errors.clear()
                check_web.check_widget_type_tables()
            self.assertEqual(check_web.errors, ["widget sensor: missing docs.content, docs.group (expected once)"])
        check_web.errors.clear()

    def test_geometry_clipping_and_missing_content_are_rejected(self):
        card = dict(widget="one", kind="card", type="sensor", x1=0, y1=0, x2=100, y2=100)
        label = dict(widget="one", kind="label", text="21.5", x1=5, y1=5, x2=30, y2=20)
        self.assertFalse(check_widget_layout.find_problems([card, label]))
        self.assertTrue(any("overlaps" in p for p in check_widget_layout.find_problems([card, label, dict(label, text="C")])))
        self.assertTrue(any("outside" in p for p in check_widget_layout.find_problems([card, dict(label, x2=103)])))
        clipped = dict(label, essential=True, text_clipped=True, font_px=20, H_arcmin_30cm=18)
        self.assertTrue(any("clipped" in p for p in check_readability.problems([clipped])))
        layout = {"pages": [{"id": "page", "widgets": [{"id": "one", "type": "sensor", "title": "Temperature"}]}]}
        with patch.object(test_layouts, "EXPECT", {"one": ["21.5"]}):
            self.assertTrue(test_layouts.content_problems(layout, {"page": []}))
            self.assertFalse(test_layouts.content_problems(layout, {"page": [label]}))


class CheckRunnerTest(unittest.TestCase):
    def run_check(self, arguments, paths=None, untracked=(), fail_node=False, ci=False, no_node=False,
                  stub_tools=False, node_version="test-node", workspace=None, log_directory=None, probe="before"):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            repo = Path(workspace) if workspace else folder / "repo"
            for directory in ("tools", "main/ui/fonts", "components/webui/www/js", "tests/webui", "docs"):
                (repo / directory).mkdir(parents=True, exist_ok=True)
            shutil.copy2(root / "tools/check.sh", repo / "tools/check.sh")
            for name in ("pyproject.toml", "uv.lock", ".gitignore"):
                shutil.copy2(root / name, repo / name)
            (repo / "tools/cache-input").write_text(probe)
            (repo / "components/webui/www/js/example.js").write_text("// synthetic check input\n")
            bindir = folder / "bin"
            bindir.mkdir()
            env = dict(os.environ, BETTA_CHECK_LOG_DIR=str(log_directory or folder / "logs"), PYTHONDONTWRITEBYTECODE="1")
            env.pop("CI", None)
            if ci:
                env["CI"] = "1"
            if paths is not None:
                for name, values in (("tracked", paths), ("untracked", untracked)):
                    (folder / name).write_bytes(b"".join(str(value).encode() + b"\0" for value in values))
                (bindir / "git").write_text('#!/bin/sh\ncase "$1" in diff) cat "' + str(folder / "tracked") +
                                            '";; ls-files) cat "' + str(folder / "untracked") + '";; *) exit 99;; esac\n')
                (bindir / "git").chmod(0o700)
            if fail_node or stub_tools:
                # Stub execution boundaries; the actual runner and its stamps operate in an isolated tree.
                env["CHECK_TEST_NODE_VERSION"] = node_version
                for name, body in (("node", 'if [ "$1" = --version ]; then echo "$CHECK_TEST_NODE_VERSION"; exit 0; fi; exit ' + ("71" if fail_node else "0")),
                                   ("python3", 'if [ "$1" = --version ]; then echo test-python; fi; exit 0')):
                    (bindir / name).write_text("#!/bin/sh\n" + body + "\n")
                    (bindir / name).chmod(0o700)
            if no_node:
                for name in ("bash", "dirname", "mkdir", "head", "rm"):
                    (bindir / name).symlink_to(shutil.which(name))
            env["PATH"] = str(bindir) + ("" if no_node else ":" + str(root / ".venv/bin") + ":" + os.environ["PATH"])
            return subprocess.run(["bash", str(repo / "tools/check.sh"), *arguments], cwd=folder,
                                  env=env, capture_output=True, text=True)

    def test_help_invalid_arguments_and_no_json_interface(self):
        self.assertEqual(self.run_check(["--help"]).returncode, 0)
        for arguments in (["--group"], ["--quick", "--full"], ["--quick", "--build"],
                          ["--build", "--group", "web"], ["--json"], ["--group", "invalid"]):
            self.assertEqual(self.run_check(arguments).returncode, 2, arguments)

    def test_quick_and_ci_full_boundaries(self):
        quick = self.run_check(["--quick", "--list"])
        self.assertEqual(quick.returncode, 0)
        self.assertIn("tooling tests <-", quick.stdout)
        self.assertNotIn("widget layout", quick.stdout)
        self.assertNotIn("host tests panel4 <-", quick.stdout)
        full = self.run_check(["--group", "sim-tab5", "--list"], ci=True)
        self.assertIn("widget layout tab5 step 2", full.stdout)
        self.assertNotIn("widget layout panel4", full.stdout)
        self.assertEqual(self.run_check(["--quick"], ci=True).returncode, 2)
        builds = self.run_check(["--build", "--list"])
        self.assertEqual(builds.returncode, 0)
        self.assertEqual(sum(line.startswith("build ") for line in builds.stdout.splitlines()), 5)
        docs = self.run_check(["--group", "docs", "--list"])
        self.assertIn("documentation build", docs.stdout)
        self.assertNotIn("tooling tests", docs.stdout)
        self.assertNotIn("host tests panel4 <-", docs.stdout)

    def test_changed_uses_the_declared_step_inputs(self):
        # Expected responsibilities, not a second production path mapping.
        editor = self.run_check(["--changed", "--list"], paths=["components/webui/www/js/config.js"])
        self.assertEqual(editor.returncode, 0)
        self.assertIn("editor unit tests", editor.stdout)
        self.assertNotIn("widget layout", editor.stdout)
        docs = self.run_check(["--changed", "--list"], paths=["docs/example.md"])
        self.assertIn("documentation build", docs.stdout)
        self.assertNotIn("editor unit tests", docs.stdout)
        for path in ("main/ui/widgets/w_sensor.c", "main/net/wifi_mgr.c"):
            result = self.run_check(["--changed", "--list"], paths=[path])
            self.assertIn("host tests panel4", result.stdout)
            self.assertIn("widget layout tab5", result.stdout)

    def test_empty_unknown_deleted_renamed_and_untracked_inputs(self):
        empty = self.run_check(["--changed", "--list"], paths=[])
        self.assertEqual(empty.returncode, 0)
        self.assertEqual(empty.stdout, "")
        unknown = self.run_check(["--changed", "--list"], paths=["unknown-input"])
        self.assertIn("host tests", unknown.stdout)
        moved = self.run_check(["--changed", "--list"], paths=["main/ui/old file.c", "docs/new file.md"])
        self.assertIn("widget layout", moved.stdout)
        self.assertIn("documentation build", moved.stdout)
        new = self.run_check(["--changed", "--list"], paths=[], untracked=["tests/host/new\ncase.c"])
        self.assertIn("host tests", new.stdout)
        self.assertNotIn("editor unit tests", new.stdout)

    def test_failure_stops_by_default_and_keep_going_is_explicit(self):
        failed = self.run_check(["--quick", "--force"], fail_node=True)
        self.assertEqual(failed.returncode, 1)
        self.assertEqual(len(failed.stdout.splitlines()), 1)
        self.assertIn("selected=1", failed.stdout)
        self.assertIn("Repeat:", failed.stderr)
        continued = self.run_check(["--quick", "--force", "--keep-going"], fail_node=True)
        self.assertEqual(continued.returncode, 1)
        self.assertIn("selected=10", continued.stdout)
        self.assertIn("passed=8", continued.stdout)

    def test_missing_requirement_is_not_a_ci_pass(self):
        result = self.run_check(["--group", "web"], ci=True, no_node=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("unavailable=1", result.stdout)

    def test_stamps_follow_real_inputs_and_tool_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            options = dict(stub_tools=True, workspace=Path(folder) / "repo", log_directory=Path(folder) / "logs")
            first = self.run_check(["--quick", "--force"], **options)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertIn("passed=10", first.stdout)
            warm = self.run_check(["--quick"], **options)
            self.assertIn("cached=10", warm.stdout)
            changed = self.run_check(["--quick"], probe="after", **options)
            self.assertEqual(changed.returncode, 0)
            self.assertNotIn("cached=10", changed.stdout)
            self.assertNotIn("passed=0", changed.stdout)
            version = self.run_check(["--quick"], probe="after", node_version="test-node-v2", **options)
            self.assertIn("passed=10", version.stdout)
            self.assertIn("cached=0", version.stdout)

    def test_script_line_budget(self):
        root = Path(__file__).resolve().parents[2]
        self.assertLessEqual(len((root / "tools/check.sh").read_text().splitlines()), 191)


class SizeCheckerTest(unittest.TestCase):
    def test_documentation_limits_and_exemptions_are_preserved(self):
        root = Path(__file__).resolve().parents[2]
        spec = importlib.util.spec_from_file_location("sizes", root / "tools/check_source_sizes.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.LEGACY_CEILINGS, {"main/ha/ha_client.c": 7799})
        with tempfile.TemporaryDirectory() as folder:
            docs = Path(folder)
            (docs / "widgets").mkdir()
            (docs / "widgets/long.md").write_text("x\n" * 1000)
            (docs / "gallery-custom.md").write_text("x\n" * 1000)
            (docs / "page.md").write_text("x\n" * 300)
            (docs / "release-notes.md").write_text("x\n" * 800)
            with patch.object(module, "DOCS", docs), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(module.check_docs(), 0)
                (docs / "page.md").write_text("x\n" * 301)
                self.assertEqual(module.check_docs(), 1)
                (docs / "page.md").unlink()
                (docs / "release-notes.md").write_text("x\n" * 801)
                self.assertEqual(module.check_docs(), 1)


if __name__ == "__main__":
    unittest.main()
