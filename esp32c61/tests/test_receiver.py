import json
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "pc_tools"))
from raw_udp_receiver import Capture, pattern_bytes, receive_upload


class ReceiverTests(unittest.TestCase):
    def test_pattern_and_boundaries(self):
        self.assertEqual(pattern_bytes(8), b"\x00\x00\xc3\xa5\x01\x00\xc3\xa5")
        for size in (1, 4, 1439, 1440, 1441, 2880, 787456):
            with self.subTest(size=size):
                c = Capture(size)
                original = pattern_bytes(size)
                for i in range(0, size, 1440):
                    self.assertTrue(c.add(original[i:i+1440]))
                self.assertFalse(c.passed)  # Complete does not imply validated.
                c.verify_pattern()
                self.assertTrue(c.passed)
                self.assertEqual(c.payload, original)

    def test_lost_reordered_duplicate_and_corrupt(self):
        blocks = [pattern_bytes(1440, i * 1440) for i in range(3)]
        for indices in ((1, 0, 2), (0, 0, 1), (0, 2)):
            c = Capture(4320)
            for i in indices:
                c.add(blocks[i])
            c.verify_pattern()
            self.assertFalse(c.passed)
            self.assertGreater(c.mismatch_bytes, 0)
        c = Capture(4)
        c.add(b"\x00\x00\xc3\xa4")
        c.verify_pattern()
        self.assertEqual(c.first_mismatch, (3, 0xa5, 0xa4))

    def test_deferred_check_chunk_boundary_and_idempotence(self):
        raw = bytearray(pattern_bytes(65540))
        raw[65535] ^= 1
        raw[65536] ^= 1
        c = Capture(len(raw))
        for offset in range(0, len(raw), 1440):
            c.add(raw[offset:offset + 1440])
        self.assertEqual(c.mismatch_bytes, 0)  # No pattern work during add.
        c.verify_pattern()
        self.assertEqual(c.mismatch_bytes, 2)
        self.assertEqual(c.first_mismatch, (65535, pattern_bytes(1, 65535)[0], raw[65535]))
        c.verify_pattern()
        self.assertEqual(c.mismatch_bytes, 2)
        self.assertFalse(c.passed)

    def test_adding_after_partial_validation_requires_recheck(self):
        c = Capture(1444)
        c.add(pattern_bytes(1440))
        c.verify_pattern()
        self.assertFalse(c.passed)
        c.add(pattern_bytes(4, 1440))
        self.assertFalse(c.passed)
        c.verify_pattern()
        self.assertTrue(c.passed)

    def test_validation_is_outside_receive_timing(self):
        raw = pattern_bytes(1444)
        class FakeSocket:
            now = 0.0
            calls = 0
            def settimeout(self, timeout):
                pass
            def recvfrom(self, length):
                block = raw[self.calls * 1440:(self.calls + 1) * 1440]
                self.calls += 1
                self.now += 0.01
                return block, ("127.0.0.1", 50000)
        sock = FakeSocket()
        def slow_pattern(length, offset=0):
            self.assertEqual(sock.calls, 2)  # All recvfrom calls happened first.
            sock.now += 10
            return pattern_bytes(length, offset)
        with patch("raw_udp_receiver.time.monotonic", side_effect=lambda: sock.now), \
             patch("raw_udp_receiver.time.perf_counter", side_effect=lambda: sock.now), \
             patch("raw_udp_receiver.pattern_bytes", side_effect=slow_pattern):
            r = receive_upload(sock, len(raw), grace_period=0)
        self.assertTrue(r["passed"])
        self.assertAlmostEqual(r["elapsed_s"], 0.01)
        self.assertAlmostEqual(r["validation_elapsed_s"], 10)
        self.assertAlmostEqual(r["mbit_s"], len(raw) * 8 / 0.01 / 1_000_000)
        self.assertEqual(r["timing_basis"], "first_to_last_datagram_dequeue")

    def test_datagram_lengths_and_no_pattern(self):
        for data in (b"", bytes(1441), bytes(1439)):
            c = Capture(3000)
            self.assertFalse(c.add(data))
            self.assertTrue(c.error)
        c = Capture(4, False)
        self.assertTrue(c.add(b"abcd"))
        self.assertTrue(c.passed)

    def capture_loopback(self, blocks, length, **kwargs):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
            receiver.bind(("127.0.0.1", 0))
            address = receiver.getsockname()
            def sender():
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                    time.sleep(0.02)
                    for block in blocks:
                        s.sendto(block, address)
                        time.sleep(0.002)
            thread = threading.Thread(target=sender)
            thread.start()
            result = receive_upload(receiver, length, wait_timeout=0.2, idle_timeout=0.1,
                                    transfer_timeout=0.4, grace_period=0.03, **kwargs)
            thread.join()
            return result

    def test_real_udp_success(self):
        raw = pattern_bytes(3000)
        r = self.capture_loopback([raw[i:i+1440] for i in range(0, 3000, 1440)], 3000)
        self.assertTrue(r["passed"])
        self.assertEqual(r["payload"], raw)
        self.assertEqual(r["packets"], 3)

    def test_real_udp_missing_timeout(self):
        r = self.capture_loopback([pattern_bytes(1440)], 3000)
        self.assertFalse(r["passed"])
        self.assertIn("idle timeout", r["error"])
        self.assertTrue(r["pattern_checked"])

    def test_real_udp_corruption_after_receive(self):
        raw = bytearray(pattern_bytes(3000))
        raw[1450] ^= 1
        r = self.capture_loopback([raw[i:i+1440] for i in range(0, len(raw), 1440)], len(raw))
        self.assertFalse(r["passed"])
        self.assertEqual(r["received_bytes"], len(raw))
        self.assertEqual(r["mismatch_bytes"], 1)
        self.assertEqual(r["first_mismatch"][0], 1450)

    def test_pattern_none_skips_validation(self):
        with patch("raw_udp_receiver.pattern_bytes", side_effect=AssertionError("pattern called")):
            r = self.capture_loopback([b"abcd"], 4, check_pattern=False)
        self.assertTrue(r["passed"])
        self.assertFalse(r["pattern_checked"])
        self.assertEqual(r["validation_elapsed_s"], 0)
        self.assertIsNone(r["mbit_s"])  # One packet has no receive interval.

    def test_wait_timeout_and_source_filter(self):
        r = self.capture_loopback([pattern_bytes(4)], 4, source_ip="192.168.1.100")
        self.assertFalse(r["passed"])
        self.assertEqual(r["ignored_datagrams"], 1)
        self.assertIn("wait timeout", r["error"])
        self.assertFalse(r["pattern_checked"])
        self.assertEqual(r["validation_elapsed_s"], 0)

    def test_extra_packet(self):
        r = self.capture_loopback([pattern_bytes(4), pattern_bytes(4)], 4)
        self.assertFalse(r["passed"])
        self.assertIn("extra datagram", r["error"])

    def run_cli(self, blocks, length):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "capture.bin"
            tool = Path(__file__).resolve().parents[1] / "pc_tools" / "raw_udp_receiver.py"
            process = subprocess.Popen([sys.executable, str(tool), "--bind", "127.0.0.1",
                "--port", str(port), "--bytes", str(length), "--output", str(output),
                "--wait-timeout", "2", "--idle-timeout", "0.1", "--transfer-timeout", "2"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                self.assertIn("Listening", process.stdout.readline())
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                    for block in blocks:
                        sender.sendto(block, ("127.0.0.1", port))
                        time.sleep(0.002)
                stdout, stderr = process.communicate(timeout=5)
                saved = output if output.exists() else output.with_suffix(".bin.partial")
                payload = saved.read_bytes() if saved.exists() else b""
                return process.returncode, stdout, stderr, payload, saved.suffix
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()

    def test_cli_save_and_exit_status(self):
        raw = pattern_bytes(3000)
        code, stdout, stderr, saved, suffix = self.run_cli(
            [raw[i:i+1440] for i in range(0, len(raw), 1440)], len(raw))
        self.assertEqual(code, 0, stderr)
        self.assertEqual(saved, raw)
        self.assertEqual(suffix, ".bin")
        self.assertIn('"passed": true', stdout)
        result = json.loads(stdout)
        self.assertTrue(result["pattern_checked"])
        self.assertGreaterEqual(result["validation_elapsed_s"], 0)
        self.assertEqual(result["timing_basis"], "first_to_last_datagram_dequeue")

    def test_cli_incomplete_partial_file(self):
        code, stdout, stderr, saved, suffix = self.run_cli([pattern_bytes(1440)], 3000)
        self.assertEqual(code, 1, stderr)
        self.assertEqual(saved, pattern_bytes(1440))
        self.assertEqual(suffix, ".partial")
        self.assertIn('"passed": false', stdout)

    def test_cli_corrupt_capture_is_partial(self):
        raw = bytearray(pattern_bytes(3000))
        raw[-1] ^= 1
        code, stdout, stderr, saved, suffix = self.run_cli(
            [raw[i:i+1440] for i in range(0, len(raw), 1440)], len(raw))
        self.assertEqual(code, 1, stderr)
        self.assertEqual(saved, raw)
        self.assertEqual(suffix, ".partial")
        result = json.loads(stdout)
        self.assertTrue(result["pattern_checked"])
        self.assertEqual(result["mismatch_bytes"], 1)
        self.assertEqual(result["first_mismatch"][0], 2999)

    def test_partial_corruption_is_validated_after_timeout(self):
        raw = bytearray(pattern_bytes(1440))
        raw[12] ^= 1
        r = self.capture_loopback([raw], 3000)
        self.assertFalse(r["passed"])
        self.assertIn("idle timeout", r["error"])
        self.assertTrue(r["pattern_checked"])
        self.assertEqual(r["mismatch_bytes"], 1)
        self.assertEqual(r["first_mismatch"][0], 12)


if __name__ == "__main__":
    unittest.main()
