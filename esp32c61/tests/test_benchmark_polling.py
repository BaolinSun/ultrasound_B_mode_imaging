import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import urllib.error
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pc_tools'))
from sdio_benchmark import sample_status
import sdio_benchmark as benchmark


class StatusSamplingTests(unittest.TestCase):
    def test_success_only_issues_one_get(self):
        calls = []
        def api(path):
            calls.append(path)
            return {'busy': True}
        errors = []
        self.assertEqual(sample_status(api, errors, 1), {'busy': True})
        self.assertEqual(calls, ['/api/status'])
        self.assertEqual(errors, [])

    def test_server_failure_is_recorded_without_retry(self):
        calls = []
        def api(path):
            calls.append(path)
            raise urllib.error.HTTPError('http://device/api/status', 500, 'Error', {},
                                         io.BytesIO(b'JSON allocation failed'))
        errors = []
        self.assertIsNone(sample_status(api, errors, 2))
        self.assertEqual(calls, ['/api/status'])
        self.assertEqual(errors, [{'t': 2, 'error': 'HTTP 500: JSON allocation failed'}])

    def test_client_error_not_hidden(self):
        def api(path):
            raise urllib.error.HTTPError('http://device/api/status', 404, 'Error', {}, None)
        with self.assertRaises(urllib.error.HTTPError):
            sample_status(api, [], 0)

    def test_timeout_is_recorded(self):
        def api(path):
            raise TimeoutError('read timed out')
        errors = []
        self.assertIsNone(sample_status(api, errors, 3))
        self.assertEqual(errors[0]['error'], 'read timed out')

    def test_malformed_json_is_not_a_successful_sample(self):
        def api(path):
            raise ValueError('malformed response')
        with self.assertRaises(ValueError):
            sample_status(api, [], 0)

    def run_polling_case(self, failures):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'test.jsonl'
            clock = [0.0]
            running = [False]
            reads = [0]
            writes = []
            class Input:
                def write(self, data):
                    if data == b'STOP\n':
                        process.returncode = 0
                def flush(self):
                    pass
            class Process:
                returncode = None
                stdin = Input()
                def poll(self):
                    return self.returncode
                def wait(self, timeout):
                    return self.returncode
            process = Process()
            def request(req, timeout):
                if req.get_method() == 'POST':
                    writes.append(req.full_url)
                    running[0] = req.full_url.endswith('/start')
                elif running[0]:
                    reads[0] += 1
                    if reads[0] <= failures:
                        raise urllib.error.HTTPError(req.full_url, 500, 'Error', {},
                                                     io.BytesIO(b'JSON allocation failed'))
                return io.BytesIO(json.dumps({'busy': running[0], 'sdio': {
                    'running': running[0], 'host_sdio': True, 'free_slots': 2}}).encode())
            def launch(command, stdout, stderr, creationflags, stdin):
                stdout.write('Listening UBSD fixture\n')
                stdout.flush()
                log.write_text(json.dumps({'event': 'summary', 'passed': True})+'\n')
                return process
            def sleep(seconds):
                clock[0] += seconds
            with patch.object(sys, 'argv', ['benchmark', '--device-ip', '127.0.0.1',
                                           '--seconds', '3', '--log', str(log)]), \
                 patch.object(benchmark.urllib.request, 'urlopen', side_effect=request), \
                 patch.object(benchmark.subprocess, 'Popen', side_effect=launch), \
                 patch.object(benchmark.time, 'monotonic', side_effect=lambda: clock[0]), \
                 patch.object(benchmark.time, 'sleep', side_effect=sleep), \
                 patch.object(benchmark, 'print', lambda *a, **k: None, create=True):
                result = benchmark.main()
            state = json.loads(log.with_suffix('.device.json').read_text())
            self.assertEqual(len(writes), 2)
            self.assertTrue(writes[0].endswith('/start'))
            self.assertTrue(writes[1].endswith('/stop'))
            self.assertEqual(len(state['status_poll_errors']), failures)
            return result, state

    def test_transient_failure_preserves_run_without_reposting_start(self):
        result, state = self.run_polling_case(1)
        self.assertEqual(result, 0)
        self.assertFalse(state['unexpected_stop'])
        self.assertIsNone(state['monitor_error'])

    def test_three_failures_stop_and_fail_even_when_receiver_passed(self):
        result, state = self.run_polling_case(3)
        self.assertEqual(result, 1)
        self.assertTrue(state['unexpected_stop'])
        self.assertIn('Three consecutive', state['monitor_error'])


if __name__ == '__main__':
    unittest.main()
