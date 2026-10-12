#!/usr/bin/env python3
"""UBSD v1 frame receiver. No ACK/retransmission; incomplete frames are discarded."""
from __future__ import annotations
import argparse
import collections
import dataclasses
import json
import socket
import queue
import threading
import struct
import time
import zlib
import uuid
import sys
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
        expected = (p.total + PAYLOAD - 1) // PAYLOAD
        missing = sorted(set(range(expected)) - p.indices)
        return {"event": "discard", "session": key[0], "frame": key[1], "reason": reason,
                "packets": len(p.indices), "expected_packets": (p.total + PAYLOAD - 1) // PAYLOAD,
                "missing_packets": len(missing), "missing_indices": missing,
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

class DatagramPump:
    """One bounded receive queue; socket draining never waits for print/disk/CRC."""
    def __init__(self, sock, capacity=4096):
        if capacity < 1:
            raise ValueError("receive queue capacity must be positive")
        self.sock = sock
        self.items = queue.Queue(capacity)
        self.stop = threading.Event()
        self.drops = self.high_water = 0
        self.error = None
        self.thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        try:
            while not self.stop.is_set():
                try:
                    packet, source = self.sock.recvfrom(65535)
                except socket.timeout:
                    continue
                item = (packet, source, time.monotonic())
                try:
                    self.items.put_nowait(item)
                    self.high_water = max(self.high_water, self.items.qsize())
                except queue.Full:
                    self.drops += 1  # Local loss is explicit, never hidden.
        except OSError as exc:
            if not self.stop.is_set():
                self.error = str(exc)

    def close(self):
        self.stop.set()
        self.thread.join(timeout=1)


class EventWriter:
    """Bounded asynchronous output; only validated complete frames reach disk."""
    def __init__(self, log_path=None, output_dir=None, capacity=64):
        if capacity < 1:
            raise ValueError("output queue capacity must be positive")
        self.items = queue.Queue(capacity)
        self.log_path, self.output_dir = log_path, output_dir
        self.error = None
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def submit(self, event, complete=None):
        if self.error:
            raise OSError(self.error)
        try:
            self.items.put_nowait((event, complete))
        except queue.Full:
            raise OSError("output queue full: terminal/disk stalled; reception stopped explicitly") from None

    def _run(self):
        log = None
        try:
            log = self.log_path.open("a", encoding="utf-8") if self.log_path else None
            flushed = time.monotonic()
            while True:
                item = self.items.get()
                if item is None:
                    break
                event, complete = item
                if complete and self.output_dir:
                    (session, frame), data = complete
                    path = self.output_dir / f"frame_{session:08x}_{frame:08x}.bin"
                    with path.open("xb") as out:
                        out.write(data)
                    event["output"] = str(path.resolve())
                line = json.dumps(event, ensure_ascii=False)
                if log:
                    log.write(line + "\n")
                    if event["event"] == "summary" or time.monotonic() - flushed >= 0.5:
                        log.flush()
                        flushed = time.monotonic()
                print(line, flush=True)
        except (OSError, ValueError) as exc:
            self.error = str(exc)
        finally:
            if log:
                log.close()

    def close(self):
        # A permanently paused console must not block reception/exit forever.
        try:
            self.items.put(None, timeout=1)
        except queue.Full:
            self.error = self.error or "output queue remained full at shutdown"
        self.thread.join(timeout=3)
        if self.thread.is_alive():
            self.error = self.error or "terminal/disk output stalled at shutdown"


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
    parser.add_argument("--queue-packets", type=int, default=4096, help="bounded PC receive queue")
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535 or args.frames < 0 or args.seconds < 0 or args.idle_timeout <= 0 or not 1 <= args.queue_packets <= 65536:
        parser.error("invalid port/frame count/time limit")
    try:
        receiver = FrameReceiver(args.length, args.frame_timeout, args.test_pattern)
    except ValueError as exc:
        parser.error(str(exc))
    receiver.source = args.source_ip
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
    run_id = uuid.uuid4().hex
    writer = EventWriter(args.log, args.output_dir)
    def emit(item):
        item["run_id"] = run_id
        writer.submit(item)
    started = last_valid_packet = time.monotonic()
    reason = "completed"
    failed = False
    pump = None
    first_packet = last_packet = None
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
            sock.bind((args.bind, args.port))
            sock.settimeout(min(0.1, args.frame_timeout))
            print(f"Listening UBSD on {args.bind}:{args.port}; boot Zynq compiled for SDIO, then start on ESP32 page.", flush=True)
            emit({"event": "run_start", "frames_target": args.frames, "seconds_limit": args.seconds,
                  "pattern_checked": args.test_pattern, "queue_packets": args.queue_packets,
                  "socket_rcvbuf": sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)})
            pump = DatagramPump(sock, args.queue_packets)
            pump.thread.start()
            while not args.frames or receiver.valid_frames < args.frames:
                now = time.monotonic()
                if args.seconds and now - started >= args.seconds:
                    reason = "duration reached"
                    break
                if now - last_valid_packet >= args.idle_timeout:
                    reason = "no valid UBSD datagrams before idle timeout"
                    break
                if pump.error or writer.error:
                    raise OSError(pump.error or writer.error)
                try:
                    packet, source, arrived = pump.items.get(timeout=0.05)
                except queue.Empty:
                    for item in receiver.expire():
                        emit(item)
                    continue
                previous = receiver.packets
                events, complete = receiver.feed(packet, source, arrived)
                if receiver.packets != previous:
                    last_valid_packet = arrived
                    first_packet = arrived if first_packet is None else first_packet
                    last_packet = arrived
                for index, item in enumerate(events):
                    item["run_id"] = run_id
                    writer.submit(item, complete if args.output_dir and index == len(events)-1 else None)
            pump.close()
    except KeyboardInterrupt:
        reason = "interrupted"
    except OSError as exc:
        reason = f"receiver I/O error: {exc}"
        failed = True
    finally:
        if pump:
            pump.close()
        elapsed = time.monotonic() - started
        data_elapsed = max(0.0, last_packet-first_packet) if first_packet is not None else 0.0
        passed = not failed and receiver.valid_frames > 0 and (args.frames == 0 or receiver.valid_frames >= args.frames)
        try:
            for item in receiver.flush():
                emit(item)
            emit({"event": "summary", "passed": passed, "reason": reason,
              "valid_frames": receiver.valid_frames, "discarded_frames": receiver.dropped_frames,
              "observed_drop_rate": receiver.dropped_frames / max(1, receiver.valid_frames + receiver.dropped_frames),
              "valid_bytes": receiver.valid_bytes, "received_bytes": receiver.received_bytes,
              "packets": receiver.packets, "duplicates": receiver.duplicates,
              "invalid_datagrams": receiver.invalid_datagrams, "ignored_datagrams": receiver.ignored_datagrams,
              "elapsed_s": elapsed, "valid_fps": receiver.valid_frames / elapsed,
              "payload_mbit_s": receiver.valid_bytes * 8 / elapsed / 1e6,
              "data_elapsed_s": data_elapsed,
              "data_valid_fps": receiver.valid_frames / data_elapsed if data_elapsed else None,
              "data_payload_mbit_s": receiver.valid_bytes * 8 / data_elapsed / 1e6 if data_elapsed else None,
              "local_queue_drops": pump.drops if pump else 0,
              "queue_high_water": pump.high_water if pump else 0,
              "queued_packets_at_stop": pump.items.qsize() if pump else 0,
              "note": "drop rate counts observed partial frames, not frames with zero packets received"})
        except OSError as exc:
            writer.error = writer.error or str(exc)
        writer.close()
        if writer.error:
            print(f"Output failed: {writer.error}", file=sys.stderr)
            passed = False
    return 0 if passed else 1

if __name__ == "__main__":
    raise SystemExit(main())
