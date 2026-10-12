import io
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pc_tools'))
import sdio_benchmark as b


class BenchmarkTests(unittest.TestCase):
    def test_stream_stall_fails_and_stops_device_even_if_receiver_reports_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            log=pathlib.Path(folder)/'stall.jsonl';clock=[0.0];running=[False];calls=[]
            def request(req,timeout):
                calls.append(req.full_url)
                if req.full_url.endswith('/start'):running[0]=True
                if req.full_url.endswith('/stop'):running[0]=False
                return io.BytesIO(json.dumps({'busy':False,'sdio':{'running':running[0],
                    'host_sdio':True,'free_slots':2}}).encode())
            class Input:
                def write(self,data):
                    if data==b'STOP\n':process.returncode=0
                def flush(self):pass
            class Process:
                returncode=None;stdin=Input()
                def poll(self):return self.returncode
                def wait(self,timeout):return self.returncode
            process=Process()
            def launch(command,stdout,stderr,creationflags,stdin):
                stdout.write('Listening UBSD fixture\n');stdout.flush()
                log.write_text(json.dumps({'event':'summary','passed':True})+'\n')
                return process
            def sleep(seconds):clock[0]+=seconds
            with patch.object(sys,'argv',['benchmark','--device-ip','127.0.0.1','--seconds','60','--log',str(log)]), \
                 patch.object(b.urllib.request,'urlopen',side_effect=request), \
                 patch.object(b.subprocess,'Popen',side_effect=launch), \
                 patch.object(b.time,'monotonic',side_effect=lambda:clock[0]), \
                 patch.object(b.time,'sleep',side_effect=sleep), \
                 patch.object(b,'print',lambda *a,**k:None,create=True):
                self.assertEqual(b.main(),1)
            result=json.loads(log.with_suffix('.device.json').read_text())
            self.assertTrue(result['upstream_stall'] and result['unexpected_stop'])
            self.assertTrue(any(url.endswith('/stop') for url in calls))
            self.assertLess(clock[0],10)
    def test_stall_timer_resets_for_backpressure_or_active_sender(self):
        healthy={'busy':False,'sdio':{'running':True,'host_sdio':True,'free_slots':2}}
        since,stalled=b.stalled_since(healthy,1,None,5)
        self.assertEqual(since,1);self.assertFalse(stalled)
        self.assertEqual(b.stalled_since(healthy,6,since,5),(1,True))
        for key,value in (('free_slots',0),('running',False),('host_sdio',False),('ready',False)):
            s={'busy':False,'sdio':dict(healthy['sdio'],**{key:value})}
            self.assertEqual(b.stalled_since(s,6,since,5),(None,False))
        self.assertEqual(b.stalled_since(dict(healthy,busy=True),6,since,5),(None,False))
        self.assertEqual(b.stalled_since(healthy,9,None,5),(9,False))
    def test_phase_metrics_deduplicate_and_exclude_history_cancelled_and_other_sessions(self):
        def metric(frame,value=100,result='received_verified',session=7):
            return {'status':{'tcp_metric_frame':frame,'tcp_metric_session':session,
                'tcp_metric_result':result,'tcp_payload_us':value,'sdio':{'session':7}}}
        samples=[metric(99),metric(1),metric(1),metric(2,200),metric(3,result='cancelled'),metric(4,session=8)]
        result=b.tcp_phase_summary(samples)
        self.assertEqual(result['sampled_complete_frames'],2)
        self.assertEqual(result['mean']['tcp_payload_us'],150)
        self.assertEqual(b.tcp_phase_summary([metric(99)])['mean'],{})
    def run_fixture(self, folder, fault=None):
        log = pathlib.Path(folder)/'run.jsonl'
        calls = []
        clock=[0.0]
        status = {'busy': fault == 'busy', 'sdio': {'running': False, 'host_sdio': True, 'free_slots': 2}}
        def request(req, timeout):
            calls.append((req.full_url, req.get_method()))
            if req.full_url.endswith('/api/sdio/start') and fault == 'start-timeout':
                raise TimeoutError('fixture timeout')
            return io.BytesIO(json.dumps(status).encode())
        def process(command, stdout, stderr, creationflags, stdin):
            stdout.write('Listening UBSD fixture\n');stdout.flush()
            log.write_text(json.dumps({'event': 'summary', 'passed': True, 'valid_frames': 2})+'\n')
            class Done:
                returncode = 0
                def poll(self):
                    if fault!='early-exit':clock[0]+=2
                    return 0
            return Done()
        with patch.object(sys, 'argv', ['benchmark', '--device-ip', '127.0.0.1', '--seconds', '1', '--log', str(log)]), \
             patch.object(b.urllib.request, 'urlopen', side_effect=request), \
             patch.object(b.subprocess, 'Popen', side_effect=process) as launch, \
             patch.object(b.time,'monotonic',side_effect=lambda:clock[0]), \
             patch.object(b, 'print', lambda *a, **k: None, create=True):
            if fault in ('busy', 'existing'):
                with self.assertRaises(SystemExit): b.main()
                launch.assert_not_called()
            elif fault == 'start-timeout':
                with self.assertRaises(TimeoutError): b.main()
            else:
                self.assertEqual(b.main(), 1 if fault=='early-exit' else 0)
        return calls, log

    def test_listens_then_starts_and_always_stops(self):
        with tempfile.TemporaryDirectory() as folder:
            calls, log = self.run_fixture(folder)
            self.assertEqual([method for _, method in calls], ['GET', 'POST', 'POST', 'GET', 'GET'])
            self.assertTrue(calls[-3][0].endswith('/api/sdio/stop'))
            self.assertFalse(json.loads(log.with_suffix('.device.json').read_text())['unexpected_stop'])

    def test_start_timeout_still_attempts_stop(self):
        with tempfile.TemporaryDirectory() as folder:
            calls, log = self.run_fixture(folder, 'start-timeout')
            self.assertTrue(calls[-3][0].endswith('/api/sdio/stop'))
            self.assertTrue(log.with_suffix('.device.json').exists())

    def test_busy_device_has_no_control_writes(self):
        with tempfile.TemporaryDirectory() as folder:
            calls, _ = self.run_fixture(folder, 'busy')
            self.assertEqual([method for _, method in calls], ['GET'])

    def test_receiver_early_idle_exit_is_not_valid_continuous_run(self):
        with tempfile.TemporaryDirectory() as folder:
            _,log=self.run_fixture(folder,'early-exit')
            self.assertTrue(json.loads(log.with_suffix('.device.json').read_text())['unexpected_stop'])

    def test_existing_log_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            log = pathlib.Path(folder)/'run.jsonl';log.write_text('keep')
            calls, _ = self.run_fixture(folder, 'existing')
            self.assertEqual(log.read_text(), 'keep')
            self.assertEqual([method for _, method in calls], ['GET'])

    def test_tcp_auto_selection_and_device_stop_before_socket_close(self):
        with tempfile.TemporaryDirectory() as folder:
            log=pathlib.Path(folder)/'tcp.jsonl';order=[];clock=[0.0]
            state={'busy':False,'sdio_transport':'tcp','sdio':{'host_sdio':True,'free_slots':2,'running':False}}
            def request(req,timeout):
                if req.full_url.endswith('/start'):state['sdio']['running']=True;order.append('start')
                if req.full_url.endswith('/stop'):state['sdio']['running']=False;order.append('device-stop')
                return io.BytesIO(json.dumps(state).encode())
            class Control:
                def write(self,data):order.append(data.decode().strip())
                def flush(self):pass
            class Running:
                stdin=Control();returncode=None
                def poll(self):return self.returncode
                def wait(self,timeout):self.returncode=0;return 0
            def process(command,stdout,**kwargs):
                self.assertIn('sdio_tcp_receiver.py',command[1]);self.assertIn('--managed',command)
                stdout.write('Listening UBST TCP fixture\n');stdout.flush()
                log.write_text('{"event":"summary","passed":true}\n')
                order.append('listening');return Running()
            with patch.object(sys,'argv',['benchmark','--device-ip','127.0.0.1','--seconds','1','--log',str(log)]), \
                 patch.object(b.urllib.request,'urlopen',side_effect=request), \
                 patch.object(b.subprocess,'Popen',side_effect=process), \
                 patch.object(b.time,'monotonic',side_effect=lambda:clock[0]), \
                 patch.object(b.time,'sleep',side_effect=lambda s:clock.__setitem__(0,clock[0]+s)), \
                 patch.object(b,'print',lambda *a,**kw:None,create=True):
                self.assertEqual(b.main(),0)
            self.assertEqual(order,['listening','start','QUIESCE','device-stop','STOP'])


if __name__ == '__main__': unittest.main()
