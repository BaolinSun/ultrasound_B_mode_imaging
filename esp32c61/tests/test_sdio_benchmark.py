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
    def run_fixture(self, folder, fault=None):
        log = pathlib.Path(folder)/'run.jsonl'
        calls = []
        status = {'busy': fault == 'busy', 'sdio': {'running': False, 'host_sdio': True, 'free_slots': 2}}
        def request(req, timeout):
            calls.append((req.full_url, req.get_method()))
            if req.full_url.endswith('/api/sdio/start') and fault == 'start-timeout':
                raise TimeoutError('fixture timeout')
            return io.BytesIO(json.dumps(status).encode())
        def process(command, stdout, stderr, creationflags):
            stdout.write('Listening UBSD fixture\n');stdout.flush()
            log.write_text(json.dumps({'event': 'summary', 'passed': True, 'valid_frames': 2})+'\n')
            class Done:
                returncode = 0
                def poll(self): return 0
            return Done()
        with patch.object(sys, 'argv', ['benchmark', '--device-ip', '127.0.0.1', '--seconds', '1', '--log', str(log)]), \
             patch.object(b.urllib.request, 'urlopen', side_effect=request), \
             patch.object(b.subprocess, 'Popen', side_effect=process) as launch, \
             patch.object(b, 'print', lambda *a, **k: None, create=True):
            if fault in ('busy', 'existing'):
                with self.assertRaises(SystemExit): b.main()
                launch.assert_not_called()
            elif fault == 'start-timeout':
                with self.assertRaises(TimeoutError): b.main()
            else:
                self.assertEqual(b.main(), 0)
        return calls, log

    def test_listens_then_starts_and_always_stops(self):
        with tempfile.TemporaryDirectory() as folder:
            calls, log = self.run_fixture(folder)
            self.assertEqual([method for _, method in calls], ['GET', 'POST', 'POST'])
            self.assertTrue(calls[-1][0].endswith('/api/sdio/stop'))
            self.assertFalse(json.loads(log.with_suffix('.device.json').read_text())['unexpected_stop'])

    def test_start_timeout_still_attempts_stop(self):
        with tempfile.TemporaryDirectory() as folder:
            calls, log = self.run_fixture(folder, 'start-timeout')
            self.assertTrue(calls[-1][0].endswith('/api/sdio/stop'))
            self.assertTrue(log.with_suffix('.device.json').exists())

    def test_busy_device_has_no_control_writes(self):
        with tempfile.TemporaryDirectory() as folder:
            calls, _ = self.run_fixture(folder, 'busy')
            self.assertEqual([method for _, method in calls], ['GET'])

    def test_existing_log_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            log = pathlib.Path(folder)/'run.jsonl';log.write_text('keep')
            calls, _ = self.run_fixture(folder, 'existing')
            self.assertEqual(log.read_text(), 'keep')
            self.assertEqual([method for _, method in calls], ['GET'])


if __name__ == '__main__': unittest.main()
