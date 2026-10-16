import ast
from io import BytesIO
from pathlib import Path
import sys
import threading
import time
import unittest
from unittest.mock import patch

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'pc_tools'))
import bmode_imaging as b


def payload(ids=range(64), value=20000):
    rows = np.full((len(ids), b.LINE_WORDS), value, dtype='<u2')
    rows[:, :8] = 0
    rows[:, 0], rows[:, 1], rows[:, 2] = 0x1c00, 0x1800, list(ids)
    return rows.tobytes()


def wait_for(predicate, timeout=3):
    until = time.monotonic()+timeout
    while time.monotonic() < until:
        if predicate():
            return
        time.sleep(.01)
    raise AssertionError('condition did not become true')


class ParseTests(unittest.TestCase):
    def test_all_lines_and_terminal_line(self):
        decoded = b.parse_lines(payload())
        self.assertEqual(set(decoded), set(range(64)))
        np.testing.assert_array_equal(decoded[63], np.full(6144, 20000, dtype=np.uint16))

    def test_endian_invalid_identity_odd_isolated_and_short(self):
        for data in (payload([1]), payload([1, 2])[:20], b'odd',
                     np.frombuffer(payload([1, 2]), '<u2').astype('>u2').tobytes(),
                     payload([64, 65]), bytes(1048578)):
            self.assertEqual(b.parse_lines(data), {})

    def test_truncated_neighbor_can_confirm_complete_preceding_line(self):
        self.assertEqual(set(b.parse_lines(payload([2, 3])[:-100])), {2})

    def test_false_header_inside_payload_does_not_destroy_real_spacing(self):
        words = np.frombuffer(payload([1, 2]), '<u2').copy()
        words[30:33] = [0x1c00, 0x1800, 8]
        self.assertEqual(set(b.parse_lines(words.tobytes())), {1, 2})

    def test_last_duplicate_wins(self):
        data = payload([1, 2], 100)+payload([1, 2], 60000)
        self.assertEqual(int(b.parse_lines(data)[1][0]), 60000)

    def test_byte_offset_does_not_join_independent_snapshots(self):
        original = payload([1, 2])
        self.assertEqual(b.parse_lines(original[1:]), {})
        self.assertEqual(b.parse_lines(original[:100]), {})
        self.assertEqual(b.parse_lines(original[100:]), {})

    def test_cache_rolling_and_expiry_including_zero(self):
        cache = b.LineCache()
        cache.update(b.parse_lines(payload(range(32), 0)), 10.)
        self.assertEqual(cache.coverage(10.), 32)
        cache.update(b.parse_lines(payload(range(32, 64))), 11.)
        self.assertEqual(cache.coverage(11.), 64)
        self.assertEqual(cache.coverage(15.01), 32)
        self.assertEqual(cache.coverage(16.01), 0)
        self.assertEqual(int(cache.data[0, 0]), 0)

    def test_existing_bad_txt_is_rejected_not_displayed(self):
        names = ('frame_3e6e4163_00000002.txt', 'frame_d96df184_00000003.txt')
        files = [ROOT/'image_capture'/name for name in names if (ROOT/'image_capture'/name).exists()]
        if not files:
            self.skipTest('Historical captures not present')
        for file in files:
            words = np.loadtxt(file, dtype=np.uint16, converters=lambda x: int(x, 16))
            self.assertFalse(b.parse_lines(words.astype('<u2').tobytes()), file.name)


class RendererTests(unittest.TestCase):
    def test_png_and_lut(self):
        renderer = b.Renderer()
        self.assertEqual(renderer.lut.size, 256)
        self.assertFalse(np.array_equal(renderer.lut, np.arange(256, dtype=np.uint8)))
        image = Image.open(BytesIO(renderer.png(np.zeros((64, 6144), dtype=np.uint16))))
        self.assertEqual(image.mode, 'L')
        self.assertEqual(image.size, (883, 624))

    def test_reference_numerical_equivalence_without_importing_usb_or_gui(self):
        reference = Path(r'D:\MyProjects\py_prj\ultrasound_dev_kit\usb_graymap_bmode_pipeline.py')
        if not reference.exists():
            self.skipTest('Reference project not available')
        names = {'default_tgc_control_points', 'make_tgc_db_curve', 'tgc_db_to_gain', 'make_tgc_gain',
                 'q214_to_log10_float', 'apply_tgc_gain_log10_q214', 'full_imaging_depth_mm',
                 'clamp_display_depth_mm', 'precompute_scan_converter', 'scan_convert_bilinear',
                 'compute_log10_reference', 'smooth_log10_reference', 'apply_bmode_mapping'}
        tree = ast.parse(reference.read_text(encoding='utf-8-sig'))
        selected = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
        constants = [node for node in tree.body if isinstance(node, ast.Assign) and
                     all(isinstance(target, ast.Name) and target.id.startswith('DEFAULT_') for target in node.targets)]
        namespace = {'np': np}
        exec(compile(ast.Module(body=constants+selected, type_ignores=[]), str(reference), 'exec'), namespace)
        renderer, previous = b.Renderer(), None
        rng = np.random.default_rng(71)
        scan = namespace['precompute_scan_converter'](64, 6144, 120.)
        gain = namespace['make_tgc_gain']()
        np.testing.assert_allclose(gain, renderer.gain, rtol=1e-6)
        for _ in range(3):
            samples = rng.integers(0, 65536, (64, 6144), dtype=np.uint16)
            polar = namespace['apply_tgc_gain_log10_q214'](samples, gain)
            cart = namespace['scan_convert_bilinear'](polar, scan)
            previous = namespace['smooth_log10_reference'](previous, namespace['compute_log10_reference'](cart))
            expected = namespace['apply_bmode_mapping'](cart, (45., 3., 1., -45.), renderer.lut, previous)
            actual = renderer.gray(samples)
            self.assertLessEqual(int(np.abs(actual.astype(int)-expected.astype(int)).max()), 1)


class WorkerTests(unittest.TestCase):
    def make_worker(self, renderer=None):
        worker = b.ImagingWorker(renderer)
        self.addCleanup(worker.close)
        worker.reset()
        return worker

    def test_warmup_render_session_reset_and_invalid_payload(self):
        worker = self.make_worker()
        worker.submit(7, 1, payload(range(32)))
        wait_for(lambda: worker.snapshot()['lines'] == 32)
        self.assertFalse(worker.snapshot()['image_available'])
        worker.submit(7, 2, payload(range(32, 64)))
        wait_for(lambda: worker.snapshot()['image_available'])
        seq, png = worker.latest(0)
        self.assertTrue(png.startswith(b'\x89PNG'))
        self.assertIsNone(worker.latest(seq)[1])
        worker.submit(8, 1, b'invalid')
        wait_for(lambda: worker.snapshot()['format_errors'] == 1)
        self.assertFalse(worker.snapshot()['image_available'])
        self.assertEqual(worker.snapshot()['lines'], 0)

    def test_slow_renderer_has_bounded_queue_and_submit_does_not_wait(self):
        entered, release = threading.Event(), threading.Event()
        class Slow:
            def reset(self): pass
            def png(self, samples):
                entered.set();release.wait(2);return b'png'
        self.addCleanup(release.set)
        worker = self.make_worker(Slow())
        worker.submit(1, 1, payload())
        self.assertTrue(entered.wait(1))
        started = time.monotonic()
        for frame in range(2, 20):
            worker.submit(1, frame, payload())
        self.assertLess(time.monotonic()-started, .5)
        self.assertEqual(worker.snapshot()['queue_size'], 2)
        self.assertGreater(worker.snapshot()['queue_drops'], 0)
        worker.reset()
        release.set()
        time.sleep(.1)
        self.assertFalse(worker.snapshot()['image_available'])

    def test_close_rejects_late_callback_and_retains_last_image_on_finish(self):
        worker = self.make_worker()
        worker.submit(1, 1, payload())
        wait_for(lambda: worker.snapshot()['image_available'])
        worker.finish()
        self.assertTrue(worker.snapshot()['image_available'])
        worker.submit(2, 1, payload())
        self.assertEqual(worker.snapshot()['queue_size'], 0)

    def test_expired_lines_stop_publication_without_erasing_last_picture(self):
        with patch.object(b, 'LINE_TTL', .25):
            worker = self.make_worker()
            worker.submit(1, 1, payload())
            wait_for(lambda: worker.snapshot()['image_available'])
            sequence = worker.snapshot()['image_sequence']
            wait_for(lambda: worker.snapshot()['lines'] == 0)
            self.assertTrue(worker.snapshot()['image_available'])
            self.assertEqual(worker.snapshot()['image_sequence'], sequence)

    def test_renderer_failure_is_explicit(self):
        class Broken:
            def reset(self): pass
            def png(self, samples): raise ValueError('bad renderer')
        worker = self.make_worker(Broken())
        worker.submit(1, 1, payload())
        wait_for(lambda: bool(worker.snapshot()['processing_error']))
        self.assertFalse(worker.snapshot()['image_available'])

    def test_publication_rate_is_bounded(self):
        class Fast:
            def reset(self): pass
            def png(self, samples): return b'png'
        worker = self.make_worker(Fast())
        data, started, frame = payload(), time.monotonic(), 0
        while time.monotonic()-started < .4:
            frame += 1;worker.submit(1, frame, data);time.sleep(.005)
        elapsed = time.monotonic()-started
        self.assertGreater(worker.snapshot()['images'], 0)
        self.assertLessEqual(worker.snapshot()['images'], int(elapsed*b.MAX_FPS)+1)


if __name__ == '__main__':
    unittest.main()
