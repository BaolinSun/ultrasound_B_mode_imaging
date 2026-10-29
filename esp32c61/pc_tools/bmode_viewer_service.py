"""Viewer lifecycle and explicit, fixed acquisition/focus actions."""
from collections import deque
import ipaddress
import json
import queue
import re
import socket
import threading
import time
from types import SimpleNamespace
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from bmode_imaging import FRAME_BYTES, ImagingWorker
import bmode_settings as settings_model
from sdio_tcp_receiver import TCPReceiver
from sdio_udp_receiver import DatagramPump, FrameReceiver
from bmode_control import ViewerControl, FocusLibrary
from zynq_wifi_control import ControlFailure, validate_command


class DeviceRejected(RuntimeError):
    pass


class DeviceAPI:
    def __init__(self, device_ip):
        self.base = 'http://'+str(ipaddress.IPv4Address(device_ip))

    def __call__(self, path, payload=None):
        # Fixed viewer operations only; the local HTTP server has no command proxy.
        if payload is None:
            if path not in ('/api/status', '/api/config') and not re.fullmatch(r'/api/zynq/jobs/[0-9a-f]{8}', path):
                raise ValueError('read route not allowed')
        elif path == '/api/zynq/commands':
            if (set(payload) != {'client_id','request_id','commands','allow_hv'} or
                    not all(re.fullmatch(r'[0-9a-f]{8}', payload.get(k, '')) for k in ('client_id','request_id')) or
                    type(payload['allow_hv']) is not bool or not isinstance(payload['commands'], list) or
                    not 1 <= len(payload['commands']) <= 8):
                raise ValueError('invalid viewer control batch')
            for line in payload['commands']:
                validate_command(line)
                if line not in ('STATUS','HV ENABLE','HV DISABLE','BEAM START','CFG END','CFG ABORT') and not line.startswith(('CFG BEGIN ', 'BRAM WR delay_profile ')):
                    raise ValueError('viewer command not allowed')
                if line == 'HV ENABLE' and not payload['allow_hv']:
                    raise ValueError('HV confirmation required')
        elif not ((path == '/api/sdio/start' and payload == {'frames': 0}) or (path == '/api/sdio/stop' and payload == {})):
            raise ValueError('write route not allowed')
        data = None if payload is None else json.dumps(payload).encode('ascii')
        try:
            request = Request(self.base+path, data=data, headers={'Content-Type': 'application/json'})
            with urlopen(request, timeout=3) as response:
                result = json.load(response)
                if not isinstance(result, dict):
                    raise ValueError('设备返回无效 JSON')
                return result
        except HTTPError as exc:
            if exc.code >= 500 or path.startswith('/api/zynq/jobs/'):
                raise ControlFailure(f'设备 HTTP {exc.code}，执行结果未知', unknown=True) from None
            raise DeviceRejected(f'设备拒绝请求 HTTP {exc.code}') from None
        except (OSError, ValueError) as exc:
            raise ControlFailure('设备连接或响应异常，执行结果未知: '+str(exc), unknown=True) from None


def local_route_ip(device_ip):
    # connect() chooses a route but sends no UDP payload and changes no device state.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect((device_ip, 80))
        return probe.getsockname()[0]


class Transport:
    def __init__(self, protocol, bind, port, source_ip, imaging):
        self.protocol, self.imaging = protocol, imaging
        self.lock = threading.Lock()
        self.frames = self.bytes = self.bad_frames = self.protocol_errors = 0
        self.events = deque(maxlen=64)
        self.last_data = time.monotonic()
        self.error = None
        self.stop = threading.Event()
        self.planned = False
        self.pump = None
        kind = socket.SOCK_STREAM if protocol == 'tcp' else socket.SOCK_DGRAM
        self.sock = socket.socket(socket.AF_INET, kind)
        try:
            # Do not SO_REUSEADDR on Windows: never share/hijack an occupied data port.
            self.sock.bind((bind, port))
            self.sock.settimeout(.1)
            if protocol == 'tcp':
                self.sock.listen(1)
                args = SimpleNamespace(length=None, test_pattern=False, source_ip=source_ip,
                                       seconds=0, idle_timeout=30, frame_timeout=30, frames=0, output_dir=None)
                self.receiver = TCPReceiver(args, self, on_frame=imaging.submit, stop_on_error=True)
            else:
                self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4*1024*1024)
                self.receiver = FrameReceiver(length=None, timeout=2.)
                self.source_ip = source_ip
            self.thread = threading.Thread(target=self._run, name='bmode-receive', daemon=True)
        except Exception:
            self.sock.close()
            raise

    def launch(self):
        self.thread.start()

    def submit(self, event, complete=None):
        # TCPReceiver event sink: constant-sized bookkeeping, no print/disk/render.
        with self.lock:
            name = event['event']
            if name == 'frame':
                self.frames += 1
                self.bytes += event['bytes']
                self.last_data = time.monotonic()
            elif name == 'discard':
                self.bad_frames += 1
            elif name in ('protocol_error', 'ack_error'):
                self.protocol_errors += 1
            if name != 'frame':
                self.events.append({'event': name, 'reason': event.get('reason', event.get('error', ''))})

    def _run(self):
        try:
            if self.protocol == 'tcp':
                self.receiver.serve(self.sock)
                if not self.planned and not self.stop.is_set():
                    self.error = self.receiver.error or 'TCP 接收结束: '+self.receiver.reason
                return
            self.pump = DatagramPump(self.sock)
            self.pump.thread.start()
            while not self.stop.is_set():
                if self.pump.error:
                    raise OSError(self.pump.error)
                try:
                    packet, source, arrived = self.pump.items.get(timeout=.1)
                except queue.Empty:
                    for event in self.receiver.expire():
                        self.submit(event)
                    continue
                if source[0] != self.source_ip:
                    continue
                events, complete = self.receiver.feed(packet, source, arrived)
                for event in events:
                    self.submit(event)
                if complete:
                    (session, frame), data = complete
                    self.imaging.submit(session, frame, data)
        except Exception as exc:
            if not self.planned and not self.stop.is_set():
                self.error = str(exc)

    def quiesce(self):
        self.planned = True
        if self.protocol == 'tcp':
            self.receiver.quiescing.set()

    def close(self):
        self.stop.set()
        if self.protocol == 'tcp':
            self.receiver.stop.set()
        if self.thread.ident is not None:
            self.thread.join(1)
        if self.pump:
            self.pump.close()
        self.sock.close()
        if self.thread.is_alive():
            self.thread.join(1)

    def snapshot(self):
        with self.lock:
            return {'received_frames': self.frames, 'received_bytes': self.bytes,
                    'bad_frames': self.bad_frames, 'protocol_errors': self.protocol_errors,
                    'invalid_datagrams': getattr(self.receiver, 'invalid_datagrams', 0),
                    'local_queue_drops': self.pump.drops if self.pump else 0,
                    'last_event': dict(self.events[-1]) if self.events else None}


class ViewerService:
    def set_display_settings(self, value, expected_revision, profile=False):
        with self.lock:
            current = self.imaging.get_settings()['settings']
            settings = settings_model.from_profile(value, current) if profile else settings_model.validate(value)
            if self.state in ('starting', 'running', 'stopping') and settings['angles_deg'] != current['angles_deg']:
                raise RuntimeError('修改角度映射前请停止接收；不会替用户停止其他上传')
            return self.imaging.set_settings(settings, expected_revision)

    def __init__(self, device_ip, port=50001, data_bind='0.0.0.0', api=None, route_ip=None, imaging=None):
        self.device_ip, self.port, self.data_bind = device_ip, port, data_bind
        self.api, self.route_ip = api or DeviceAPI(device_ip), route_ip or local_route_ip
        self.imaging = imaging or ImagingWorker()
        self.lock = threading.Lock()
        self.state, self.error, self.protocol = 'idle', '', None
        self.worker = None
        self.stop_request = threading.Event()
        self.transport = None
        self.started = None
        self.history = deque(maxlen=64)
        self.device_history = deque(maxlen=8)
        self.closed = False
        self.control = ViewerControl(device_ip, self.api)
        self.focus_library = FocusLibrary()
        self.frozen = False
        self.control_action = None
        self.focus_job = None
        self.focus_cancel = threading.Event()
        self.focus_progress = 0
        self.focus_last = None
        self.off_thread = None
        self.off_error = ''
        self.operation_generation = 0

    def _state(self, state, error=''):
        with self.lock:
            self.state, self.error = state, error

    def start(self):
        with self.lock:
            if self.closed:
                raise RuntimeError('服务已关闭')
            if self.off_thread and self.off_thread.is_alive():
                raise RuntimeError('高压关断操作尚未结束，请稍后重试')
            if not self.imaging.thread.is_alive():
                raise RuntimeError('图像处理任务已退出，请重新启动 Python 服务')
            if self.state in ('starting', 'running'):
                return
            if self.worker and self.worker.is_alive():
                raise RuntimeError('正在停止，请稍后重试')
            self.state, self.error, self.protocol = 'starting', '', None
            self.operation_generation += 1
            self.stop_request = threading.Event()
            self.transport = None
            self.started = time.monotonic()
            self.history.clear()
            self.device_history.clear()
            self.imaging.reset()
            self.frozen = False
            self.control_action = None
            self.focus_job = None
            self.focus_cancel = threading.Event()
            self.off_error = ''
            self.control = ViewerControl(self.device_ip, self.api)
            self.worker = threading.Thread(target=self._run, name='bmode-control', daemon=True)
            self.worker.start()

    def stop(self, explicit=True):
        with self.lock:
            self.operation_generation += 1
            self.focus_cancel.set()
            self.focus_job = None
            if self.worker and self.worker.is_alive():
                self.state = 'stopping'
                self.stop_request.set()
                if explicit and (not self.off_thread or not self.off_thread.is_alive()):
                    self.off_error = ''
                    self.off_thread = threading.Thread(target=self._priority_off, daemon=True)
                    self.off_thread.start()
            elif explicit:
                self.state, self.error = 'stopping', ''
                self.worker = threading.Thread(target=self._stop_only, daemon=True)
                self.worker.start()

    def _priority_off(self):
        try:
            ViewerControl(self.device_ip, self.api).off(urgent=True)
        except Exception as exc:
            self.off_error = '优先关断未确认: '+str(exc)

    def _priority_result(self, verified=False):
        if self.off_thread and self.off_thread.is_alive():
            self.off_thread.join(2)
        if verified:
            return ''
        if self.off_thread and self.off_thread.is_alive():
            return '优先关断仍未确认，请核对设备状态'
        return self.off_error

    def _stop_only(self):
        self.off_error = ''
        self._priority_off()
        with self.lock:
            self.frozen = False
            self.control_action = None
        self._state('error' if self.off_error else 'idle', self.off_error)

    def set_frozen(self, frozen):
        if type(frozen) is not bool:
            raise ValueError('无效冻结状态')
        with self.lock:
            if self.state != 'running' or self.control_action:
                raise RuntimeError('仅在正常接收且控制空闲时切换冻结')
            if frozen == self.frozen:
                return
            self.control_action = 'freeze' if frozen else 'resume'

    def set_focus(self, depth):
        with self.lock:
            generation = self.operation_generation
        commands = self.focus_library.commands(depth)  # Validate before stopping anything.
        with self.lock:
            if generation != self.operation_generation:
                raise RuntimeError('设备操作已变化，取消本次焦点配置')
            if self.closed or self.state in ('starting','stopping','configuring') or self.control_action:
                raise RuntimeError('控制操作进行中，请稍后重试')
            if self.off_thread and self.off_thread.is_alive():
                raise RuntimeError('高压关断操作尚未结束，请稍后重试')
            if self.worker and self.worker.is_alive() and self.state != 'running':
                raise RuntimeError('上一操作尚未结束')
            self.focus_job = (depth, commands)
            self.focus_cancel = threading.Event()
            self.focus_progress = 0
            self.off_error = ''
            self.control_action = 'focus'
            if self.worker and self.worker.is_alive():
                self.state = 'stopping'
                self.stop_request.set()
            else:
                self.state, self.error = 'configuring', ''
                self.worker = threading.Thread(target=self._focus_only, daemon=True)
                self.worker.start()

    def _focus_only(self):
        failure = ''
        touched = False
        off_verified = False
        try:
            state = self.api('/api/status')
            sdio, control = state.get('sdio', {}), state.get('zynq_control', {})
            if (state.get('busy') or sdio.get('running') or sdio.get('stopping') or sdio.get('free_slots') != 2 or
                    not control.get('ready') or not control.get('synced') or control.get('busy') or control.get('config_owned') or
                    control.get('afe_state') != 'ready' or control.get('tx_state') != 'ready'):
                raise RuntimeError('设备有其他传输/配置占用或 AFE/TX 未就绪，不接管')
            with self.lock:
                job = self.focus_job
                self.state = 'configuring'
            if job is None or self.focus_cancel.is_set():
                return
            depth, commands = job
            touched = True
            self.control = ViewerControl(self.device_ip, self.api)
            def progress(n, total):
                with self.lock:
                    self.focus_progress = n
            self.control.focus(commands, self.focus_cancel, progress)
            with self.lock:
                self.focus_last = depth
        except Exception as exc:
            failure = '' if str(exc) == '操作已取消' and self.focus_cancel.is_set() else str(exc)
        finally:
            if touched:
                try:
                    self.control.off()
                    off_verified = True
                except Exception as exc:
                    failure += '；关断未确认: '+str(exc)
            off_error = self._priority_result(off_verified)
            if off_error:
                failure = (failure+'；' if failure else '')+off_error
            with self.lock:
                self.focus_job = None
                self.control_action = None
                self.frozen = False
            self._state('error' if failure else 'idle', failure)

    def _preflight(self):
        state, config = self.api('/api/status'), self.api('/api/config')
        sdio = state.get('sdio', {})
        control = state.get('zynq_control', {})
        if (not state.get('connected') or not state.get('upload_ready') or
                not sdio.get('ready') or not sdio.get('host_sdio')):
            raise RuntimeError('设备 Wi-Fi/SDIO 尚未就绪，请检查原配置页')
        if (state.get('busy') or sdio.get('stopping') or sdio.get('running') or sdio.get('raw_busy') or
                sdio.get('free_slots') != 2 or control.get('config_owned') or control.get('busy')):
            raise RuntimeError('设备被其他上传或配置占用，不接管现有任务')
        if (not control.get('ready') or not control.get('synced') or
                control.get('afe_state') != 'ready' or control.get('tx_state') != 'ready'):
            raise RuntimeError('PS 尚未握手或 AFE/TX 未就绪，请在控制台查询状态')
        protocol = state.get('sdio_transport')
        if protocol not in ('tcp', 'udp'):
            raise RuntimeError('设备没有报告有效 TCP/UDP 编译协议')
        pc_ip = self.route_ip(self.device_ip) if self.data_bind == '0.0.0.0' else self.data_bind
        if config.get('target_ip') != pc_ip or config.get('target_port') != self.port:
            raise RuntimeError(f'请在 ESP32 原配置页将目标设为 {pc_ip}:{self.port}，本工具不会自动修改配置')
        return protocol

    def _run(self):
        transport, armed, failure, hardware, off_verified = None, False, '', False, False
        try:
            protocol = self._preflight()
            if self.stop_request.is_set():
                return
            transport = Transport(protocol, self.data_bind, self.port, self.device_ip, self.imaging)
            with self.lock:
                self.transport, self.protocol = transport, protocol
            transport.launch()  # Listening and receiver ready BEFORE asking ESP32 to send.
            if self.stop_request.is_set():
                return
            hardware = True
            self.control.start(self.stop_request)
            if self.stop_request.is_set():
                return
            armed = True  # A lost HTTP response can still mean the device accepted start.
            try:
                self.api('/api/sdio/start', {'frames': 0})
            except DeviceRejected:
                armed = False  # Explicit rejection: never stop another owner's task.
                raise
            with self.lock:
                if not self.stop_request.is_set():
                    self.state = 'running'
            next_poll = 0.
            while not self.stop_request.wait(.1):
                with self.lock:
                    action = self.control_action
                if action in ('freeze','resume'):
                    (self.control.freeze if action == 'freeze' else self.control.resume)(self.stop_request)
                    with self.lock:
                        self.frozen = action == 'freeze'
                        self.control_action = None
                if transport.error or not transport.thread.is_alive():
                    raise RuntimeError(transport.error or '接收任务意外结束')
                image_error = self.imaging.snapshot()['processing_error']
                if image_error:
                    raise RuntimeError(image_error)
                if time.monotonic()-transport.last_data >= 30:
                    raise RuntimeError('30 秒未收到有效载荷；请检查上传及网络连接')
                if time.monotonic() >= next_poll:
                    device=self.api('/api/status').get('sdio', {})
                    with self.lock:
                        self.device_history.append((time.monotonic(),dict(device)))
                    if not device.get('running'):
                        raise RuntimeError('ESP32 上传已结束，请检查设备后手动重新开始')
                    next_poll = time.monotonic()+1
        except Exception as exc:
            failure = '' if str(exc) == '操作已取消' and self.stop_request.is_set() else str(exc)
        finally:
            if transport:
                transport.quiesce()
            if hardware:
                try:
                    self.control.off()
                    off_verified = True
                except Exception as exc:
                    failure = (failure+'；' if failure else '')+'关闭扫描/高压未确认: '+str(exc)
            if armed:
                try:
                    self.api('/api/sdio/stop', {})
                    deadline = time.monotonic()+7
                    while True:
                        status = self.api('/api/status')
                        if (not status.get('busy') and not status.get('sdio', {}).get('running')
                            and not status.get('sdio', {}).get('stopping')
                            and status.get('sdio', {}).get('free_slots') == 2):
                            with self.lock:
                                self.device_history.append((time.monotonic(), dict(status['sdio'])))
                            break
                        if time.monotonic() >= deadline:
                            raise RuntimeError('设备未在截止时间内停止；请在原页面核对上传状态')
                        time.sleep(.05)
                except Exception as exc:
                    failure = (failure+'；' if failure else '')+'停止未确认: '+str(exc)
            if transport:
                transport.close()
            self.imaging.finish()
            off_error = self._priority_result(off_verified)
            if off_error:
                failure = (failure+'；' if failure else '')+off_error
            with self.lock:
                focus = self.focus_job is not None and not self.focus_cancel.is_set()
                self.frozen = False
                if not focus:
                    self.control_action = None
            if focus and not failure:
                self._focus_only()
                return
            if focus:
                with self.lock:
                    self.focus_job = None
                    self.control_action = None
            self._state('error' if failure else 'idle', failure)

    def snapshot(self):
        now = time.monotonic()
        with self.lock:
            transport = self.transport
            result = {'state': self.state, 'error': self.error, 'transport': self.protocol,
                      'device_ip': self.device_ip, 'port': self.port,
                      'elapsed_s': 0. if self.started is None else now-self.started}
            result.update(frozen=self.frozen, control_action=self.control_action,
                          focus={'target_mm': self.focus_job[0] if self.focus_job else None,
                                 'completed': self.focus_progress, 'total': 2048,
                                 'last_applied_mm': self.focus_last})
        net = transport.snapshot() if transport else {'received_frames': 0, 'received_bytes': 0, 'bad_frames': 0,
                                                     'protocol_errors': 0, 'invalid_datagrams': 0, 'local_queue_drops': 0}
        image = self.imaging.snapshot()
        with self.lock:
            if not self.history or now-self.history[-1][0] >= .25:
                self.history.append((now, net['received_frames'], net['received_bytes'], image['images']))
            # Five-second rolling rates; include silence so a stopped stream reaches 0.
            samples = [sample for sample in self.history if now-sample[0] <= 5]
            baseline = samples[0] if samples else (now, 0, 0, 0)
        elapsed = now-baseline[0]
        with self.lock:
            device_samples=list(self.device_history)
        source_fps=sdio_fps=0.
        if len(device_samples)>1:
            dt=device_samples[-1][0]-device_samples[0][0]
            if dt>0:
                source_delta=(device_samples[-1][1].get('source_published',0)-device_samples[0][1].get('source_published',0))&0xffffffff
                sdio_delta=(device_samples[-1][1].get('received_frames',0)-device_samples[0][1].get('received_frames',0))&0xffffffff
                if source_delta<0x80000000: source_fps=source_delta/dt
                if sdio_delta<0x80000000: sdio_fps=sdio_delta/dt
        result.update(source_fps=source_fps,sdio_fps=sdio_fps,source_status=device_samples[-1][1] if device_samples else {})
        result.update(net)
        result.update(image)
        result.update(receive_fps=max(0., (net['received_frames']-baseline[1])/elapsed) if elapsed else 0.,
                      payload_mbit_s=max(0., (net['received_bytes']-baseline[2])*8/elapsed/1e6) if elapsed else 0.,
                      display_fps=max(0., (image['images']-baseline[3])/elapsed) if elapsed else 0.)
        return result

    def close(self):
        with self.lock:
            self.closed = True
        self.stop(explicit=False)
        if self.worker:
            self.worker.join(14)
            if self.worker.is_alive():
                self._state('error', '服务退出时尚未确认设备停止，请在原 ESP32 页面核对上传状态')
        self.imaging.close()
