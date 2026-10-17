# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Exercise local saves/restart and device isolation through the actual HTTP API."""
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, build_opener, ProxyHandler

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from editor_local import build_server, demo_responses
from editor_local_demo import all_widgets_fixture
import re


class LocalEditorTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.state = Path(self.temp.name) / 'state.json'
        self.opener = build_opener(ProxyHandler({}))
        self.start()

    def start(self):
        self.server = build_server(port=0, state_path=self.state, default_ui='spike')
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f'http://127.0.0.1:{self.server.server_port}'

    def stop(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def tearDown(self):
        self.stop()
        self.temp.cleanup()

    def request(self, path, method='GET', body=None, origin=None):
        headers = {'Content-Type':'application/json'}
        if origin: headers['Origin'] = origin
        data = json.dumps(body).encode() if body is not None else None
        request = Request(self.base + path, data=data, headers=headers, method=method)
        try:
            with self.opener.open(request, timeout=5) as response:
                return response.status, response.read()
        except HTTPError as error:
            try: return error.code, error.read()
            finally: error.close()

    def test_save_reload_and_restart_share_one_layout(self):
        _, raw = self.request('/api/layout')
        layout = json.loads(raw)
        layout['pages'][0]['widgets'][0]['title'] = 'Local draft'
        status, _ = self.request('/api/layout', 'PUT', layout, self.base)
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(self.state.read_text())['/api/layout'], layout)
        # HTML view requests must not save or rewrite the persistent state.
        saved = self.state.read_bytes()
        for view in ('classic', 'spike'):
            status, html = self.request('/?ui=' + view)
            self.assertEqual(status, 200)
            self.assertIn(b'/__local__/local-editor.js', html)
            self.assertEqual(b'/__local__/spike.js' in html, view == 'spike')
            self.assertEqual(b'/__local__/entity-fields.js' in html, view == 'spike')
            if view == 'spike':
                self.assertLess(html.index(b'/__local__/entity-fields.js'), html.index(b'/__local__/spike.js'))
            self.assertEqual(self.state.read_bytes(), saved)
        self.stop()
        self.start()
        self.assertEqual(json.loads(self.request('/api/layout')[1]), layout)
        self.assertEqual(self.request('/__local__/entity-fields.js')[0], 200)

    def test_brightness_preview_is_temporary_and_updates_do_not_extend_it(self):
        original = json.loads(self.request('/api/settings')[1])
        status, raw = self.request('/api/display/test', 'POST', {'action':'start', 'kind':'night', 'percent':0})
        self.assertEqual(status, 200)
        self.assertTrue(json.loads(raw)['active'])
        self.assertTrue(json.loads(raw)['local_only'])
        until = self.server.local_store.display_test['until']
        _, raw = self.request('/api/display/test', 'POST', {'action':'update', 'percent':7})
        self.assertEqual(json.loads(raw)['brightness_percent'], 7)
        self.assertEqual(self.server.local_store.display_test['until'], until)
        self.assertEqual(json.loads(self.request('/api/settings')[1]), original)
        self.assertFalse(self.state.exists())
        self.assertEqual(self.request('/api/display/test', 'POST', {'action':'start','kind':'night','percent':101})[0], 400)
        self.assertEqual(self.request('/api/display/test', 'POST', {'action':'start','kind':'night','percent':True})[0], 400)
        self.assertFalse(json.loads(self.request('/api/display/test', 'POST', {'action':'stop'})[1])['active'])
        self.assertFalse(json.loads(self.request('/api/display/test', 'POST', {'action':'update','percent':3})[1])['active'])
        self.request('/api/display/test', 'POST', {'action':'start','kind':'dim','percent':20})
        self.server.local_store.display_test['until'] = 0
        self.assertFalse(json.loads(self.request('/api/display/test')[1])['active'])

    def test_display_preferences_survive_a_local_restart(self):
        hardware = {'night_enabled':False, 'night_brightness_percent':7, 'dim_brightness':20}
        self.assertEqual(self.request('/api/settings', 'PUT', {'hardware':hardware})[0], 200)
        self.stop(); self.start()
        for key,value in hardware.items():
            self.assertEqual(json.loads(self.request('/api/settings')[1])['hardware'][key], value)
        self.assertFalse(json.loads(self.request('/api/display/test')[1])['active'])

    def test_device_commands_and_foreign_origin_cannot_write(self):
        _, raw = self.request('/api/layout')
        self.assertEqual(self.request('/api/layout', 'PUT', json.loads(raw), 'https://example.org')[0], 403)
        for path in ('/api/reboot', '/api/ota/upload', '/api/ha/service', '/api/wifi'):
            self.assertEqual(self.request(path, 'POST', {})[0], 405)
        self.assertFalse(self.state.exists())
        self.assertEqual(self.request('/api/layout', 'PUT', {'pages':[]})[0], 400)
        self.assertFalse(self.state.exists())


class AllWidgetsDemoTest(unittest.TestCase):
    def test_all_types_bindings_geometry_and_save_budget(self):
        root = Path(__file__).resolve().parents[2]
        registry = (root / 'main/layout/widget_types.c').read_text().split('s_variant_sizes')[0]
        types = set(re.findall(r'\{"([a-z0-9_]+)",\s*\{', registry)) - {'roborock_tile'}
        for variant in ('paneljc', 'tab5'):
            with self.subTest(variant=variant):
                fixture = all_widgets_fixture(variant)
                widgets = [w for p in fixture['pages'] for w in p['widgets']]
                self.assertEqual({w['type'] for w in widgets}, types)
                self.assertEqual(len(fixture['pages']), 12)
                # Compact navigation needs a distinct, explicit icon for every page.
                icons = [page['icon'] for page in fixture['pages']]
                self.assertEqual(len(set(icons)), len(icons))
                self.assertNotIn('none', icons)
                entities = {s['entity_id'] for s in fixture['states']}

                def bindings(value):
                    if isinstance(value, dict):
                        for key, child in value.items():
                            if key.endswith('entity_id') and child:
                                self.assertIn(child, entities, key)
                            elif key == 'room_entities':
                                self.assertTrue(set(child) <= entities)
                            else: bindings(child)
                    elif isinstance(value, list):
                        for child in value: bindings(child)
                bindings(fixture['pages'])
                for page in fixture['pages']:
                    for i, widget in enumerate(page['widgets']):
                        r = widget['rect']
                        self.assertGreaterEqual(min(r['x'], r['y']), 0)
                        self.assertLessEqual(r['x'] + r['w'], 1280)
                        self.assertLessEqual(r['y'] + r['h'], 600)
                        for other in page['widgets'][i + 1:]:
                            b = other['rect']
                            self.assertTrue(r['x'] + r['w'] <= b['x'] or b['x'] + b['w'] <= r['x'] or
                                            r['y'] + r['h'] <= b['y'] or b['y'] + b['h'] <= r['y'])
                responses, templates = demo_responses(variant, all_widgets=True)
                self.assertLess(len(json.dumps(responses['/api/layout'], ensure_ascii=False).encode()), 32768)
                self.assertIn('empty_tile', templates)
                # Heavy Inspectors must exercise their optional bindings, not just show blank forms.
                for kind, minimum in [('energy_timeline', 7), ('car_widget', 6), ('bambu_printer', 10), ('wallbox_evcc', 11), ('vacuum_tile', 4)]:
                    sample = next(w for w in widgets if w['type'] == kind)
                    self.assertGreaterEqual(sum(bool(v) for k, v in sample.items() if k.endswith('entity_id')), minimum)

    def test_demo_uses_separate_default_save_file(self):
        with tempfile.TemporaryDirectory() as directory:
            # Inspect construction without persisting anything in the repository.
            original = build_server(port=0)
            demo = build_server(port=0, all_widgets=True)
            try:
                self.assertNotEqual(original.local_store.path, demo.local_store.path)
                self.assertEqual(len(demo.local_store.seed['/api/layout']['pages']), 12)
            finally:
                original.server_close()
                demo.server_close()
        with self.assertRaises(ValueError): all_widgets_fixture('panel4')


if __name__ == '__main__': unittest.main()
