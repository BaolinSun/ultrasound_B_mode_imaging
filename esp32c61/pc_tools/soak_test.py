#!/usr/bin/env python3
"""Trigger and verify sequential self-test uploads for a board soak test."""
import argparse
from datetime import datetime, timezone
import ipaddress
import json
import math
from pathlib import Path
import socket
import time
from urllib.request import Request, urlopen

from raw_udp_receiver import receive_upload


def request(device, path, method="GET"):
    with urlopen(Request(f"http://{device}{path}", method=method), timeout=5) as response:
        return json.load(response)


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
                    # Socket is listening before the upload is triggered.
                    request(device, "/api/upload", "POST")
                    result = receive_upload(sock, length, source_ip=device,
                                            wait_timeout=5, idle_timeout=5, transfer_timeout=30)
                result.pop("payload", None)
                status = request(device, "/api/status")
                result.update(psram_free=status["psram_free"],
                              internal_free=status["internal_free"],
                              internal_min_free=status["internal_min_free"],
                              device_result=status["result"])
                if status["busy"] or status["result"] != "sent_local":
                    result.update(passed=False, error="Device did not report a completed local upload")
            except (OSError, ValueError, KeyError) as error:
                result.pop("payload", None)
                result.update(passed=False, error=str(error))
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
