import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "pc_tools"))
from raw_udp_receiver import pattern_bytes
import soak_test


class SoakTests(unittest.TestCase):
    def simulate(self, corrupt):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            udp_port = probe.getsockname()[1]
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass
            def respond(self, data):
                body = json.dumps(data).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            def do_GET(self):
                if self.path == "/api/config":
                    self.respond({"target_ip":"127.0.0.1", "target_port":udp_port})
                else:
                    self.respond({"selftest_bytes":8, "upload_ready":True, "busy":False,
                        "result":"sent_local", "psram_free":1000000, "internal_free":100000,
                        "internal_min_free":90000})
            def do_POST(self):
                self.respond({"message":"accepted"})
                raw = bytearray(pattern_bytes(8))
                if corrupt:
                    raw[0] ^= 1
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                    sender.sendto(raw, ("127.0.0.1", udp_port))
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        real_request = soak_test.request
        def local_request(device, path, method="GET"):
            return real_request(f"127.0.0.1:{server.server_port}", path, method)
        try:
            with tempfile.TemporaryDirectory() as directory:
                log_path = Path(directory) / "soak.jsonl"
                with patch.object(soak_test, "request", local_request), patch.object(sys, "argv", [
                    "soak_test", "--device-ip", "127.0.0.1", "--hours", "0.00002",
                    "--interval", "0", "--log", str(log_path)]):
                    code = soak_test.main()
                entries = [json.loads(line) for line in log_path.read_text().splitlines()]
                return code, entries
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_http_trigger_udp_capture_and_log(self):
        code, entries = self.simulate(False)
        self.assertEqual(code, 0)
        self.assertEqual(len(entries), 1)
        self.assertTrue(entries[0]["passed"])
        self.assertEqual(entries[0]["received_bytes"], 8)
        self.assertEqual(entries[0]["internal_min_free"], 90000)

    def test_corruption_stops_soak(self):
        code, entries = self.simulate(True)
        self.assertEqual(code, 1)
        self.assertFalse(entries[0]["passed"])
        self.assertGreater(entries[0]["mismatch_bytes"], 0)

    def test_reject_nan_duration(self):
        with patch.object(sys, "argv", ["soak_test", "--device-ip", "127.0.0.1", "--hours", "nan"]):
            with self.assertRaises(SystemExit) as exit_info:
                soak_test.main()
        self.assertEqual(exit_info.exception.code, 2)
