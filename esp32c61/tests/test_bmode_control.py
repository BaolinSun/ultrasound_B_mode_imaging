"""Local fake-device tests; no board, real scanning or HV is used."""
import copy
import json
from pathlib import Path
import sys
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pc_tools'))
import bmode_viewer_service as v
import bmode_web_viewer as web
from bmode_control import FocusLibrary, ViewerControl, fields
from zynq_wifi_control import ControlFailure
from test_bmode_viewer import FakeDevice, free_port
from test_bmode_imaging import wait_for


class ControlTests(unittest.TestCase):
    def service(self):
        device = FakeDevice(free_port())
        service = v.ViewerService('127.0.0.1', device.config['target_port'], '127.0.0.1', api=device)
        self.addCleanup(service.close)
        return service, device

    def test_read_only_startup_and_all_sixteen_local_focus_profiles(self):
        service, device = self.service()
        service.snapshot()
        catalog = service.focus_library.catalog()
        self.assertEqual(device.calls, [])
        self.assertEqual(len({p['sha256'] for p in catalog['profiles']}), 16)
        for depth in range(20, 171, 10):
            commands = service.focus_library.commands(depth)
            self.assertEqual(len(commands), 2048)
            self.assertTrue(all(c.startswith('BRAM WR delay_profile ') for c in commands))
            self.assertTrue(commands[-1].startswith('BRAM WR delay_profile 16376 8 '))
        for invalid in (True, 0, 25, 180, 80.0, '80'):
            with self.assertRaises(ValueError): service.set_focus(invalid)
        self.assertEqual(device.calls, [])

    def test_start_listener_before_enable_freeze_and_resume_keep_same_upload(self):
        service, device = self.service()
        def spy(path, body=None):
            if body and 'HV ENABLE' in body.get('commands', []):
                self.assertTrue(service.transport.thread.is_alive())
                self.assertEqual(service.transport.sock.getsockname()[1], device.config['target_port'])
            return device(path, body)
        service.api = spy
        service.start()
        wait_for(lambda: service.snapshot()['state'] == 'running')
        self.assertEqual((device.ps['run'], device.ps['hv']), (1, 1))
        self.assertLess(device.commands.index('HV ENABLE'), device.commands.index('BEAM START'))
        transport = service.transport
        service.set_frozen(True)
        wait_for(lambda: service.snapshot()['frozen'])
        self.assertEqual((device.ps['run'], device.ps['hv']), (1, 0))
        self.assertIs(service.transport, transport)
        self.assertTrue(device.status['sdio']['running'])
        service.set_frozen(False)
        wait_for(lambda: not service.snapshot()['frozen'] and not service.snapshot()['control_action'])
        self.assertEqual((device.ps['run'], device.ps['hv']), (1, 1))
        self.assertEqual(sum(path == '/api/sdio/start' for path, body in device.calls), 1)
        service.stop()
        wait_for(lambda: service.snapshot()['state'] == 'idle')
        self.assertEqual((device.ps['run'], device.ps['hv']), (0, 0))

    def test_idle_stop_closes_external_scan_and_hv_without_taking_foreign_upload(self):
        service, device = self.service()
        device.ps.update(run=1, hv=1)
        device.status['sdio']['running'] = True
        service.stop()
        wait_for(lambda: service.snapshot()['state'] == 'idle')
        self.assertEqual((device.ps['run'], device.ps['hv']), (0, 0))
        self.assertTrue(device.status['sdio']['running'])
        self.assertNotIn(('/api/sdio/stop', {}), device.calls)

    def test_unknown_enable_and_failed_scan_are_not_replayed_and_end_off(self):
        for mode in ('lost_enable', 'bad_scan'):
            service, device = self.service()
            def fail(path, body=None):
                result = device(path, body)
                if body and body.get('commands') == ['HV ENABLE'] and mode == 'lost_enable':
                    raise ControlFailure('lost reply', unknown=True)
                if body and body.get('commands') == ['BEAM START'] and mode == 'bad_scan':
                    device.jobs[result['job_id']].update(state='failed', error='scan failed')
                return result
            service.api = fail
            service.start()
            wait_for(lambda: service.snapshot()['state'] == 'error')
            self.assertEqual((device.ps['run'], device.ps['hv']), (0, 0))
            self.assertEqual(device.commands.count('HV ENABLE'), 1)
            self.assertNotIn(('/api/sdio/start', {'frames': 0}), device.calls)

    def test_priority_stop_during_start_cannot_leave_late_enable_active(self):
        service, device = self.service()
        entered, release, off = threading.Event(), threading.Event(), threading.Event()
        def slow(path, body=None):
            if body and body.get('commands') == ['HV ENABLE']:
                entered.set(); release.wait(2)
            result = device(path, body)
            if body and body.get('commands') == ['HV DISABLE']: off.set()
            return result
        service.api = slow
        try:
            service.start(); self.assertTrue(entered.wait(2))
            service.stop(); self.assertTrue(off.wait(2))
        finally:
            release.set()
        wait_for(lambda: service.snapshot()['state'] == 'idle')
        self.assertEqual((device.ps['run'], device.ps['hv']), (0, 0))
        self.assertNotIn('BEAM START', device.commands)

    def test_focus_waits_for_upload_callback_and_slots_then_stays_off(self):
        service, device = self.service()
        entered = threading.Event()
        def slow_stop(path, body=None):
            result = device(path, body)
            if path == '/api/sdio/stop' and body is not None:
                device.status['sdio'].update(stopping=True, free_slots=1); entered.set()
            return result
        service.api = slow_stop
        service.start(); wait_for(lambda: service.snapshot()['state'] == 'running')
        service.set_focus(90); self.assertTrue(entered.wait(2))
        time.sleep(.12)
        self.assertFalse(any(c.startswith('CFG BEGIN ') for c in device.commands))
        device.status['sdio'].update(stopping=False, free_slots=2)
        wait_for(lambda: service.snapshot()['state'] == 'idle', 6)
        self.assertEqual(service.snapshot()['focus']['last_applied_mm'], 90)
        self.assertEqual(service.snapshot()['focus']['completed'], 2048)
        commands = [c for c in device.commands if c.startswith('BRAM WR ')]
        self.assertEqual(commands, service.focus_library.commands(90))
        end = next(i for i, c in enumerate(device.commands) if c.startswith('CFG BEGIN '))
        self.assertNotIn('HV ENABLE', device.commands[end:])
        self.assertEqual((device.ps['run'], device.ps['hv']), (0, 0))

    def test_focus_cancellation_aborts_own_transaction_without_restart(self):
        service, device = self.service()
        entered, release = threading.Event(), threading.Event()
        def pause(path, body=None):
            result = device(path, body)
            if body and body.get('commands', [''])[0].startswith('BRAM WR '):
                entered.set(); release.wait(2)
            return result
        service.api = pause
        try:
            service.set_focus(120); self.assertTrue(entered.wait(2)); service.stop()
        finally:
            release.set()
        wait_for(lambda: service.snapshot()['state'] == 'idle', 5)
        self.assertIn('CFG ABORT', device.commands)
        self.assertEqual(sum(c.startswith('BRAM WR ') for c in device.commands), 8)
        self.assertIsNone(service.snapshot()['focus']['last_applied_mm'])
        self.assertNotIn('HV ENABLE', device.commands)
        self.assertEqual((device.ps['run'], device.ps['hv'], device.ps['cfg']), (0, 0, 0))

    def test_focus_rejects_foreign_config_and_invalid_status_or_ack(self):
        service, device = self.service()
        device.status['zynq_control']['config_owned'] = True
        service.set_focus(80); wait_for(lambda: service.snapshot()['state'] == 'error')
        self.assertFalse(device.commands)
        for text in ('@STATUS v=1 run=1 run=0', '@ACK 0 OK', '@STATUS v=2 run=1'):
            with self.assertRaises(RuntimeError): fields(text)
        control = ViewerControl('127.0.0.1', device)
        original = device.__call__
        def wrong(path, body=None):
            result=original(path, body)
            if body: device.jobs[result['job_id']]['results'][0]['response']='@ACK 0 BEAM_START'
            return result
        control.client.api = wrong
        with self.assertRaises(ControlFailure): control.batch(['HV ENABLE'])

    def test_failed_drain_cannot_write_focus_or_auto_restart(self):
        service, device = self.service()
        service.start(); wait_for(lambda: service.snapshot()['state'] == 'running')
        device.fail_stop=True
        service.set_focus(80); wait_for(lambda: service.snapshot()['state'] == 'error')
        self.assertFalse(any(c.startswith(('CFG BEGIN ', 'BRAM WR ')) for c in device.commands))
        self.assertEqual((device.ps['run'],device.ps['hv']),(0,0))

    def test_failed_resume_shuts_down_and_does_not_retry_enable(self):
        service, device = self.service()
        service.start(); wait_for(lambda: service.snapshot()['state'] == 'running')
        service.set_frozen(True); wait_for(lambda: service.snapshot()['frozen'])
        def lost(path, body=None):
            result=device(path,body)
            if body and body.get('commands')==['HV ENABLE']:
                raise ControlFailure('resume ACK lost',unknown=True)
            return result
        service.control.client.api=lost
        service.set_frozen(False); wait_for(lambda: service.snapshot()['state']=='error')
        self.assertEqual(device.commands.count('HV ENABLE'),2)
        self.assertEqual((device.ps['run'],device.ps['hv']),(0,0))

    def test_cancel_before_start_cannot_hide_a_failed_priority_shutdown(self):
        service, device = self.service()
        entered, release = threading.Event(), threading.Event()
        first=True
        def fail_off(path, body=None):
            nonlocal first
            if path=='/api/status' and body is None and first:
                first=False;entered.set();release.wait(2)
            if body and body.get('commands')==['HV DISABLE']:
                raise ControlFailure('shutdown link lost',unknown=True)
            return device(path,body)
        service.api=fail_off
        try:
            service.start();self.assertTrue(entered.wait(2));service.stop()
        finally:
            release.set()
        wait_for(lambda:service.snapshot()['state']=='error')
        self.assertIn('关断未确认',service.snapshot()['error'])
        self.assertNotIn('HV ENABLE',device.commands)

    def test_http_controls_require_exact_intent_and_focus_schema(self):
        service, device = self.service()
        server = web.make_server(service, ('127.0.0.1', 0))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close); self.addCleanup(server.shutdown)
        base='http://127.0.0.1:'+str(server.server_port)
        with urlopen(base+'/api/viewer/focus') as response:
            self.assertEqual(len(json.load(response)['profiles']),16)
        self.assertFalse(device.calls)
        for path, body in [('/start',{}),('/resume',{}),('/start',{'confirm_hv':1}),('/focus',{'focus_mm':80,'commands':['HV ENABLE']}),('/focus',{'focus_mm':25}),('/focus',{'focus_mm':True})]:
            with self.assertRaises(HTTPError):
                urlopen(Request(base+'/api/viewer'+path,json.dumps(body).encode(),headers={'Content-Type':'application/json'}))
        self.assertFalse(device.calls)
        with self.assertRaises(HTTPError) as error:
            urlopen(Request(base+'/api/viewer/focus',b'{"focus_mm":80}',headers={'Content-Type':'application/json','Origin':'http://evil.test'}))
        self.assertEqual(error.exception.code,403)


if __name__=='__main__': unittest.main()
