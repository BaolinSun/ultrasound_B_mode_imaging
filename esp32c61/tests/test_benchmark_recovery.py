import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pc_tools'))
import sdio_benchmark as b


class RecoveryTests(unittest.TestCase):
    def run_case(self, stop_fault=False):
        with tempfile.TemporaryDirectory() as folder:
            log=Path(folder)/'run.jsonl'
            clock=[0.0];running=[False];calls=[];launches=[];session=[1];pending=[0]
            class Input:
                def write(self,data):
                    if data==b'STOP\n':proc.returncode=0
                def flush(self):pass
            class Process:
                returncode=None;stdin=Input()
                def poll(self):return self.returncode
                def wait(self,timeout):return self.returncode
            proc=Process()
            def request(req,timeout):
                calls.append((clock[0],req.full_url,req.get_method()))
                if req.full_url.endswith('/start'):running[0]=True
                if req.full_url.endswith('/stop'):
                    if stop_fault:raise TimeoutError('unknown stop outcome')
                    if running[0]:pending[0]=2
                    running[0]=False
                if req.get_method()=='GET' and pending[0]:
                    pending[0]-=1
                    if not pending[0]:session[0]+=1
                return io.BytesIO(json.dumps({'busy':False,'sdio':{'running':running[0],
                    'host_sdio':True,'free_slots':2,'source_flags':1,'session':session[0]}}).encode())
            def launch(command,stdout,stderr,creationflags,stdin):
                launches.append(command)
                stdout.write('Listening UBSD fixture\n');stdout.flush()
                log.write_text(json.dumps({'event':'summary','passed':True})+'\n')
                return proc
            def sleep(seconds):clock[0]+=seconds
            with patch.object(sys,'argv',['bench','--device-ip','127.0.0.1','--seconds','20',
                    '--stall-timeout','1','--recover-stalls','--max-recoveries','2','--log',str(log)]), \
                 patch.object(b.urllib.request,'urlopen',side_effect=request), \
                 patch.object(b.subprocess,'Popen',side_effect=launch), \
                 patch.object(b.time,'monotonic',side_effect=lambda:clock[0]), \
                 patch.object(b.time,'sleep',side_effect=sleep), \
                 patch.object(b,'print',lambda *a,**k:None,create=True):
                result=b.main()
            return result,json.loads(log.with_suffix('.device.json').read_text()),calls,launches,clock[0]

    def test_recovers_without_restarting_clock_or_receiver_and_bounds_writes(self):
        code,data,calls,launches,elapsed=self.run_case()
        self.assertEqual(code,0)
        self.assertGreaterEqual(elapsed,20)
        self.assertLess(elapsed,21)
        self.assertEqual(len(launches),1)
        self.assertIn('--idle-timeout',launches[0])
        self.assertEqual(len(data['recoveries']),2)
        self.assertTrue(all(x['restart_accepted'] for x in data['recoveries']))
        self.assertTrue(data['recovery_limit_reached'])
        self.assertFalse(data['unexpected_stop'])
        posts=[url for _,url,method in calls if method=='POST']
        self.assertEqual(sum(x.endswith('/start') for x in posts),3)
        self.assertEqual(sum(x.endswith('/stop') for x in posts),3)
        self.assertTrue(all('/api/sdio/' in x for x in posts))

    def test_unknown_stop_never_triggers_a_new_start(self):
        code,data,calls,launches,elapsed=self.run_case(stop_fault=True)
        self.assertEqual(code,1)
        self.assertTrue(data['unexpected_stop'])
        self.assertFalse(data['recoveries'][0]['restart_accepted'])
        self.assertEqual(sum(url.endswith('/start') for _,url,method in calls if method=='POST'),1)


if __name__=='__main__':unittest.main()
