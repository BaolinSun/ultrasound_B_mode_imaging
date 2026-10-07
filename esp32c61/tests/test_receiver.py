import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

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
                self.assertTrue(c.passed)
                self.assertEqual(c.payload, original)

    def test_lost_reordered_duplicate_and_corrupt(self):
        blocks = [pattern_bytes(1440, i * 1440) for i in range(3)]
        for indices in ((1, 0, 2), (0, 0, 1), (0, 2)):
            c = Capture(4320)
            for i in indices:
                c.add(blocks[i])
            self.assertFalse(c.passed)
            self.assertGreater(c.mismatch_bytes, 0)
        c = Capture(4)
        c.add(b"\x00\x00\xc3\xa4")
        self.assertEqual(c.first_mismatch, (3, 0xa5, 0xa4))

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

    def test_wait_timeout_and_source_filter(self):
        r = self.capture_loopback([pattern_bytes(4)], 4, source_ip="192.168.1.100")
        self.assertFalse(r["passed"])
        self.assertEqual(r["ignored_datagrams"], 1)
        self.assertIn("wait timeout", r["error"])

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

    def test_cli_incomplete_partial_file(self):
        code, stdout, stderr, saved, suffix = self.run_cli([pattern_bytes(1440)], 3000)
        self.assertEqual(code, 1, stderr)
        self.assertEqual(saved, pattern_bytes(1440))
        self.assertEqual(suffix, ".partial")
        self.assertIn('"passed": false', stdout)


if __name__ == "__main__":
    unittest.main()
