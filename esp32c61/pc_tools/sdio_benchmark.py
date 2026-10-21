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
from log_paths import LOG_HELP, resolve_log_path

def upstream_idle(status):
    s=status.get('sdio',{})
    return bool(s.get('running') and s.get('ready',True) and s.get('host_sdio') and
                s.get('free_slots')==2 and not status.get('busy'))

def stalled_since(status, now, since, timeout):
    """No frame in either slot despite an armed/handshaken stream. Not TCP loss."""
    if status.get('sdio',{}).get('source_flags',0)&2:return None,False
    if not upstream_idle(status):return None,False
    since=now if since is None else since
    return since,now-since>=timeout

def tcp_phase_summary(samples):
    """Deduplicate successful last-frame metrics; polling does not see all frames."""
    frames={}
    for sample in samples[1:]:  # Exclude pre-test history.
        s=sample['status'];session=s.get('tcp_metric_session');frame=s.get('tcp_metric_frame')
        if not session or not frame or session!=s.get('sdio',{}).get('session'):
            continue
        if s.get('tcp_metric_result') not in ('received_verified','sent_local'):
            continue
        frames[(session,frame)]=s
    fields=('tcp_header_us','tcp_payload_us','tcp_ack_us','tcp_write_wait_us',
            'tcp_read_wait_us','tcp_yield_us','tcp_copy_us','tcp_write_calls',
            'tcp_partial_writes','tcp_send_waits','tcp_ack_waits','tcp_resource_waits','tcp_heap_guard_waits',
            'tcp_adaptive_writes','tcp_cooperative_waits','tcp_queue_us','tcp_idle_us')
    return {'sampled_complete_frames':len(frames),
            'mean':{f:sum(s.get(f,0) for s in frames.values())/len(frames) for f in fields} if frames else {},
            'note':'Unique completed frames observed by status polling, not every frame. Wait/yield/copy are overlapping subsets of stage durations.'}


def sdio_phase_summary(samples):
    """New copy+CRC timing only; do not reinterpret old pure-CRC logs."""
    frames={}
    initial=samples[0]['status'].get('sdio',{}).get('received_frames') if samples else None
    for sample in samples[1:]:
        s=sample['status'].get('sdio',{})
        # A new START changes session before the first new frame completes.
        # Do not relabel the previous run's retained last-frame metrics.
        if initial is not None:
            count=s.get('received_frames')
            if count is None or not 0<((count-initial)&0xffffffff)<0x80000000:
                continue
        if s.get('session') and s.get('last_received_frame') and 'last_copy_crc_us' in s:
            frames[(s['session'],s['last_received_frame'])]=s
    return {'sampled_complete_frames':len(frames),
            'mean':{key:sum(s.get(key,0) for s in frames.values())/len(frames)
                    for key in ('last_receive_us','last_copy_crc_us')} if frames else {},
            'note':'Copy+CRC wall time overlaps receive and may include preemption. Not comparable to legacy pure last_crc_us; polling samples only.'}


def pipeline_rates(samples):
    active=samples[1:]
    result={}
    for label,key in (('pl_published_fps','source_published'),('sdio_received_fps','received_frames'),
                      ('wifi_completed_fps','sent_frames')):
        values=[(s['t'],s['status'].get('sdio',{})) for s in active
                if key in s['status'].get('sdio',{})]
        result[label]=None
        if len(values)>1:
            dt=values[-1][0]-values[0][0]
            delta=(values[-1][1][key]-values[0][1][key])&0xffffffff
            if dt>0 and delta<0x80000000:
                result[label]=delta/dt
    result['note']='Sampled counter deltas. PL publication excludes invalid/unbuffered frames. Wi-Fi completion means PC ACK for TCP, local send for UDP; receiver summary is valid-PC-throughput authority.'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device-ip', required=True)
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--log', type=Path, required=True, help=LOG_HELP)
    parser.add_argument('--port', type=int, default=50001)
    parser.add_argument('--test-pattern', action='store_true', help='only for explicit synthetic source, not imaging DDR')
    parser.add_argument('--stall-timeout',type=float,default=5,
                        help='Fail if stream stays armed with two free slots and idle sender for N seconds')
    args = parser.parse_args()
    args.log = resolve_log_path(args.log)
    if not 1 <= args.seconds <= 3600 or not 1 <= args.port <= 65535 or not 1<=args.stall_timeout<=120:
        parser.error('invalid seconds or TCP/UDP port')
    base = 'http://' + args.device_ip
    def api(path, payload=None):
        body = json.dumps(payload).encode() if payload is not None else None
        request = urllib.request.Request(base+path, data=body,
                                         headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=5) as response:
            return json.load(response)
    before = api('/api/status')
    transport=before.get('sdio_transport','udp')
    if transport not in ('tcp','udp'): raise SystemExit('Unknown compiled transport')
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
    upstream_stall=False;idle_since=None
    process = None
    armed = False
    control_error = None
    started = time.monotonic()
    try:
        with stdout_path.open('w', encoding='utf-8') as stdout, args.log.with_suffix('.stderr.log').open('w') as stderr:
            command = [sys.executable, str(Path(__file__).with_name('sdio_'+transport+'_receiver.py')),
                       '--source-ip', args.device_ip, '--frames', '0', '--seconds', str(args.seconds+10),
                       '--port', str(args.port),
                       '--log', str(args.log),'--managed']
            if args.test_pattern: command.append('--test-pattern')
            process = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                       stdin=subprocess.PIPE,
                                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            deadline = time.monotonic()+5
            while ('Listening UBST TCP' if transport=='tcp' else 'Listening UBSD') not in stdout_path.read_text(encoding='utf-8'):
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('Receiver did not become ready')
                time.sleep(.05)
            armed = True
            api('/api/sdio/start', {'frames': 0})
            active_started=time.monotonic()
            print('Fixed DDR benchmark running; do not press device start/stop buttons.', flush=True)
            while process.poll() is None:
                if time.monotonic()-active_started>=args.seconds: break
                state = api('/api/status')
                samples.append({'t': time.monotonic()-started, 'status': state})
                idle_since,upstream_stall=stalled_since(state,time.monotonic(),idle_since,args.stall_timeout)
                if upstream_stall:
                    unexpected_stop=True
                    print('Upstream stalled: stream armed, two free slots, no sender activity.',file=sys.stderr)
                    break
                if not state.get('sdio', {}).get('running'):
                    unexpected_stop = True
                    break
                time.sleep(.5)
            if process.poll() is not None and time.monotonic()-active_started < args.seconds:
                unexpected_stop = True  # e.g. receiver idle timeout while device claims running
    finally:
        try:
            if armed:
                # TCP may cancel a partially sent frame. Announce the planned
                # cancellation, but keep the receiver/socket alive until the
                # device has stopped. Unexpected disconnects during the run
                # remain errors. UDP control ignores this non-STOP line.
                if process and process.poll() is None:
                    process.stdin.write(b'QUIESCE\n');process.stdin.flush()
                api('/api/sdio/stop', {})
                deadline=time.monotonic()+2
                while time.monotonic()<deadline:
                    if not api('/api/status').get('busy'): break
                    time.sleep(.05)
                else:
                    control_error='device sender did not stop within 2 seconds'
        except Exception as exc:
            control_error = str(exc)
        if process and process.poll() is None:
            try:
                process.stdin.write(b'STOP\n');process.stdin.flush()
                process.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                process.terminate();process.wait(timeout=5)
                control_error=control_error or 'receiver did not stop cleanly'
        result = {'before': before, 'samples': samples, 'unexpected_stop': unexpected_stop,
                  'upstream_stall':upstream_stall,'stall_timeout_s':args.stall_timeout,
                  'transport':transport,
                  'stop_error': control_error,
                  'receiver_exit': process.returncode if process else None}
        if armed and not control_error:
            try: result['after']=api('/api/status')
            except Exception as exc:
                control_error=str(exc);result['stop_error']=control_error
        if transport=='tcp':result['tcp_phase_summary']=tcp_phase_summary(samples)
        result['sdio_phase_summary']=sdio_phase_summary(samples)
        result['pipeline_rates']=pipeline_rates(samples)
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
