import json
import io
import pathlib
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pc_tools'))
import sdio_udp_receiver as r


class PipelineTests(unittest.TestCase):
    def test_missing_indices_and_recovery(self):
        rx = r.FrameReceiver(3000)
        data = r.test_pattern(3000)
        rx.feed(r.encode_packet(data, 1, 1, 0), ('127.0.0.1', 1), 0)
        rx.feed(r.encode_packet(data, 1, 1, 2880), ('127.0.0.1', 1), .01)
        event = rx.expire(3)[0]
        self.assertEqual(event['missing_indices'], [1])
        self.assertEqual(event['missing_packets'], 1)
        for offset in (0, 1440, 2880):
            _, complete = rx.feed(r.encode_packet(data, 1, 2, offset), ('127.0.0.1', 1), 4)
        self.assertEqual(complete[1], data)

    def test_receive_queue_loss_is_bounded_and_visible(self):
        class BurstSocket:
            def __init__(self): self.n = 100
            def recvfrom(self, maximum):
                if not self.n: raise OSError('end of fixture')
                self.n -= 1
                return b'x', ('127.0.0.1', 1)
        pump = r.DatagramPump(BurstSocket(), 4)
        pump.thread.start()
        pump.thread.join(2)
        self.assertEqual(pump.items.qsize(), 4)
        self.assertEqual(pump.drops, 96)
        self.assertEqual(pump.high_water, 4)
        self.assertIsNotNone(pump.error)
        pump.close()

    def test_slow_output_does_not_block_submit(self):
        entered = threading.Event()
        release = threading.Event()
        def stalled_print(*args, **kwargs):
            entered.set()
            release.wait(1)
        with tempfile.TemporaryDirectory() as folder, patch.object(r, 'print', stalled_print, create=True):
            path = pathlib.Path(folder) / 'test.jsonl'
            writer = r.EventWriter(path, capacity=1)
            try:
                writer.submit({'event': 'frame', 'frame': 1})
                self.assertTrue(entered.wait(1))
                writer.submit({'event': 'summary', 'passed': True})
                started = time.monotonic()
                with self.assertRaisesRegex(OSError, 'output queue full'):
                    writer.submit({'event': 'frame', 'frame': 2})
                self.assertLess(time.monotonic()-started, .1)
            finally:
                release.set()
                writer.close()
            events = [json.loads(line) for line in path.read_text().splitlines()]
            self.assertEqual([e['event'] for e in events], ['frame', 'summary'])
            self.assertIsNone(writer.error)

    def test_output_failure_is_reported_without_overwriting(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(r, 'print', lambda *a, **k: None, create=True):
            dest = pathlib.Path(folder)
            existing = dest / 'frame_00000001_00000001.txt'
            existing.write_bytes(b'keep')
            writer = r.EventWriter(output_dir=dest)
            writer.submit({'event': 'frame'}, ((1, 1), b'new!'))
            writer.close()
            self.assertIsNotNone(writer.error)
            self.assertEqual(existing.read_bytes(), b'keep')

    def test_invalid_queue_capacity(self):
        with self.assertRaises(ValueError): r.DatagramPump(None, 0)
        with self.assertRaises(ValueError): r.EventWriter(capacity=0)
        with self.assertRaises(ValueError): r.EventWriter(word_bytes=3)

    def test_little_endian_hex_words_exact_format(self):
        out = io.StringIO()
        r.write_hex_words(out, b'\x34\x12\x00\x00\xff\xff\x00\x80')
        self.assertEqual(out.getvalue(), '1234\n0000\nFFFF\n8000\n')
        for word_bytes, expected in ((1, '34\n12\n78\n56\n'), (4, '56781234\n')):
            out = io.StringIO()
            r.write_hex_words(out, b'\x34\x12\x78\x56', word_bytes)
            self.assertEqual(out.getvalue(), expected)

    def test_full_image_hex_roundtrip_and_metadata(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(r, 'print', lambda *a, **k: None, create=True):
            dest = pathlib.Path(folder)
            data = r.test_pattern(787456)
            writer = r.EventWriter(dest/'events.jsonl', dest)
            writer.submit({'event': 'frame'}, ((1, 2), data))
            writer.close()
            self.assertIsNone(writer.error)
            path = dest/'frame_00000001_00000002.txt'
            lines = path.read_text(encoding='ascii').splitlines()
            self.assertEqual(len(lines), 393728)
            self.assertTrue(all(len(x) == 4 for x in lines))
            self.assertEqual(b''.join(int(x, 16).to_bytes(2, 'little') for x in lines), data)
            self.assertFalse(list(dest.glob('*.bin')))
            event = json.loads((dest/'events.jsonl').read_text())
            self.assertEqual(event['output_format'], 'hex_txt')
            self.assertEqual(event['word_bytes'], 2)
            self.assertEqual(event['word_byteorder'], 'little')

    def test_odd_length_not_truncated_or_padded(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(r, 'print', lambda *a, **k: None, create=True):
            dest = pathlib.Path(folder)
            writer = r.EventWriter(output_dir=dest)
            writer.submit({'event': 'frame'}, ((1, 1), b'odd'))
            writer.close()
            self.assertIn('not divisible', writer.error)
            self.assertFalse(list(dest.glob('*.txt')))


if __name__ == '__main__': unittest.main()
