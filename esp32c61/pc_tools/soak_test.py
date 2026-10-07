#!/usr/bin/env python3
"""Trigger and verify sequential self-test uploads for a board soak test."""
import argparse
from datetime import datetime, timezone
import ipaddress
import json
import math
from pathlib import Path
import socket
import threading
import time
from urllib.request import Request, urlopen

from raw_udp_receiver import DATAGRAM_BYTES, receive_upload


def request(device, path, method="GET"):
    with urlopen(Request(f"http://{device}{path}", method=method), timeout=5) as response:
        return json.load(response)


def trigger_and_receive(device, sock, length):
    """Drain UDP while HTTP is in flight; never replay a failed upload."""
    ready = threading.Event()
    cancelled = threading.Event()
    trigger = {"trigger_error": "", "http_trigger_elapsed_s": None}

    def send_request():
        ready.wait()
        if cancelled.is_set():
            return
        started = time.perf_counter()
        try:
            request(device, "/api/upload", "POST")
        except Exception as error:
            # Marshal HTTP/JSON errors back to the owning thread and JSON log.
            trigger["trigger_error"] = str(error) or type(error).__name__
        finally:
            trigger["http_trigger_elapsed_s"] = time.perf_counter() - started

    rcvbuf = sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
    worker = threading.Thread(target=send_request, name="soak-http-trigger")
    worker.start()
    try:
        result = receive_upload(sock, length, source_ip=device,
                                wait_timeout=5, idle_timeout=5, transfer_timeout=30,
                                on_ready=ready.set)
    finally:
        # Release/join even if receive setup or Ctrl+C interrupts this cycle.
        # No next cycle or socket close may race an outstanding HTTP trigger.
        cancelled.set()
        ready.set()
        worker.join()
    result.update(trigger, udp_rcvbuf_bytes=rcvbuf,
                  receive_mode="concurrent_http_udp")
    if trigger["trigger_error"]:
        mark_failed(result, trigger["trigger_error"])
    return result


def mark_failed(result, error):
    result["passed"] = False
    # Keep the original UDP failure, while logging trigger/status errors apart.
    if not result.get("error"):
        result["error"] = error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device-ip", required=True, type=ipaddress.IPv4Address)
    parser.add_argument("--hours", type=float, default=48)
    parser.add_argument("--interval", type=float, default=1)
    parser.add_argument("--log", type=Path,
                        default=Path(datetime.now().strftime("soak-%Y%m%d-%H%M%S.jsonl")))
    args = parser.parse_args()
    if not math.isfinite(args.hours) or not math.isfinite(args.interval) or args.hours <= 0 or args.interval < 0:
        parser.error("hours must be positive and interval must be nonnegative")
    device = str(args.device_ip)
    config = request(device, "/api/config")
    status = request(device, "/api/status")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((device, 80))
        local_ip = route.getsockname()[0]
    if config["target_ip"] != local_ip:
        parser.error(f"Set the device target IP to this PC's route address {local_ip} first")
    length = status["selftest_bytes"]
    if not status["upload_ready"] or status["busy"] or not 1 <= length <= 16 * 1024 * 1024:
        parser.error("Device must be connected, idle, and have a PSRAM self-test buffer")
    cycles = 0
    deadline = time.monotonic() + args.hours * 3600
    # Exclusive creation protects earlier soak results. No binary capture files accumulate.
    with args.log.open("x", encoding="utf-8") as log:
        while time.monotonic() < deadline:
            cycles += 1
            result = {"passed": False}
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
                    sock.bind(("0.0.0.0", config["target_port"]))
                    result = trigger_and_receive(device, sock, length)
                result.pop("payload", None)
                try:
                    status = request(device, "/api/status")
                    result.update(psram_free=status["psram_free"],
                                  internal_free=status["internal_free"],
                                  internal_min_free=status["internal_min_free"],
                                  device_result=status["result"],
                                  device_busy=status["busy"],
                                  device_sent_bytes=status["sent_bytes"],
                                  device_packets=status["packets"],
                                  device_retries=status["retries"],
                                  device_socket_errno=status["socket_errno"])
                    if (status["busy"] or status["result"] != "sent_local"
                            or status["sent_bytes"] != length
                            or status["packets"] != (length + DATAGRAM_BYTES - 1) // DATAGRAM_BYTES):
                        mark_failed(result, "Device did not report a completed local upload")
                except (OSError, ValueError, KeyError) as error:
                    result["status_error"] = str(error)
                    mark_failed(result, str(error))
            except (OSError, ValueError, KeyError) as error:
                result.pop("payload", None)
                mark_failed(result, str(error))
            result.update(cycle=cycles, utc=datetime.now(timezone.utc).isoformat())
            line = json.dumps(result, ensure_ascii=False)
            log.write(line + "\n")
            log.flush()
            print(line, flush=True)
            if not result["passed"]:
                print("Soak FAILED; inspect the log and device console.")
                return 1
            time.sleep(args.interval)
    print(f"Soak PASSED: {cycles} sequential verified uploads, duration >= {args.hours} hours")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
