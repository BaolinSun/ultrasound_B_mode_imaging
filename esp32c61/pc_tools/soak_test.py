#!/usr/bin/env python3
"""Verify sequential self-test frames, discard bad frames and continue."""
import argparse
from collections import deque
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


def trigger_and_receive(device, sock, length, excluded_sources=()):
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
                                on_ready=ready.set, excluded_sources=excluded_sources)
    finally:
        # Release/join even if receive setup or Ctrl+C interrupts this cycle.
        # No next cycle or socket close may race an outstanding HTTP trigger.
        cancelled.set()
        ready.set()
        worker.join()
    result.update(trigger, udp_rcvbuf_bytes=rcvbuf,
                  receive_mode="concurrent_http_udp")
    if trigger["trigger_error"]:
        result["control_error"] = trigger["trigger_error"]
        mark_failed(result, trigger["trigger_error"])
    return result


def mark_failed(result, error):
    result["passed"] = False
    # Keep the original UDP failure, while logging trigger/status errors apart.
    if not result.get("error"):
        result["error"] = error


def drain_until_quiet(sock, quiet=0.25, timeout=2):
    """Discard queued/late bytes BEFORE the next HTTP trigger, with a deadline."""
    deadline = time.monotonic() + timeout
    quiet_deadline = time.monotonic() + quiet
    packets = 0
    sources = set()
    while True:
        now = time.monotonic()
        if now >= quiet_deadline:
            return packets, sources
        if now >= deadline:
            raise OSError("UDP did not become quiet between uploads")
        sock.settimeout(min(0.05, quiet_deadline - now, deadline - now))
        try:
            _, address = sock.recvfrom(65535)
        except socket.timeout:
            continue
        packets += 1
        sources.add(address)
        quiet_deadline = time.monotonic() + quiet


def wait_for_idle(device, timeout=35):
    """An early RX rejection must not trigger a new frame while TX is busy."""
    deadline = time.monotonic() + timeout
    while True:
        status = request(device, "/api/status")
        if not status["busy"]:
            return status
        if time.monotonic() >= deadline:
            raise OSError("Device remained busy after upload deadline")
        time.sleep(0.05)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device-ip", required=True, type=ipaddress.IPv4Address)
    parser.add_argument("--hours", type=float, default=48)
    parser.add_argument("--interval", type=float, default=1)
    parser.add_argument("--frames", type=int, help="Optional maximum number of frames")
    parser.add_argument("--stop-on-frame-error", action="store_true",
                        help="Strict mode: stop at the first discarded frame")
    parser.add_argument("--log", type=Path,
                        default=Path(datetime.now().strftime("soak-%Y%m%d-%H%M%S.jsonl")))
    args = parser.parse_args()
    if not math.isfinite(args.hours) or not math.isfinite(args.interval) or args.hours <= 0 or args.interval < 0:
        parser.error("hours must be positive and interval must be nonnegative")
    if args.frames is not None and args.frames <= 0:
        parser.error("frames must be positive")
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
    valid_frames = discarded_frames = 0
    # Each firmware job opens a new UDP socket. A bounded history rejects late
    # datagrams from recent jobs, without blacklisting eventual port reuse forever.
    recent_sources = deque(maxlen=16)
    deadline = time.monotonic() + args.hours * 3600
    # Exclusive creation protects earlier soak results. No binary capture files accumulate.
    with args.log.open("x", encoding="utf-8") as log, \
         socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        sock.bind(("0.0.0.0", config["target_port"]))
        while time.monotonic() < deadline and (args.frames is None or cycles < args.frames):
            cycles += 1
            result = {"passed": False}
            try:
                result = trigger_and_receive(device, sock, length, tuple(recent_sources))
                result.pop("payload", None)
                if result.get("source"):
                    recent_sources.append(tuple(result["source"]))
                try:
                    status = wait_for_idle(device)
                    result.update(psram_free=status["psram_free"],
                                  internal_free=status["internal_free"],
                                  internal_min_free=status["internal_min_free"],
                                  device_result=status["result"],
                                  device_busy=status["busy"],
                                  device_sent_bytes=status["sent_bytes"],
                                  device_packets=status["packets"],
                                  device_retries=status["retries"],
                                  device_socket_errno=status["socket_errno"])
                    if (status["result"] != "sent_local"
                            or status["sent_bytes"] != length
                            or status["packets"] != (length + DATAGRAM_BYTES - 1) // DATAGRAM_BYTES):
                        result["control_error"] = "Device did not report a completed local upload"
                        mark_failed(result, result["control_error"])
                except (OSError, ValueError, KeyError) as error:
                    result["status_error"] = str(error)
                    result["control_error"] = str(error)
                    mark_failed(result, str(error))
                if not result.get("control_error"):
                    drained, sources = drain_until_quiet(sock)
                    result["drained_datagrams"] = drained
                    # Never let unrelated traffic evict device ports from history.
                    recent_sources.extend(sorted(s for s in sources if s[0] == device))
            except (OSError, ValueError, KeyError) as error:
                result.pop("payload", None)
                result["control_error"] = str(error)
                mark_failed(result, str(error))
            if result["passed"]:
                valid_frames += 1
            else:
                discarded_frames += 1
            result.update(cycle=cycles, utc=datetime.now(timezone.utc).isoformat(),
                          frame_status="valid" if result["passed"] else "discarded",
                          discard_reason="" if result["passed"] else (
                              result.get("error") or "self-test pattern mismatch"),
                          valid_frames=valid_frames, discarded_frames=discarded_frames,
                          frame_drop_rate=discarded_frames / cycles)
            line = json.dumps(result, ensure_ascii=False)
            log.write(line + "\n")
            log.flush()
            print(line, flush=True)
            if result.get("control_error") or (args.stop_on_frame_error and not result["passed"]):
                print("Soak FAILED; control/device error or strict frame check failed.")
                return 1
            time.sleep(args.interval)
    print(f"Soak COMPLETED: frames={cycles}, valid={valid_frames}, discarded={discarded_frames}, "
          f"frame_drop_rate={discarded_frames / cycles if cycles else 0:.2%}")
    # Permitting occasional loss does not make a zero-valid-frame run useful.
    if not valid_frames:
        print("Soak FAILED: no valid frame received.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
