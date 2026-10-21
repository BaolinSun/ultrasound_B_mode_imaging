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
import urllib.error
import urllib.request
from log_paths import LOG_HELP, resolve_log_path


def sample_status(api, errors, elapsed):
    """One read-only attempt. Never retry a mutating control request here."""
    try:
        return api('/api/status')
    except urllib.error.HTTPError as exc:
        if not 500 <= exc.code <= 599:
            raise
        detail = f'HTTP {exc.code}'
        # Keep the diagnostic response, but never record request headers/tokens.
        body = exc.read(256).decode('utf-8', errors='replace')
        if body:
            detail += ': ' + body
    except (urllib.error.URLError, TimeoutError) as exc:
        detail = str(exc)
    errors.append({'t': elapsed, 'error': detail})
    print('Status sample unavailable: '+detail, file=sys.stderr, flush=True)
    return None

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
            'tcp_adaptive_writes','tcp_cooperative_waits','tcp_staged_bytes','tcp_direct_bytes','tcp_queue_us','tcp_idle_us')
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
    parser.add_argument('--recover-stalls', action='store_true',
                        help='Explicit fixed-duration experiment: stop/start upload after an idle stall; keep receiver and clock running')
    parser.add_argument('--max-recoveries', type=int, default=12,
                        help='Bound recovery control writes; after this limit keep measuring zero throughput until the deadline')
    args = parser.parse_args()
    args.log = resolve_log_path(args.log)
    if not 1 <= args.seconds <= 3600 or not 1 <= args.port <= 65535 or not 1<=args.stall_timeout<=120 or not 1<=args.max_recoveries<=60:
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
    monitor_error = None
    monitoring = False
    status_poll_errors = []
    consecutive_poll_errors = 0
    recoveries = []
    recovery_limit_reached = False
    started = time.monotonic()
    try:
        with stdout_path.open('w', encoding='utf-8') as stdout, args.log.with_suffix('.stderr.log').open('w') as stderr:
            command = [sys.executable, str(Path(__file__).with_name('sdio_'+transport+'_receiver.py')),
                       '--source-ip', args.device_ip, '--frames', '0', '--seconds', str(args.seconds+10),
                       '--port', str(args.port),
                       '--log', str(args.log),'--managed']
            if args.recover_stalls:
                # One receiver, one monotonic time base across ALL sessions.
                # A long source outage must not end the measurement early.
                command += ['--idle-timeout', str(args.seconds+10)]
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
            monitoring = True
            print('Fixed DDR benchmark running; do not press device start/stop buttons.', flush=True)
            while process.poll() is None:
                if time.monotonic()-active_started>=args.seconds: break
                state = sample_status(api,status_poll_errors,time.monotonic()-started)
                if state is None:
                    consecutive_poll_errors += 1
                    if consecutive_poll_errors >= 3:
                        raise RuntimeError('Three consecutive status queries failed; stopping test')
                    time.sleep(.5)
                    continue
                consecutive_poll_errors = 0
                samples.append({'t': time.monotonic()-started, 'status': state})
                idle_since,upstream_stall=stalled_since(state,time.monotonic(),idle_since,args.stall_timeout)
                if upstream_stall:
                    if args.recover_stalls:
                        if len(recoveries) >= args.max_recoveries:
                            recovery_limit_reached = True
                            time.sleep(.5)
                            continue
                        if args.seconds-(time.monotonic()-active_started) < 15:
                            time.sleep(.5)
                            continue
                        event = {'detected_t':time.monotonic()-started,
                                 'old_session':state.get('sdio',{}).get('session'),
                                 'restart_accepted':False}
                        recoveries.append(event)
                        print('Idle stall: recording upload stop/start recovery '+str(len(recoveries)),flush=True)
                        # Only this upload is controlled; never scan, HV or CFG.
                        # POSTs are never retried after an unknown outcome.
                        api('/api/sdio/stop', {})
                        drain_deadline=min(time.monotonic()+10,active_started+args.seconds)
                        while True:
                            drained=api('/api/status')
                            ds=drained.get('sdio',{})
                            # STOP merely queues a reset. Old idle fields may
                            # still be visible before bridge_task processes it.
                            # A NEW session plus host handshake proves that
                            # reset and re-enumeration actually completed.
                            if (not drained.get('busy') and not ds.get('running') and ds.get('free_slots')==2 and
                                    ds.get('session')!=event['old_session'] and ds.get('session') and
                                    ds.get('ready',True) and ds.get('host_sdio') and not ds.get('raw_busy')):
                                break
                            if time.monotonic()>=drain_deadline:
                                raise RuntimeError('Recovery stop did not release both frame slots')
                            time.sleep(.05)
                        event['drained_t']=time.monotonic()-started
                        api('/api/sdio/start', {'frames':0})
                        event['restarted_t']=time.monotonic()-started
                        event['restart_accepted']=True  # Actual resumed frames are evidenced by the receiver log.
                        idle_since=None
                        continue
                    unexpected_stop=True
                    print('Upstream stalled: stream armed, two free slots, no sender activity.',file=sys.stderr)
                    break
                if not state.get('sdio', {}).get('running'):
                    unexpected_stop = True
                    break
                time.sleep(.5)
            if process.poll() is not None and time.monotonic()-active_started < args.seconds:
                unexpected_stop = True  # e.g. receiver idle timeout while device claims running
    except Exception as exc:
        monitor_error = str(exc)
        unexpected_stop = True
        print('Benchmark monitoring failed: '+monitor_error,file=sys.stderr)
        # A start timeout is an unknown control outcome, not a skipped sample.
        # Preserve the original caller-visible failure and finally-stop path.
        if not monitoring:
            raise
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
                  'monitor_error':monitor_error,'status_poll_errors':status_poll_errors,
                  'recover_stalls':args.recover_stalls,'recoveries':recoveries,
                  'recovery_limit_reached':recovery_limit_reached,
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
