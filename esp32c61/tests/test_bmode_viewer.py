import contextlib
import copy
from pathlib import Path
import json
import socket
import sys
import threading
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'pc_tools'))
import bmode_viewer_service as v
import bmode_web_viewer as web
import sdio_tcp_receiver as tcp
import sdio_udp_receiver as udp
from test_bmode_imaging import payload, wait_for


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def header(data, frame=1, session=7, crc=None):
    return tcp.HEADER.pack(tcp.MAGIC, 1, 24, 0, session, frame, len(data), zlib.crc32(data) if crc is None else crc)


def exact(conn, length):
    result = bytearray()
    while len(result) < length:
        data = conn.recv(length-len(result))
        if not data: raise AssertionError('unexpected EOF')
        result.extend(data)
    return bytes(result)


class FakeDevice:
    def __init__(self, port, protocol='tcp'):
        self.config = {'target_ip': '127.0.0.1', 'target_port': port}
        self.status = {'connected': True, 'upload_ready': True, 'busy': False, 'sdio_transport': protocol,
                       'sdio': {'ready': True, 'host_sdio': True, 'free_slots': 2, 'running': False},
                       'zynq_control': {'ready':True,'synced':True,'busy':False,'config_owned':False,'afe_state':'ready','tx_state':'ready'}}
        self.ps = {'run':0,'hv':0,'cfg':0,'seq':0,'down':0}
        self.jobs = {}
        self.commands = []
        self.calls = []
        self.reject = self.fail_stop = False
        self.on_start = None

    def __call__(self, path, body=None):
        self.calls.append((path, body))
        if body is None:
            if path.startswith('/api/zynq/jobs/'):
                return copy.deepcopy(self.jobs[path.split('/')[-1]])
            return copy.deepcopy(self.status if path == '/api/status' else self.config)
        if path == '/api/zynq/commands':
            results=[]
            for command in body['commands']:
                self.commands.append(command)
                if command == 'STATUS':
                    response='@STATUS v=1 '+' '.join(f'{k}={val}' for k,val in self.ps.items())+' afe=ready tx=ready'
                else:
                    if command == 'HV DISABLE':self.ps.update(run=0,hv=0)
                    if command == 'HV ENABLE':self.ps['hv']=1
                    if command == 'BEAM START':self.ps['run']=1
                    if command.startswith('CFG BEGIN '):self.ps.update(seq=int(command.split()[-1]),cfg=1,run=0,hv=0)
                    if command in ('CFG END','CFG ABORT'):self.ps.update(cfg=0,run=0,hv=0)
                    tag='CFG_BEGIN' if command.startswith('CFG BEGIN ') else 'BRAM_WR' if command.startswith('BRAM WR ') else command.replace(' ','_')
                    response=f'@ACK {self.ps["seq"]} {tag}'
                results.append({'command':command,'response':response})
            key=f'{len(self.jobs)+1:08x}'
            self.jobs[key]={'busy':False,'state':'succeeded','count':len(results),'completed':len(results),'results':results}
            return {'job_id':key}
        if path == '/api/sdio/start':
            if self.reject: raise v.DeviceRejected('occupied')
            self.status['sdio']['running'] = True
            if self.on_start: self.on_start()
        elif path == '/api/sdio/stop':
            if self.fail_stop: raise OSError('offline')
            self.status['sdio']['running'] = False
        else:
            raise AssertionError('unsafe API '+path)
        return {'accepted': True}


class ServiceTests(unittest.TestCase):
    def make_service(self, protocol='tcp'):
        device = FakeDevice(free_port(), protocol)
        service = v.ViewerService('127.0.0.1', device.config['target_port'], '127.0.0.1', api=device)
        self.addCleanup(service.close)
        return service, device

    def test_tcp_listener_precedes_start_crc_ack_then_image_and_clean_stop(self):
        service, device = self.make_service()
        connections = []
        def connect():
            connections.append(socket.create_connection(('127.0.0.1', device.config['target_port']), timeout=1))
        device.on_start = connect
        service.start();service.start()
        wait_for(lambda: service.snapshot()['state'] == 'running')
        self.assertEqual(len(connections), 1)
        with connections[0] as conn:
            data = payload()
            conn.sendall(header(data)+data)
            self.assertEqual(tcp.ACK.unpack(exact(conn, 20))[3], 0)
            wait_for(lambda: service.snapshot()['image_available'])
            self.assertEqual(service.snapshot()['lines'], 64)
            service.stop();service.stop()
            wait_for(lambda: service.snapshot()['state'] == 'idle')
        self.assertEqual([p for p, body in device.calls if body is not None and p.startswith('/api/sdio/')], ['/api/sdio/start', '/api/sdio/stop'])
        self.assertEqual(device.ps['run'],0)
        self.assertEqual(device.ps['hv'],0)
        self.assertEqual(service.snapshot()['received_frames'], 1)

    def test_stop_waits_for_host_and_callback_ownership_before_closing_receiver(self):
        service, device = self.make_service()
        stopped = threading.Event()
        def delayed(path, body=None):
            result = device(path, body)
            if path == '/api/sdio/stop' and body is not None:
                device.status['sdio'].update(stopping=True, free_slots=1)
                stopped.set()
            return result
        service.api = delayed
        service.start();wait_for(lambda: service.snapshot()['state'] == 'running')
        transport = service.transport
        service.stop();service.stop();self.assertTrue(stopped.wait(2))
        time.sleep(.15)
        self.assertEqual(service.snapshot()['state'], 'stopping')
        self.assertFalse(transport.stop.is_set())
        device.status['sdio']['stopping'] = False
        time.sleep(.1)
        self.assertFalse(transport.stop.is_set())  # Callback/slot still outstanding.
        device.status['sdio']['free_slots'] = 2
        wait_for(lambda: service.snapshot()['state'] == 'idle')
        self.assertTrue(transport.stop.is_set())
        self.assertFalse(service.snapshot()['source_status']['running'])
        self.assertFalse(service.snapshot()['source_status']['stopping'])
        self.assertEqual(sum(p == '/api/sdio/stop' and b is not None for p, b in device.calls), 1)

    def test_stopping_device_is_not_taken_over(self):
        service, device = self.make_service()
        device.status['sdio']['stopping'] = True
        service.start();wait_for(lambda: service.snapshot()['state'] == 'error')
        self.assertFalse(any(body is not None for _, body in device.calls))

    def test_wrong_target_busy_protocol_and_port_occupied_never_stop_other_task(self):
        for issue in ('target', 'busy', 'protocol', 'port'):
            with self.subTest(issue=issue):
                service, device = self.make_service()
                occupied = None
                if issue == 'target': device.config['target_ip'] = '192.0.2.5'
                if issue == 'busy': device.status['sdio']['running'] = True
                if issue == 'protocol': device.status['sdio_transport'] = 'invalid'
                if issue == 'port':
                    occupied = socket.socket();occupied.bind(('127.0.0.1', device.config['target_port']));occupied.listen()
                try:
                    service.start();wait_for(lambda: service.snapshot()['state'] == 'error')
                    self.assertFalse([body for path, body in device.calls if body is not None])
                finally:
                    if occupied: occupied.close()

    def test_explicit_start_rejection_does_not_stop_foreign_upload(self):
        service, device = self.make_service()
        device.reject = True
        service.start();wait_for(lambda: service.snapshot()['state'] == 'error')
        self.assertNotIn(('/api/sdio/stop', {}), device.calls)

    def test_tcp_format_errors_do_not_nack_good_transport_or_fabricate_image(self):
        service, device = self.make_service()
        service.start();wait_for(lambda: service.snapshot()['state'] == 'running')
        with socket.create_connection(('127.0.0.1', device.config['target_port'])) as conn:
            conn.settimeout(1)
            data = bytes(v.FRAME_BYTES)
            conn.sendall(header(data)+data)
            self.assertEqual(tcp.ACK.unpack(exact(conn, 20))[3], 0)
            wait_for(lambda: service.snapshot()['format_errors'] == 1)
            self.assertFalse(service.snapshot()['image_available'])
            self.assertEqual(service.snapshot()['state'], 'running')
            service.stop();wait_for(lambda: service.snapshot()['state'] == 'idle')

    def test_tcp_crc_failure_stops_round_no_reconnect_and_stop_failure_is_explicit(self):
        service, device = self.make_service()
        service.start();wait_for(lambda: service.snapshot()['state'] == 'running')
        with socket.create_connection(('127.0.0.1', device.config['target_port'])) as conn:
            conn.settimeout(1);data = payload()
            conn.sendall(header(data, crc=0)+data)
            self.assertEqual(tcp.ACK.unpack(exact(conn, 20))[3], 1)
        wait_for(lambda: service.snapshot()['state'] == 'error')
        self.assertFalse(service.snapshot()['image_available'])
        self.assertEqual(service.snapshot()['bad_frames'], 1)
        self.assertIn(('/api/sdio/stop', {}), device.calls)
        service2, device2 = self.make_service()
        service2.start();wait_for(lambda: service2.snapshot()['state'] == 'running')
        device2.fail_stop = True;service2.stop()
        wait_for(lambda: service2.snapshot()['state'] == 'error')
        self.assertIn('停止未确认', service2.snapshot()['error'])

    def test_partial_tcp_disconnect_is_error_and_cleans_up(self):
        service, device = self.make_service()
        service.start();wait_for(lambda: service.snapshot()['state'] == 'running')
        with socket.create_connection(('127.0.0.1', device.config['target_port'])) as conn:
            data = payload();conn.sendall(header(data)+data[:200])
        wait_for(lambda: service.snapshot()['state'] == 'error')
        self.assertEqual(service.snapshot()['received_frames'], 0)
        self.assertFalse(device.status['sdio']['running'])

    def test_udp_reordering_duplicate_crc_bad_and_missing_not_rendered(self):
        service, device = self.make_service('udp')
        service.start();wait_for(lambda: service.snapshot()['state'] == 'running')
        data = payload()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            address = ('127.0.0.1', device.config['target_port'])
            packets = [udp.encode_packet(data, 7, 1, offset) for offset in range(0, len(data), 1440)]
            for packet in reversed(packets):
                sock.sendto(packet, address);time.sleep(.0002)
            sock.sendto(packets[0], address)
            wait_for(lambda: service.snapshot()['image_available'])
            self.assertEqual(service.snapshot()['received_frames'], 1)
            for offset in range(0, len(data), 1440):
                bad = bytearray(udp.encode_packet(data, 7, 2, offset));bad[32] ^= 1
                sock.sendto(bad, address);time.sleep(.0002)
            wait_for(lambda: service.snapshot()['bad_frames'] == 1)
            sock.sendto(udp.encode_packet(data, 7, 3, 0), address)
            wait_for(lambda: service.snapshot()['bad_frames'] == 2, 4)
            self.assertEqual(service.snapshot()['state'], 'running')
        service.stop();wait_for(lambda: service.snapshot()['state'] == 'idle')

    def test_device_api_has_only_explicit_upload_writes(self):
        api = v.DeviceAPI('127.0.0.1')
        for route, body in [('/api/config', {}), ('/api/zynq/commands', {}), ('/api/sdio/start', {'frames': 1})]:
            with self.assertRaises(ValueError): api(route, body)

    def test_five_dynamic_lengths_through_real_local_tcp_and_udp_receivers(self):
        for protocol in ('tcp', 'udp'):
            with self.subTest(protocol=protocol):
                service, device = self.make_service(protocol)
                service.start();wait_for(lambda: service.snapshot()['state'] == 'running')
                kind = socket.SOCK_STREAM if protocol == 'tcp' else socket.SOCK_DGRAM
                with socket.socket(socket.AF_INET, kind) as conn:
                    conn.settimeout(3)
                    address = ('127.0.0.1', device.config['target_port'])
                    if protocol == 'tcp':conn.connect(address)
                    for down in range(5):
                        data = payload(down=down)
                        if protocol == 'tcp':
                            conn.sendall(header(data, frame=down+1)+data)
                            self.assertEqual(tcp.ACK.unpack(exact(conn, 20))[3], 0)
                        else:
                            for offset in range(0, len(data), 1440):
                                conn.sendto(udp.encode_packet(data, 7, down+1, offset), address)
                                time.sleep(.0002)
                        wait_for(lambda: service.snapshot()['image_frame'] == down+1)
                        self.assertEqual(service.snapshot()['samples_per_line'], 6144 >> down)
                self.assertEqual(service.snapshot()['received_frames'], 5)
                self.assertEqual(service.snapshot()['bad_frames'], 0)
                service.stop();wait_for(lambda: service.snapshot()['state'] == 'idle')


class HTTPTests(unittest.TestCase):
    def test_http_boundaries_no_side_effect_on_get_and_latest_png(self):
        device = FakeDevice(free_port())
        service = v.ViewerService('127.0.0.1', device.config['target_port'], '127.0.0.1', api=device)
        server = web.make_server(service, ('127.0.0.1', 0))
        thread = threading.Thread(target=server.serve_forever, daemon=True);thread.start()
        base = f'http://127.0.0.1:{server.server_port}'
        self.addCleanup(service.close)
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        for route in ('/', '/viewer.js', '/api/status'):
            with urlopen(base+route) as response: self.assertEqual(response.status, 200)
        self.assertFalse(device.calls)
        with urlopen(base+'/api/image?after=0') as response: self.assertEqual(response.status, 204)
        with self.assertRaises(HTTPError) as failure:
            urlopen(Request(base+'/api/viewer/start', b'{}', headers={'Content-Type': 'application/json', 'Origin': 'http://evil.test'}))
        self.assertEqual(failure.exception.code, 403)
        for route, body in [('/api/config', b'{}'), ('/api/viewer/start', b'{"HV":"ENABLE"}')]:
            with self.assertRaises(HTTPError): urlopen(Request(base+route, body, headers={'Content-Type': 'application/json'}))
        self.assertFalse(device.calls)
        with urlopen(Request(base+'/api/viewer/start', b'{"confirm_hv":true}', headers={'Content-Type': 'application/json'})) as response:
            self.assertEqual(response.status, 202)
        wait_for(lambda: service.snapshot()['state'] == 'running')
        service.imaging.submit(7, 1, payload())
        wait_for(lambda: service.snapshot()['image_available'])
        with urlopen(base+'/api/image?after=0') as response:
            seq = response.headers['X-Image-Sequence']
            self.assertTrue(response.read().startswith(b'\x89PNG'))
        with urlopen(base+'/api/image?after='+seq) as response: self.assertEqual(response.status, 204)
        with urlopen(Request(base+'/api/viewer/stop', b'{}', headers={'Content-Type': 'application/json'})) as response:
            self.assertEqual(response.status, 202)
        wait_for(lambda: service.snapshot()['state'] == 'idle')


class ReceiverHookTests(unittest.TestCase):
    def test_tcp_ack_precedes_slow_callback(self):
        callback, release = threading.Event(), threading.Event()
        class Sink:
            def submit(self, event, complete=None): pass
        args = v.SimpleNamespace(length=16, test_pattern=False, source_ip='127.0.0.1', seconds=0,
                                 idle_timeout=1, frame_timeout=1, frames=1, output_dir=None)
        def slow(*args): callback.set();release.wait(2)
        receiver = tcp.TCPReceiver(args, Sink(), on_frame=slow)
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0));listener.listen();listener.settimeout(.1)
            thread = threading.Thread(target=receiver.serve, args=(listener,), daemon=True);thread.start()
            try:
                with socket.create_connection(listener.getsockname()) as conn:
                    conn.settimeout(.5);data = bytes(16);conn.sendall(header(data)+data)
                    self.assertTrue(callback.wait(1))
                    self.assertEqual(tcp.ACK.unpack(exact(conn, 20))[3], 0)
                    self.assertTrue(thread.is_alive())
            finally:
                release.set();thread.join(1)


if __name__ == '__main__':
    unittest.main()
