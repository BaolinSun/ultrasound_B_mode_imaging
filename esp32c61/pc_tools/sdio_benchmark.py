#!/usr/bin/env python3
"""Bounded fixed-DDR benchmark: listen first, start SDIO, sample status, stop.
Never starts imaging or changes Wi-Fi/target configuration.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device-ip', required=True)
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--port', type=int, default=50001)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600 or not 1 <= args.port <= 65535:
        parser.error('invalid seconds or UDP port')
    base = 'http://' + args.device_ip
    def api(path, payload=None):
        body = json.dumps(payload).encode() if payload is not None else None
        request = urllib.request.Request(base+path, data=body,
                                         headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=5) as response:
            return json.load(response)
    before = api('/api/status')
    sdio = before.get('sdio', {})
    if before.get('busy') or sdio.get('running') or not sdio.get('host_sdio') or sdio.get('free_slots') != 2:
        raise SystemExit('Device must be idle, handshaken, with two free slots')
    # Unique names avoid mixing different tests in an append-mode receiver log.
    if args.log.exists():
        raise SystemExit('Choose a new log filename; existing benchmark log is preserved')
    args.log.parent.mkdir(parents=True, exist_ok=True)
    stdout_path = args.log.with_suffix('.stdout.log')
    samples = [{'t': 0, 'status': before}]
    unexpected_stop = False
    process = None
    armed = False
    control_error = None
    started = time.monotonic()
    try:
        with stdout_path.open('w', encoding='utf-8') as stdout, args.log.with_suffix('.stderr.log').open('w') as stderr:
            command = [sys.executable, str(Path(__file__).with_name('sdio_udp_receiver.py')),
                       '--source-ip', args.device_ip, '--frames', '0', '--seconds', str(args.seconds+2),
                       '--port', str(args.port),
                       '--test-pattern', '--log', str(args.log)]
            process = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            deadline = time.monotonic()+5
            while 'Listening UBSD' not in stdout_path.read_text(encoding='utf-8'):
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('Receiver did not become ready')
                time.sleep(.05)
            armed = True
            api('/api/sdio/start', {'frames': 0})
            print('Fixed DDR benchmark running; do not press device start/stop buttons.', flush=True)
            while process.poll() is None:
                state = api('/api/status')
                samples.append({'t': time.monotonic()-started, 'status': state})
                if not state.get('sdio', {}).get('running'):
                    unexpected_stop = True
                time.sleep(.5)
    finally:
        try:
            if armed:
                api('/api/sdio/stop', {})
        except Exception as exc:
            control_error = str(exc)
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        result = {'before': before, 'samples': samples, 'unexpected_stop': unexpected_stop,
                  'stop_error': control_error,
                  'receiver_exit': process.returncode if process else None}
        args.log.with_suffix('.device.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    summaries = [json.loads(line) for line in args.log.read_text(encoding='utf-8').splitlines()
                 if json.loads(line).get('event') == 'summary']
    print(json.dumps(summaries[-1] if summaries else {'error': 'no receiver summary'}, indent=2))
    if unexpected_stop:
        print('Device stopped before benchmark finished; not a clean continuous-throughput result.', file=sys.stderr)
    if control_error:
        print('Device stop failed: '+control_error, file=sys.stderr)
    return 0 if process.returncode == 0 and not unexpected_stop and not control_error else 1


if __name__ == '__main__':
    raise SystemExit(main())
