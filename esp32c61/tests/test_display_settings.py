import ast
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import threading
import time
from urllib.request import Request, urlopen
from urllib.error import HTTPError
import unittest

import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'pc_tools'))
import bmode_imaging as b
import bmode_settings as model
import bmode_web_viewer as web
import bmode_viewer_service as service_module
import wifi_config_builder as builder
from test_bmode_imaging import payload, wait_for


class SettingsTests(unittest.TestCase):
    def setUp(self):
        self.default = model.defaults(b.load_lut())

    def test_ranges_types_lut_points_angles_and_geometry(self):
        self.assertEqual(model.validate(self.default), self.default)
        for key, value in [('dynamic_range_db', 71), ('contrast', .1), ('depth_mm', 189),
                           ('noise_floor_db', float('nan')), ('tgc_enabled', 1),
                           ('angles_deg', [0]*64), ('lut', [0]*255),
                           ('tgc_points', [[0, 0], [6143, 25]])]:
            v = deepcopy(self.default);v[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):model.validate(v)
        for key, value in [('unknown', 0), ('version', True)]:
            v = deepcopy(self.default);v[key] = value
            with self.assertRaises(ValueError):model.validate(v)
        for p in ([[0, 0], [0, 1], [6143, 0]], [[1, 0], [6143, 0]]):
            v = deepcopy(self.default);v['tgc_points'] = p
            with self.assertRaises(ValueError):model.validate(v)

    def test_explicit_profile_angle_decode_and_five_depth_mappings(self):
        angles = np.linspace(-40, 50, 64)
        encoded = [int(round(x)) & 0xfffff for x in np.sin(np.deg2rad(angles))*(1 << 18)]
        value = model.from_profile({'bram': {'sin_beta': encoded}}, self.default)
        np.testing.assert_allclose(value['angles_deg'], angles, atol=.001)
        value['angles_deg'] = [-45+90*(i/63)**1.2 for i in range(64)]
        r = b.Renderer();r.apply_settings(value)
        for d in range(5):
            r.configure(d)
            self.assertEqual(r.gain.size, 6144 >> d)
            np.testing.assert_allclose(r.gain, b.tgc_gain()[::1 << d])
            self.assertTrue(r.scan['valid_flat'].any())
        with self.assertRaises(ValueError):model.from_profile({'bram': {'sin_beta': [0x40000]*64}}, self.default)

    def test_reference_mapping_all_controls_lut_and_tgc(self):
        path = Path(r'D:/MyProjects/py_prj/ultrasound_dev_kit/usb_graymap_bmode_pipeline.py')
        tree = ast.parse(path.read_text(encoding='utf-8-sig'))
        nodes = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'apply_bmode_mapping']
        ns = {'np': np};exec(compile(ast.Module(body=nodes, type_ignores=[]), str(path), 'exec'), ns)
        rng = np.random.default_rng(41)
        data = rng.integers(0, 65536, (64, 1536), dtype=np.uint16)
        for dr, bright, contrast, floor in [(20, -10, .5, -60), (70, 20, 2, -20), (45, 3, 1, -45)]:
            s = deepcopy(self.default);s.update(dynamic_range_db=dr, brightness_db=bright, contrast=contrast, noise_floor_db=floor)
            s['lut'] = list(reversed(s['lut']));s['tgc_enabled'] = False
            r = b.Renderer();r.apply_settings(s);r.configure(2)
            cart = b.scan_convert(data.astype(np.float32)/16384, r.scan)
            expected = ns['apply_bmode_mapping'](cart, (dr, bright, contrast, floor), r.lut, float(np.percentile(cart[np.isfinite(cart)], 99.9)))
            np.testing.assert_array_equal(r.gray(data), expected)

    def test_revision_thread_application_busy_geometry_and_no_old_publish(self):
        w = b.ImagingWorker();self.addCleanup(w.close)
        w.reset();w.submit(1, 1, payload(down=3));wait_for(lambda: w.snapshot()['image_available'])
        value = deepcopy(self.default);value['brightness_db'] = 8
        w.set_settings(value, 1)
        wait_for(lambda: w.snapshot()['image_settings_revision'] == 2)
        self.assertEqual(w.snapshot()['image_display_settings']['brightness_db'], 8)
        self.assertEqual(w.snapshot()['samples_per_line'], 768)
        with self.assertRaises(RuntimeError):w.set_settings(value, 1)
        value['depth_mm'] = 100
        w.set_settings(value, 2)
        wait_for(lambda: w.snapshot()['image_settings_revision'] == 3)
        self.assertEqual(w.snapshot()['image_display_settings']['depth_mm'], 100)
        sequence, png, meta=w.latest_with_metadata(0)
        self.assertEqual(sequence,w.snapshot()['image_sequence'])
        self.assertTrue(png.startswith(b'\x89PNG'))
        self.assertEqual(meta['revision'],3)
        self.assertEqual(meta['settings']['depth_mm'],100)
        self.assertTrue(w.accepting)
        value['angles_deg'] = list(np.linspace(-40, 40, 64))
        with self.assertRaises(RuntimeError):w.set_settings(value, 3)
        w.finish();w.set_settings(value, 3);wait_for(lambda: w.get_settings()['applied_revision'] == 4)

    def test_http_origin_body_limits_revision_and_no_device_writes(self):
        def forbidden(*args):raise AssertionError('hardware I/O')
        s = service_module.ViewerService('127.0.0.1', api=forbidden)
        server = web.make_server(s, ('127.0.0.1', 0))
        thread = threading.Thread(target=server.serve_forever, daemon=True);thread.start()
        self.addCleanup(s.close);self.addCleanup(server.server_close);self.addCleanup(server.shutdown)
        base = 'http://127.0.0.1:'+str(server.server_port)
        with urlopen(base+'/api/viewer/settings') as r:state = json.load(r)
        def put(body, **headers):return urlopen(Request(base+'/api/viewer/settings', json.dumps(body).encode(), method='PUT', headers={'Content-Type':'application/json', **headers}))
        body = {'revision':state['revision'], 'settings':self.default}
        with put(body) as r:self.assertEqual(r.status, 202)
        with self.assertRaises(HTTPError) as e:put(body)
        self.assertEqual(e.exception.code, 409)
        for bad in ({'commands':['HV ENABLE']}, {'revision':2,'settings':{},'padding':'x'*32768}):
            with self.assertRaises(HTTPError) as e:put(bad)
            self.assertEqual(e.exception.code, 400)
        with self.assertRaises(HTTPError) as e:put(body, Origin='http://evil.invalid')
        self.assertEqual(e.exception.code, 403)
        with urlopen(base+'/viewer_settings.js') as r:self.assertIn(b'displayConfig', r.read())

    def test_setting_change_during_render_cannot_publish_old_revision(self):
        class Slow(b.Renderer):
            def __init__(self):
                super().__init__();self.entered=threading.Event();self.release=threading.Event();self.calls=0
            def png(self, samples):
                self.calls+=1
                if self.calls == 1:self.entered.set();self.release.wait(2)
                return super().png(samples)
        renderer=Slow();w=b.ImagingWorker(renderer);self.addCleanup(w.close)
        w.reset();w.submit(1,1,payload(down=4));self.assertTrue(renderer.entered.wait(2))
        changed=deepcopy(self.default);changed['brightness_db']=10;w.set_settings(changed,1)
        self.assertFalse(w.snapshot()['image_available']);renderer.release.set()
        wait_for(lambda:w.snapshot()['image_settings_revision']==2)
        self.assertEqual(w.snapshot()['images'],1)
        self.assertEqual(w.snapshot()['image_display_settings']['brightness_db'],10)

    def test_live_depth_all_decimations_and_gray_smoothing_continuity(self):
        w=b.ImagingWorker();self.addCleanup(w.close);w.reset()
        for down in range(5):
            w.submit(1,down+1,payload(down=down));wait_for(lambda:w.snapshot()['image_frame']==down+1)
            settings=w.get_settings();value=deepcopy(settings['settings']);value['depth_mm']=80+down*10
            old_png=w.latest(0)[1]
            w.set_settings(value,settings['revision'])
            self.assertTrue(w.snapshot()['image_available'])
            wait_for(lambda:w.snapshot()['image_settings_revision']==settings['revision']+1)
            self.assertEqual(w.snapshot()['image_display_settings']['depth_mm'],80+down*10)
            self.assertEqual(w.snapshot()['samples_per_line'],6144>>down)
            self.assertIsNotNone(old_png)
        r=b.Renderer();r.reference=1.23;s=deepcopy(self.default);s['brightness_db']=9;r.apply_settings(s)
        self.assertEqual(r.reference,1.23)
        s['tgc_enabled']=False;r.apply_settings(s);self.assertIsNone(r.reference)

    def test_service_allows_live_depth_without_any_device_request(self):
        def forbidden(*args):raise AssertionError('hardware I/O')
        s=service_module.ViewerService('127.0.0.1',api=forbidden);self.addCleanup(s.close)
        # Exercise admission during reception without launching a control worker.
        s.state='running';s.imaging.reset();s.imaging.submit(1,1,payload(down=2))
        wait_for(lambda:s.imaging.snapshot()['image_available'])
        v=deepcopy(self.default);v['depth_mm']=150
        s.set_display_settings(v,1)
        wait_for(lambda:s.imaging.snapshot()['image_settings_revision']==2)
        self.assertEqual(s.state,'running')
        self.assertEqual(s.imaging.snapshot()['image_display_settings']['depth_mm'],150)
        s.state='idle'


class BrowserGoldenTests(unittest.TestCase):
    def test_browser_builders_match_python_tables_and_afe(self):
        root = Path(__file__).resolve().parents[1]
        reference = Path(r'D:/MyProjects/py_prj/ultrasound_dev_kit/tools/configs')
        for file in sorted(reference.glob('*_default.json')):
            data = json.loads(file.read_text())
            if not {'afe5832','bram','tx7332'} & data.keys():continue
            if 'afe5832' in data:
                # Parameterized DTGC is deliberately excluded in the web editor.
                data['afe5832'].pop('dtgc', None)
            if 't_timing' in data.get('bram', {}):
                data['bram']['t_timing'][0] = data['bram']['t_timing'][8] = 6144
            expected = builder.build_afe_commands(data)+builder.build_tx7332_commands(data,8)+builder.build_bram_commands(data,8)
            code = "const P=require('./main/parameters_core.js');let s='';process.stdin.on('data',x=>s+=x);process.stdin.on('end',()=>console.log(JSON.stringify(P.build(JSON.parse(s)))));"
            result = subprocess.run(['node','-e',code],cwd=root,input=json.dumps(data),capture_output=True,text=True,check=True)
            self.assertEqual(json.loads(result.stdout), expected, file.name)


if __name__ == '__main__':unittest.main()
