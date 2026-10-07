#!/usr/bin/env python3
"""Receive ONE raw upload, then validate its pattern outside receive timing."""
from __future__ import annotations

import argparse
import json
import math
import socket
import time
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_BYTES = 787456
DATAGRAM_BYTES = 1440


def pattern_bytes(length: int, offset: int = 0) -> bytes:
    return bytes((((i // 4) ^ 0xA5C30000) >> ((i % 4) * 8)) & 255
                 for i in range(offset, offset + length))


@dataclass
class Capture:
    expected_bytes: int
    check_pattern: bool = True
    payload: bytearray = field(default_factory=bytearray)
    packets: int = 0
    mismatch_bytes: int = 0
    first_mismatch: tuple[int, int, int] | None = None
    error: str = ""
    _pattern_verified: bool = field(default=False, init=False, repr=False)

    def add(self, data: bytes) -> bool:
        if self.error:
            return False
        remaining = self.expected_bytes - len(self.payload)
        expected_datagram = min(DATAGRAM_BYTES, remaining)
        if not data or len(data) != expected_datagram:
            self.error = f"bad datagram length: got={len(data)} expected={expected_datagram}"
            return False
        # Keep Python byte-by-byte pattern work out of the recvfrom hot path.
        self.payload.extend(data)
        self.packets += 1
        self._pattern_verified = False
        return True

    def verify_pattern(self) -> None:
        """Check retained bytes, including partial captures, after receiving stops."""
        if not self.check_pattern or self._pattern_verified:
            return
        self.mismatch_bytes = 0
        self.first_mismatch = None
        # Bound temporary expected-pattern memory even for large captures.
        for offset in range(0, len(self.payload), 65536):
            actual = self.payload[offset:offset + 65536]
            expected = pattern_bytes(len(actual), offset)
            if actual == expected:
                continue
            for i, (value, wanted) in enumerate(zip(actual, expected)):
                if value != wanted:
                    self.mismatch_bytes += 1
                    if self.first_mismatch is None:
                        self.first_mismatch = (offset + i, wanted, value)
        self._pattern_verified = True

    @property
    def complete(self) -> bool:
        return len(self.payload) == self.expected_bytes

    @property
    def passed(self) -> bool:
        return (self.complete and not self.error and self.mismatch_bytes == 0
                and (not self.check_pattern or self._pattern_verified))


def receive_upload(sock: socket.socket, expected_bytes: int = DEFAULT_BYTES,
                   check_pattern: bool = True, wait_timeout: float = 60,
                   idle_timeout: float = 5, transfer_timeout: float = 30,
                   source_ip: str | None = None, grace_period: float = 0.25) -> dict:
    capture = Capture(expected_bytes, check_pattern)
    wait_started = time.monotonic()
    started = last = finished = None
    source = None
    ignored = 0
    while True:
        now = time.monotonic()
        if capture.complete:
            if finished is None:
                finished = now
            if now - finished >= grace_period:
                break
        elif started is None and now - wait_started >= wait_timeout:
            capture.error = "no upload before wait timeout"
            break
        elif started is not None:
            if now - started >= transfer_timeout:
                capture.error = "transfer deadline exceeded"
                break
            if now - last >= idle_timeout:
                capture.error = "incomplete upload: idle timeout"
                break
        sock.settimeout(0.05)
        try:
            data, address = sock.recvfrom(65535)
        except socket.timeout:
            continue
        if (source_ip and address[0] != source_ip) or (source and address != source):
            ignored += 1
            continue
        if capture.complete:
            capture.error = "extra datagram after expected length (duplicate or overlapping upload)"
            break
        now = time.monotonic()
        if started is None:
            started = now
            source = address
        last = now
        if not capture.add(data):
            break
    elapsed = last - started if last is not None and started is not None else 0.0
    # These are userspace dequeue timestamps, not NIC/kernel arrival timestamps.
    # Final grace checking, pattern validation and file writes are not included.
    validation_elapsed = 0.0
    if check_pattern and capture.payload:
        validation_started = time.perf_counter()
        capture.verify_pattern()
        validation_elapsed = time.perf_counter() - validation_started
    return {
        "passed": capture.passed, "expected_bytes": expected_bytes,
        "received_bytes": len(capture.payload), "packets": capture.packets,
        "pattern_checked": check_pattern and bool(capture.payload),
        "mismatch_bytes": capture.mismatch_bytes,
        "first_mismatch": capture.first_mismatch, "error": capture.error,
        "source": source, "ignored_datagrams": ignored,
        "elapsed_s": elapsed,
        "validation_elapsed_s": validation_elapsed,
        "timing_basis": "first_to_last_datagram_dequeue",
        "mbit_s": len(capture.payload) * 8 / elapsed / 1_000_000 if elapsed > 0 and capture.packets > 1 else None,
        "payload": capture.payload,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=50001)
    parser.add_argument("--bytes", type=int, default=DEFAULT_BYTES)
    parser.add_argument("--pattern", choices=("selftest", "none"), default="selftest")
    parser.add_argument("--source-ip", help="Only accept uploads from this ESP32 IPv4")
    parser.add_argument("--output", type=Path, default=Path("psram_capture.bin"))
    parser.add_argument("--wait-timeout", type=float, default=60)
    parser.add_argument("--idle-timeout", type=float, default=5)
    parser.add_argument("--transfer-timeout", type=float, default=30)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not 1 <= args.bytes <= 16 * 1024 * 1024:
        parser.error("port must be 1..65535 and bytes must be 1..16777216")
    timeouts = (args.wait_timeout, args.idle_timeout, args.transfer_timeout)
    if any(not math.isfinite(t) or t <= 0 for t in timeouts):
        parser.error("timeouts must be positive")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        sock.bind((args.bind, args.port))
        print(f"Listening on {args.bind}:{args.port}; expect {args.bytes} raw bytes. "
              "Press Upload on the ESP32 page now.", flush=True)
        result = receive_upload(sock, args.bytes, args.pattern == "selftest", args.wait_timeout,
                                args.idle_timeout, args.transfer_timeout, args.source_ip)
    payload = result.pop("payload")
    # Never overwrite an earlier capture accidentally. Failed captures remain inspectable.
    output = args.output if result["passed"] else args.output.with_suffix(args.output.suffix + ".partial")
    if payload:
        with output.open("xb") as stream:
            stream.write(payload)
        result["output"] = str(output.resolve())
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
