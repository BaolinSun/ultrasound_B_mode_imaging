#!/usr/bin/env python3
"""UBSD v1 frame receiver. No ACK/retransmission; incomplete frames are discarded."""
from __future__ import annotations
import argparse
import collections
import dataclasses
import json
import socket
import struct
import time
import zlib
from pathlib import Path

HEADER = struct.Struct("!IBBHIIIIHHI")
MAGIC, VERSION, PAYLOAD, MAX_BYTES = 0x55425344, 1, 1440, 1048576

def encode_packet(data: bytes, session: int, frame: int, offset: int) -> bytes:
    """Test/interop fixture encoder; data is the complete frame."""
    chunk = data[offset:offset + PAYLOAD]
    return HEADER.pack(MAGIC, VERSION, HEADER.size, 0, session, frame,
                       len(data), offset, len(chunk), offset // PAYLOAD,
                       zlib.crc32(data)) + chunk

def parse_packet(packet: bytes, expected_bytes: int | None = None):
    if len(packet) < HEADER.size:
        raise ValueError("short header")
    magic, version, hlen, flags, session, frame, total, offset, size, index, crc = HEADER.unpack_from(packet)
    if (magic, version, hlen, flags) != (MAGIC, VERSION, HEADER.size, 0):
        raise ValueError("unsupported header")
    if not session or not frame or not 0 < total <= MAX_BYTES:
        raise ValueError("invalid frame identity/length")
    if expected_bytes is not None and total != expected_bytes:
        raise ValueError("unexpected frame length")
    if offset >= total or offset % PAYLOAD or index != offset // PAYLOAD:
        raise ValueError("invalid packet offset/index")
    if size != min(PAYLOAD, total - offset) or len(packet) != hlen + size:
        raise ValueError("invalid datagram length")
    return session, frame, total, offset, index, crc, packet[hlen:]

def test_pattern(length: int) -> bytes:
    words = (length + 3) // 4
    return b"".join(struct.pack("<I", i ^ 0xA5C30000) for i in range(words))[:length]

@dataclasses.dataclass
class Pending:
    total: int
    crc: int
    started: float
    data: bytearray
    indices: set[int] = dataclasses.field(default_factory=set)

class FrameReceiver:
    def __init__(self, length=787456, timeout=2.0, check_pattern=False):
        if not 0 < length <= MAX_BYTES or timeout <= 0:
            raise ValueError("invalid receiver limits")
        self.length, self.timeout = length, timeout
        self.expected = test_pattern(length) if check_pattern else None
        self.pending = collections.OrderedDict()
        self.retired = collections.deque(maxlen=256)
        self.old_sessions = collections.deque(maxlen=64)
        self.session = None
        self.latest_frame = None
        self.source = None
        self.valid_frames = self.dropped_frames = self.duplicates = 0
        self.invalid_datagrams = self.ignored_datagrams = self.received_bytes = self.valid_bytes = 0
        self.packets = 0

    def _drop(self, key, reason, now):
        p = self.pending.pop(key)
        self.retired.append(key)
        self.dropped_frames += 1
        return {"event": "discard", "session": key[0], "frame": key[1], "reason": reason,
                "packets": len(p.indices), "expected_packets": (p.total + PAYLOAD - 1) // PAYLOAD,
                "elapsed_s": now - p.started}

    def expire(self, now=None):
        now = time.monotonic() if now is None else now
        return [self._drop(k, "frame timeout", now) for k, p in list(self.pending.items())
                if now - p.started >= self.timeout]

    def flush(self, reason="receiver stopped", now=None):
        now = time.monotonic() if now is None else now
        return [self._drop(k, reason, now) for k in list(self.pending)]

    def feed(self, packet, source, now=None):
        now = time.monotonic() if now is None else now
        events = self.expire(now)
        try:
            session, frame, total, offset, index, crc, chunk = parse_packet(packet, self.length)
        except ValueError:
            self.invalid_datagrams += 1
            return events, None
        ip = source[0]
        if self.source is not None and self.source != ip:
            self.ignored_datagrams += 1
            return events, None
        self.source = ip
        if session in self.old_sessions:
            self.ignored_datagrams += 1
            return events, None
        if self.session != session:
            events += self.flush("session changed", now)
            if self.session is not None:
                self.old_sessions.append(self.session)
            self.session = session
            self.latest_frame = None
        key = (session, frame)
        if self.latest_frame is not None:
            delta = ((frame - self.latest_frame + 0x80000000) & 0xffffffff) - 0x80000000
            if delta < -1 and key not in self.pending:
                self.ignored_datagrams += 1
                return events, None
            if delta > 0:
                self.latest_frame = frame
        else:
            self.latest_frame = frame
        if key in self.retired:
            self.ignored_datagrams += 1
            return events, None
        self.packets += 1
        self.received_bytes += len(chunk)
        if key not in self.pending:
            if len(self.pending) >= 2:
                events.append(self._drop(next(iter(self.pending)), "assembly capacity", now))
            self.pending[key] = Pending(total, crc, now, bytearray(total))
        p = self.pending[key]
        if p.total != total or p.crc != crc:
            events.append(self._drop(key, "conflicting metadata", now))
            return events, None
        if index in p.indices:
            if p.data[offset:offset + len(chunk)] != chunk:
                events.append(self._drop(key, "conflicting duplicate", now))
            else:
                self.duplicates += 1
            return events, None
        p.data[offset:offset + len(chunk)] = chunk
        p.indices.add(index)
        if len(p.indices) != (total + PAYLOAD - 1) // PAYLOAD:
            return events, None
        if zlib.crc32(p.data) != crc:
            events.append(self._drop(key, "CRC32 mismatch", now))
            return events, None
        if self.expected is not None and p.data != self.expected:
            events.append(self._drop(key, "test pattern mismatch", now))
            return events, None
        del self.pending[key]
        self.retired.append(key)
        self.valid_frames += 1
        self.valid_bytes += total
        elapsed = now - p.started
        events.append({"event": "frame", "session": session, "frame": frame, "bytes": total,
                       "packets": len(p.indices), "crc32": f"{crc:08x}", "passed": True,
                       "elapsed_s": elapsed, "mbit_s": total * 8 / elapsed / 1e6 if elapsed else None})
        return events, (key, bytes(p.data))

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=50001)
    parser.add_argument("--length", type=int, default=787456)
    parser.add_argument("--frames", type=int, default=1, help="valid frames to receive; 0 forever")
    parser.add_argument("--seconds", type=float, default=0, help="optional total run duration")
    parser.add_argument("--frame-timeout", type=float, default=2)
    parser.add_argument("--idle-timeout", type=float, default=30)
    parser.add_argument("--source-ip")
    parser.add_argument("--test-pattern", action="store_true")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535 or args.frames < 0 or args.seconds < 0 or args.idle_timeout <= 0:
        parser.error("invalid port/frame count/time limit")
    try:
        receiver = FrameReceiver(args.length, args.frame_timeout, args.test_pattern)
    except ValueError as exc:
        parser.error(str(exc))
    receiver.source = args.source_ip
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
    log = args.log.open("a", encoding="utf-8") if args.log else None
    def emit(item):
        line = json.dumps(item, ensure_ascii=False)
        print(line, flush=True)
        if log:
            log.write(line + "\n")
            log.flush()
    started = last_valid_packet = time.monotonic()
    reason = "completed"
    failed = False
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
            sock.bind((args.bind, args.port))
            sock.settimeout(min(0.1, args.frame_timeout))
            print(f"Listening UBSD on {args.bind}:{args.port}; boot Zynq compiled for SDIO, then start on ESP32 page.", flush=True)
            while not args.frames or receiver.valid_frames < args.frames:
                now = time.monotonic()
                if args.seconds and now - started >= args.seconds:
                    reason = "duration reached"
                    break
                if now - last_valid_packet >= args.idle_timeout:
                    reason = "no valid UBSD datagrams before idle timeout"
                    break
                try:
                    packet, source = sock.recvfrom(65535)
                except socket.timeout:
                    for item in receiver.expire():
                        emit(item)
                    continue
                previous = receiver.packets
                events, complete = receiver.feed(packet, source)
                if receiver.packets != previous:
                    last_valid_packet = time.monotonic()
                if complete and args.output_dir:
                    (session, frame), data = complete
                    path = args.output_dir / f"frame_{session:08x}_{frame:08x}.bin"
                    # Never silently overwrite an earlier capture from another run.
                    with path.open("xb") as out:
                        out.write(data)
                    events[-1]["output"] = str(path.resolve())
                for item in events:
                    emit(item)
    except KeyboardInterrupt:
        reason = "interrupted"
    except OSError as exc:
        reason = f"receiver I/O error: {exc}"
        failed = True
    finally:
        for item in receiver.flush():
            emit(item)
        elapsed = time.monotonic() - started
        passed = not failed and receiver.valid_frames > 0 and (args.frames == 0 or receiver.valid_frames >= args.frames)
        emit({"event": "summary", "passed": passed, "reason": reason,
              "valid_frames": receiver.valid_frames, "discarded_frames": receiver.dropped_frames,
              "observed_drop_rate": receiver.dropped_frames / max(1, receiver.valid_frames + receiver.dropped_frames),
              "valid_bytes": receiver.valid_bytes, "received_bytes": receiver.received_bytes,
              "packets": receiver.packets, "duplicates": receiver.duplicates,
              "invalid_datagrams": receiver.invalid_datagrams, "ignored_datagrams": receiver.ignored_datagrams,
              "elapsed_s": elapsed, "valid_fps": receiver.valid_frames / elapsed,
              "payload_mbit_s": receiver.valid_bytes * 8 / elapsed / 1e6,
              "note": "drop rate counts observed partial frames, not frames with zero packets received"})
        if log:
            log.close()
    return 0 if passed else 1

if __name__ == "__main__":
    raise SystemExit(main())
