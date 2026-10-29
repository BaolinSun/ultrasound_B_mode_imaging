"""PC-only live viewer lifecycle. Only SDIO upload start/stop may be written."""
from collections import deque
import ipaddress
import json
import queue
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


class DeviceRejected(RuntimeError):
    pass


class DeviceAPI:
    def __init__(self, device_ip):
        self.base = 'http://'+str(ipaddress.IPv4Address(device_ip))

    def __call__(self, path, payload=None):
        # Deliberately not a generic command/configuration proxy.
        if payload is None:
            if path not in ('/api/status', '/api/config'):
                raise ValueError('read route not allowed')
        elif not ((path == '/api/sdio/start' and payload == {'frames': 0}) or
                  (path == '/api/sdio/stop' and payload == {})):
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
            raise DeviceRejected(f'设备拒绝请求 HTTP {exc.code}') from None


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
            if self.state in ('starting', 'running', 'stopping') and any(settings[k] != current[k] for k in settings_model.GEOMETRY):
                raise RuntimeError('修改几何配置前请停止接收；不会替用户停止其他上传')
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

    def _state(self, state, error=''):
        with self.lock:
            self.state, self.error = state, error

    def start(self):
        with self.lock:
            if self.closed:
                raise RuntimeError('服务已关闭')
            if not self.imaging.thread.is_alive():
                raise RuntimeError('图像处理任务已退出，请重新启动 Python 服务')
            if self.state in ('starting', 'running'):
                return
            if self.worker and self.worker.is_alive():
                raise RuntimeError('正在停止，请稍后重试')
            self.state, self.error, self.protocol = 'starting', '', None
            self.stop_request = threading.Event()
            self.transport = None
            self.started = time.monotonic()
            self.history.clear()
            self.device_history.clear()
            self.imaging.reset()
            self.worker = threading.Thread(target=self._run, name='bmode-control', daemon=True)
            self.worker.start()

    def stop(self):
        with self.lock:
            if self.worker and self.worker.is_alive():
                self.state = 'stopping'
                self.stop_request.set()

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
        protocol = state.get('sdio_transport')
        if protocol not in ('tcp', 'udp'):
            raise RuntimeError('设备没有报告有效 TCP/UDP 编译协议')
        pc_ip = self.route_ip(self.device_ip) if self.data_bind == '0.0.0.0' else self.data_bind
        if config.get('target_ip') != pc_ip or config.get('target_port') != self.port:
            raise RuntimeError(f'请在 ESP32 原配置页将目标设为 {pc_ip}:{self.port}，本工具不会自动修改配置')
        return protocol

    def _run(self):
        transport, armed, failure = None, False, ''
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
            failure = str(exc)
        finally:
            if transport:
                transport.quiesce()
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
            self._state('error' if failure else 'idle', failure)

    def snapshot(self):
        now = time.monotonic()
        with self.lock:
            transport = self.transport
            result = {'state': self.state, 'error': self.error, 'transport': self.protocol,
                      'device_ip': self.device_ip, 'port': self.port,
                      'elapsed_s': 0. if self.started is None else now-self.started}
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
        self.stop()
        if self.worker:
            self.worker.join(14)
            if self.worker.is_alive():
                self._state('error', '服务退出时尚未确认设备停止，请在原 ESP32 页面核对上传状态')
        self.imaging.close()
