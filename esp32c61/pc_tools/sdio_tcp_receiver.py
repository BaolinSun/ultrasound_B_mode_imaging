#!/usr/bin/env python3
"""UBST v1 TCP server. Validate complete frames, then send UBSA CRC ACKs."""
import argparse
import json
from pathlib import Path
import socket
import struct
import sys
import threading
import time
import uuid
import zlib
from sdio_udp_receiver import EventWriter, test_pattern

HEADER = struct.Struct('!IBBHIIII')
ACK = struct.Struct('!IBBHIII')
MAGIC, ACK_MAGIC, MAX_BYTES = 0x55425354, 0x55425341, 1048576

def parse_header(data, expected_bytes=787456):
    if len(data) != HEADER.size:
        raise ValueError('short TCP header')
    magic, version, size, flags, session, frame, length, crc = HEADER.unpack(data)
    if (magic, version, size, flags) != (MAGIC, 1, HEADER.size, 0):
        raise ValueError('invalid TCP magic/version/header/flags')
    if not session or not frame or not 0 < length <= MAX_BYTES or length != expected_bytes:
        raise ValueError('invalid TCP identity/frame length')
    return session, frame, length, crc

def encode_ack(session, frame, crc, status=0):
    return ACK.pack(ACK_MAGIC, 1, ACK.size, status, session, frame, crc)

class ReadFailure(Exception):
    def __init__(self, reason, received):
        self.reason, self.received = reason, received

def read_exact(sock, length, deadline, stop):
    data = bytearray(length)
    offset = 0
    while offset < length:
        if stop.is_set(): raise ReadFailure('receiver stopped', offset)
        if time.monotonic() >= deadline: raise ReadFailure('frame timeout', offset)
        try:
            n = sock.recv_into(memoryview(data)[offset:offset+min(65536, length-offset)])
        except socket.timeout:
            continue
        except OSError as exc:
            raise ReadFailure('peer disconnected: '+str(exc), offset) from exc
        if not n: raise ReadFailure('peer disconnected', offset)
        offset += n
    return data

class TCPReceiver:
    def __init__(self, args, writer):
        self.args, self.writer = args, writer
        self.stop = threading.Event()
        self.quiescing = threading.Event()  # Expect device cancellation; keep socket open until STOP.
        self.expected = test_pattern(args.length) if args.test_pattern else None
        self.valid_frames = self.valid_bytes = self.dropped_frames = self.received_bytes = 0
        self.connections = self.ignored_connections = self.invalid_headers = self.ack_errors = 0
        self.integrity_errors = 0
        self.incomplete_errors = 0
        self.first_data = self.last_data = None
        self.reason = 'completed'
        self.error = None
        self.run_id = uuid.uuid4().hex
        self.started = time.monotonic()
        self.until = self.started+args.seconds if args.seconds else float('inf')

    def emit(self, event, complete=None):
        event['run_id'] = self.run_id
        self.writer.submit(event, complete)

    def serve(self, listener):
        last_activity = time.monotonic()
        try:
            while not self.stop.is_set():
                if time.monotonic() >= self.until:
                    self.reason = 'duration reached'; break
                if time.monotonic()-last_activity >= self.args.idle_timeout:
                    self.reason = 'idle timeout'; break
                try: conn, source = listener.accept()
                except socket.timeout: continue
                if self.args.source_ip and source[0] != self.args.source_ip:
                    self.ignored_connections += 1; conn.close(); continue
                self.connections += 1
                with conn:
                    conn.settimeout(.1)
                    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    while not self.stop.is_set():
                        meta = None
                        frame_start = time.monotonic()
                        try:
                            header = read_exact(conn, HEADER.size,
                                                min(self.until, frame_start+self.args.idle_timeout), self.stop)
                            meta = parse_header(header, self.args.length)
                            session, frame, length, expected_crc = meta
                            frame_start = time.monotonic()
                            data = read_exact(conn, length,
                                              min(self.until, frame_start+self.args.frame_timeout), self.stop)
                        except ValueError as exc:
                            self.invalid_headers += 1
                            self.emit({'event': 'protocol_error', 'error': str(exc)})
                            break  # No scanning payload bytes for another magic.
                        except ReadFailure as exc:
                            if meta or exc.received:
                                self.dropped_frames += 1
                                if not self.stop.is_set() and not self.quiescing.is_set(): self.incomplete_errors += 1
                                if meta: self.received_bytes += exc.received
                                self.emit({'event': 'discard', 'session': meta[0] if meta else None,
                                           'frame': meta[1] if meta else None,
                                           'reason': 'planned device stop: '+exc.reason if self.quiescing.is_set() else exc.reason,
                                           'received_bytes': exc.received, 'expected_bytes': meta[2] if meta else HEADER.size})
                            if self.stop.is_set(): self.reason = 'receiver stopped'
                            break
                        last_activity = time.monotonic()
                        self.received_bytes += length
                        self.first_data = frame_start if self.first_data is None else self.first_data
                        self.last_data = last_activity
                        validation_start=time.monotonic()
                        crc = zlib.crc32(data)
                        status = 1 if crc != expected_crc else 2 if self.expected is not None and data != self.expected else 0
                        validation_elapsed=time.monotonic()-validation_start
                        if status:
                            self.integrity_errors += 1; self.dropped_frames += 1
                            self.emit({'event': 'discard', 'session': session, 'frame': frame,
                                       'reason': 'CRC32 mismatch' if status == 1 else 'test pattern mismatch'})
                        else:
                            self.valid_frames += 1; self.valid_bytes += length
                            elapsed = last_activity-frame_start
                            self.emit({'event': 'frame', 'session': session, 'frame': frame, 'bytes': length,
                                       'crc32': f'{crc:08x}', 'passed': True, 'elapsed_s': elapsed,
                                       'validation_elapsed_s': validation_elapsed,
                                       'mbit_s': length*8/elapsed/1e6 if elapsed else None},
                                      ((session, frame), data) if self.args.output_dir else None)
                        # Validation ACK, not a filesystem durability ACK.
                        try: conn.sendall(encode_ack(session, frame, crc, status))
                        except OSError as exc:
                            if not self.quiescing.is_set(): self.ack_errors += 1
                            self.emit({'event': 'ack_error', 'session': session, 'frame': frame, 'error': str(exc)})
                            break
                        if status: break
                        if self.args.frames and self.valid_frames >= self.args.frames:
                            return
                if self.stop.is_set(): break
            if self.stop.is_set(): self.reason = 'receiver stopped'
        except (OSError, ValueError) as exc:
            self.error = str(exc); self.reason = 'receiver I/O error'

    def summary(self):
        elapsed = time.monotonic()-self.started
        data_elapsed = self.last_data-self.first_data if self.first_data is not None else 0
        passed = not self.error and not self.integrity_errors and not self.invalid_headers and not self.ack_errors and not self.incomplete_errors and \
                 self.valid_frames > 0 and (not self.args.frames or self.valid_frames >= self.args.frames)
        return {'event': 'summary', 'transport': 'tcp', 'passed': passed, 'reason': self.reason,
                'valid_frames': self.valid_frames, 'discarded_frames': self.dropped_frames,
                'valid_bytes': self.valid_bytes, 'received_bytes': self.received_bytes,
                'connections': self.connections, 'ignored_connections': self.ignored_connections,
                'invalid_headers': self.invalid_headers, 'ack_errors': self.ack_errors,
                'integrity_errors': self.integrity_errors, 'elapsed_s': elapsed,
                'incomplete_errors': self.incomplete_errors,
                'valid_fps': self.valid_frames/elapsed, 'payload_mbit_s': self.valid_bytes*8/elapsed/1e6,
                'data_elapsed_s': data_elapsed,
                'data_payload_mbit_s': self.valid_bytes*8/data_elapsed/1e6 if data_elapsed else None,
                'pattern_checked': self.args.test_pattern, 'error': self.error}

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=50001)
    parser.add_argument('--source-ip')
    parser.add_argument('--length', type=int, default=787456)
    parser.add_argument('--frames', type=int, default=1)
    parser.add_argument('--seconds', type=float, default=0)
    parser.add_argument('--frame-timeout', type=float, default=30)
    parser.add_argument('--idle-timeout', type=float, default=30)
    parser.add_argument('--test-pattern', action='store_true')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--log', type=Path)
    parser.add_argument('--managed', action='store_true', help='benchmark stdin STOP command')
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535 or not 0 < args.length <= MAX_BYTES or args.frames < 0 or \
       args.seconds < 0 or args.frame_timeout <= 0 or args.idle_timeout <= 0:
        parser.error('invalid receiver limits')
    if args.output_dir: args.output_dir.mkdir(parents=True, exist_ok=True)
    writer = EventWriter(args.log, args.output_dir)
    rx = TCPReceiver(args, writer)
    def control():
        for line in sys.stdin:
            if line.strip() == 'QUIESCE':
                rx.quiescing.set();continue
            if line.strip() == 'STOP': break
        rx.stop.set()
    if args.managed: threading.Thread(target=control, daemon=True).start()
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind((args.bind, args.port));listener.listen(1);listener.settimeout(.1)
            print(f'Listening UBST TCP on {args.bind}:{args.port}; start SDIO on ESP32 now.', flush=True)
            rx.emit({'event': 'run_start', 'transport': 'tcp', 'frames_target': args.frames,
                     'seconds_limit': args.seconds, 'pattern_checked': args.test_pattern})
            thread = threading.Thread(target=rx.serve, args=(listener,), daemon=True)
            thread.start()
            while thread.is_alive():
                thread.join(.1)
                if writer.error: rx.error = writer.error;rx.stop.set()
    except KeyboardInterrupt:
        rx.reason = 'interrupted';rx.stop.set()
        if 'thread' in locals(): thread.join(1)
    except OSError as exc:
        rx.error = str(exc);rx.reason = 'receiver I/O error'
    summary = rx.summary()
    try: rx.emit(summary)
    except OSError as exc: rx.error = str(exc)
    writer.close()
    if writer.error or rx.error:
        print('Receiver/output failed: '+str(writer.error or rx.error), file=sys.stderr)
        return 1
    return 0 if summary['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
