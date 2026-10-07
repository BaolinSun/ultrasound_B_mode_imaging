import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
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
from raw_udp_receiver import pattern_bytes
import soak_test


class SoakTests(unittest.TestCase):
    def simulate(self, corrupt=False, length=8, delay_response=False,
                 fail_trigger=False, fail_status=False, drop_packet=False,
                 device_failed=False, bad_device_counts=False,
                 fail_after_send=False, strict=True, frames=1,
                 only_first=False, reorder=False, no_data=False,
                 late_after_reject=False, busy_reads=0, late_extra=False):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            udp_port = probe.getsockname()[1]
        capture_done = threading.Event()
        events = {"posts": 0, "status_reads": 0, "rx_before_http_response": False,
                  "busy_remaining": 0}
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass
            def respond(self, data, code=200):
                body = json.dumps(data).encode()
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            def do_GET(self):
                if self.path == "/api/config":
                    self.respond({"target_ip":"127.0.0.1", "target_port":udp_port})
                else:
                    events["status_reads"] += 1
                    if fail_status and events["status_reads"] > 1:
                        self.respond({"error":"status unavailable"}, 503)
                        return
                    busy = events["busy_remaining"] > 0
                    if busy:
                        events["busy_remaining"] -= 1
                    self.respond({"selftest_bytes":length, "upload_ready":True, "busy":busy,
                        "result":"send_error" if device_failed else "sent_local",
                        "sent_bytes":length, "packets":0 if bad_device_counts else (length + 1439) // 1440,
                        "retries":3, "socket_errno":12,
                        "psram_free":1000000, "internal_free":100000,
                        "internal_min_free":90000})
            def do_POST(self):
                events["posts"] += 1
                events["busy_remaining"] = busy_reads
                faulty = not only_first or events["posts"] == 1
                if fail_trigger:
                    self.respond({"error":"upload unavailable"}, 503)
                    return
                if not delay_response:
                    self.respond({"message":"accepted"}, 202)
                raw = bytearray(pattern_bytes(length))
                if corrupt and faulty:
                    raw[0] ^= 1
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                    offsets = list(range(0, len(raw), 1440))
                    if reorder and faulty:
                        offsets[0], offsets[1] = offsets[1], offsets[0]
                    if late_after_reject and faulty:
                        sender.sendto(b"bad", ("127.0.0.1", udp_port))
                        time.sleep(0.08)
                    for i in offsets:
                        if (drop_packet and faulty and i == 1440) or (no_data and faulty):
                            continue
                        sender.sendto(raw[i:i + 1440], ("127.0.0.1", udp_port))
                    if late_extra and faulty:
                        time.sleep(0.05)
                        sender.sendto(b"late duplicate", ("127.0.0.1", udp_port))
                if delay_response:
                    # Old synchronous HTTP-then-RX code cannot satisfy this.
                    events["rx_before_http_response"] = capture_done.wait(2)
                    self.respond({"message":"accepted"}, 503 if fail_after_send else 202)
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        real_request = soak_test.request
        real_receive = soak_test.receive_upload
        def local_request(device, path, method="GET"):
            return real_request(f"127.0.0.1:{server.server_port}", path, method)
        def short_receive(*args, **kwargs):
            kwargs.update(wait_timeout=0.2, idle_timeout=0.1, transfer_timeout=1)
            try:
                return real_receive(*args, **kwargs)
            finally:
                capture_done.set()
        try:
            with tempfile.TemporaryDirectory() as directory:
                log_path = Path(directory) / "soak.jsonl"
                argv = ["soak_test", "--device-ip", "127.0.0.1", "--hours", "1",
                        "--frames", str(frames), "--interval", "0", "--log", str(log_path)]
                if strict:
                    argv.append("--stop-on-frame-error")
                with patch.object(soak_test, "request", local_request), \
                     patch.object(soak_test, "receive_upload", short_receive), \
                     patch.object(sys, "argv", argv):
                    code = soak_test.main()
                entries = [json.loads(line) for line in log_path.read_text().splitlines()]
                return code, entries, events
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_http_trigger_udp_capture_and_log(self):
        code, entries, events = self.simulate()
        self.assertEqual(code, 0)
        self.assertEqual(len(entries), 1)
        self.assertTrue(entries[0]["passed"])
        self.assertEqual(entries[0]["received_bytes"], 8)
        self.assertEqual(entries[0]["internal_min_free"], 90000)
        self.assertEqual(entries[0]["receive_mode"], "concurrent_http_udp")
        self.assertGreater(entries[0]["udp_rcvbuf_bytes"], 0)
        self.assertEqual(entries[0]["device_retries"], 3)
        self.assertEqual(entries[0]["device_socket_errno"], 12)
        self.assertEqual(entries[0]["device_packets"], 1)
        self.assertEqual(entries[0]["device_sent_bytes"], 8)
        self.assertFalse(entries[0]["device_busy"])
        self.assertEqual(entries[0]["trigger_error"], "")
        self.assertGreater(entries[0]["http_trigger_elapsed_s"], 0)
        self.assertEqual(entries[0]["frame_quiet_s"], 0.25)
        self.assertGreater(entries[0]["valid_fps"], 0)
        self.assertGreater(entries[0]["valid_payload_mbit_s"], 0)
        self.assertGreaterEqual(entries[0]["cycle_elapsed_s"], 0.25)
        self.assertEqual(events["posts"], 1)

    def test_corruption_stops_soak(self):
        code, entries, events = self.simulate(corrupt=True)
        self.assertEqual(code, 1)
        self.assertFalse(entries[0]["passed"])
        self.assertGreater(entries[0]["mismatch_bytes"], 0)
        self.assertEqual(events["posts"], 1)  # No hidden frame retry.

    def test_receives_before_delayed_http_response(self):
        code, entries, events = self.simulate(length=3000, delay_response=True)
        self.assertEqual(code, 0)
        self.assertTrue(events["rx_before_http_response"])
        self.assertTrue(entries[0]["passed"])
        self.assertEqual(entries[0]["packets"], 3)
        self.assertGreater(entries[0]["http_trigger_elapsed_s"], entries[0]["elapsed_s"])

    def test_trigger_failure_is_logged_without_replay(self):
        code, entries, events = self.simulate(fail_trigger=True)
        self.assertEqual(code, 1)
        self.assertFalse(entries[0]["passed"])
        self.assertIn("503", entries[0]["trigger_error"])
        self.assertIn("wait timeout", entries[0]["error"])
        self.assertEqual(events["posts"], 1)

    def test_packet_loss_keeps_original_udp_error_and_device_counts(self):
        code, entries, events = self.simulate(length=4324, drop_packet=True)
        self.assertEqual(code, 1)
        result = entries[0]
        self.assertIn("bad datagram length", result["error"])
        self.assertEqual(result["packets"], 2)
        self.assertEqual(result["device_packets"], 4)
        self.assertEqual(result["device_sent_bytes"], 4324)
        self.assertGreater(result["mismatch_bytes"], 0)
        self.assertEqual(events["posts"], 1)

    def test_status_failure_does_not_replace_udp_failure(self):
        code, entries, events = self.simulate(length=4324, drop_packet=True, fail_status=True)
        self.assertEqual(code, 1)
        self.assertIn("bad datagram length", entries[0]["error"])
        self.assertIn("503", entries[0]["status_error"])
        self.assertEqual(events["posts"], 1)

    def test_device_failure_cannot_pass_host_capture(self):
        code, entries, events = self.simulate(device_failed=True)
        self.assertEqual(code, 1)
        self.assertEqual(entries[0]["received_bytes"], 8)
        self.assertIn("completed local upload", entries[0]["error"])
        self.assertEqual(events["posts"], 1)

    def test_device_count_mismatch_cannot_pass_host_capture(self):
        code, entries, events = self.simulate(bad_device_counts=True)
        self.assertEqual(code, 1)
        self.assertEqual(entries[0]["received_bytes"], 8)
        self.assertEqual(entries[0]["device_packets"], 0)
        self.assertIn("completed local upload", entries[0]["error"])
        self.assertEqual(events["posts"], 1)

    def test_http_failure_after_valid_udp_cannot_pass(self):
        code, entries, events = self.simulate(length=3000, delay_response=True,
                                             fail_after_send=True)
        self.assertEqual(code, 1)
        self.assertTrue(events["rx_before_http_response"])
        self.assertEqual(entries[0]["received_bytes"], 3000)
        self.assertEqual(entries[0]["mismatch_bytes"], 0)
        self.assertIn("503", entries[0]["trigger_error"])
        self.assertEqual(events["posts"], 1)

    def test_receive_setup_failure_joins_worker_without_triggering(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock, \
             patch.object(soak_test, "request") as request_mock, \
             patch.object(soak_test, "receive_upload", side_effect=OSError("receive setup failed")):
            with self.assertRaisesRegex(OSError, "receive setup failed"):
                soak_test.trigger_and_receive("127.0.0.1", sock, 8)
            request_mock.assert_not_called()
        self.assertFalse(any(t.name == "soak-http-trigger" for t in threading.enumerate()))

    def test_reject_nan_duration(self):
        with patch.object(sys, "argv", ["soak_test", "--device-ip", "127.0.0.1", "--hours", "nan"]):
            with self.assertRaises(SystemExit) as exit_info:
                soak_test.main()
        self.assertEqual(exit_info.exception.code, 2)

    def test_default_discards_lost_frame_then_accepts_next(self):
        code, entries, events = self.simulate(length=4324, drop_packet=True,
                                             only_first=True, strict=False, frames=2)
        self.assertEqual(code, 0)
        self.assertEqual(events["posts"], 2)  # New upload, not a retry of a bad frame.
        self.assertEqual([e["frame_status"] for e in entries], ["discarded", "valid"])
        self.assertEqual(entries[-1]["valid_frames"], 1)
        self.assertEqual(entries[-1]["discarded_frames"], 1)
        self.assertEqual(entries[-1]["frame_drop_rate"], 0.5)
        self.assertIn("bad datagram length", entries[0]["discard_reason"])
        self.assertTrue(all("payload" not in e for e in entries))

    def test_corrupt_frame_discard_does_not_stop_default_run(self):
        code, entries, _ = self.simulate(corrupt=True, only_first=True, strict=False, frames=2)
        self.assertEqual(code, 0)
        self.assertFalse(entries[0]["passed"])
        self.assertEqual(entries[0]["discard_reason"], "self-test pattern mismatch")
        self.assertTrue(entries[1]["passed"])

    def test_reordered_frame_is_discarded_without_reassembly(self):
        code, entries, _ = self.simulate(length=3000, reorder=True,
                                        only_first=True, strict=False, frames=2)
        self.assertEqual(code, 0)
        self.assertEqual(entries[0]["received_bytes"], 3000)
        self.assertGreater(entries[0]["mismatch_bytes"], 0)
        self.assertEqual(entries[0]["frame_status"], "discarded")
        self.assertTrue(entries[1]["passed"])

    def test_completely_lost_frame_does_not_stop_default_run(self):
        code, entries, _ = self.simulate(no_data=True, only_first=True, strict=False, frames=2)
        self.assertEqual(code, 0)
        self.assertEqual(entries[0]["received_bytes"], 0)
        self.assertFalse(entries[0]["pattern_checked"])
        self.assertTrue(entries[1]["passed"])

    def test_busy_device_and_late_remainder_are_settled_before_next_upload(self):
        code, entries, events = self.simulate(length=3000, late_after_reject=True,
                                             busy_reads=2, only_first=True, strict=False, frames=2)
        self.assertEqual(code, 0)
        self.assertGreater(events["status_reads"], 3)
        self.assertEqual(entries[0]["drained_datagrams"], 3)
        self.assertFalse(entries[0]["device_busy"])
        self.assertTrue(entries[1]["passed"])

    def test_all_frames_discarded_is_not_reported_as_success(self):
        code, entries, events = self.simulate(corrupt=True, strict=False, frames=2)
        self.assertEqual(code, 1)
        self.assertEqual(events["posts"], 2)
        self.assertEqual(entries[-1]["valid_frames"], 0)
        self.assertEqual(entries[-1]["discarded_frames"], 2)

    def test_control_errors_still_stop_loss_tolerant_run(self):
        code, entries, events = self.simulate(fail_trigger=True, strict=False, frames=2)
        self.assertEqual(code, 1)
        self.assertEqual(events["posts"], 1)
        self.assertIn("503", entries[0]["control_error"])

    def test_wait_for_idle_has_a_deadline(self):
        with patch.object(soak_test, "request", return_value={"busy": True}), \
             patch.object(soak_test.time, "monotonic", side_effect=[0, 36]):
            with self.assertRaisesRegex(OSError, "remained busy"):
                soak_test.wait_for_idle("127.0.0.1")

    def test_drain_has_a_deadline_under_continuous_traffic(self):
        sock = unittest.mock.Mock()
        sock.recvfrom.return_value = (b"late", ("127.0.0.1", 1234))
        with patch.object(soak_test.time, "monotonic", side_effect=[0, 0, 0, 1.9, 2.01]):
            with self.assertRaisesRegex(OSError, "did not become quiet"):
                soak_test.drain_until_quiet(sock)

    def test_late_extra_packet_invalidates_complete_frame_in_merged_window(self):
        code, entries, events = self.simulate(length=3000, late_extra=True,
                                             only_first=True, strict=False, frames=2)
        self.assertEqual(code, 0)
        self.assertEqual(events["posts"], 2)
        self.assertEqual(entries[0]["received_bytes"], 3000)
        self.assertEqual(entries[0]["mismatch_bytes"], 0)
        self.assertEqual(entries[0]["post_receive_frame_datagrams"], 1)
        self.assertEqual(entries[0]["frame_status"], "discarded")
        self.assertIn("extra datagram", entries[0]["discard_reason"])
        self.assertTrue(entries[1]["passed"])

    def test_reject_invalid_quiet_window(self):
        for value in ("nan", "0", "-1", "1.1"):
            with self.subTest(value=value), patch.object(sys, "argv", [
                    "soak_test", "--device-ip", "127.0.0.1", "--frame-quiet", value]):
                with self.assertRaises(SystemExit) as error:
                    soak_test.main()
                self.assertEqual(error.exception.code, 2)
