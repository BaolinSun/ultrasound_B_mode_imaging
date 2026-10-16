"""Keep runtime artifacts ignored without hiding project inputs."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class IgnoreTests(unittest.TestCase):
    def ignored(self, path):
        result = subprocess.run(['git', 'check-ignore', '--no-index', '-q', path],
                                cwd=ROOT, capture_output=True)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        return result.returncode == 0

    def test_runtime_reports_and_temporary_files_are_ignored(self):
        for suffix in ('logs/run.stdout.txt', 'logs/run.device.json', 'capture.json', 'run.jsonl', 'run.log',
                       'pc_tools/__pycache__/module.pyc', '.pytest_cache/state', '.venv/pyvenv.cfg',
                       'build/CMakeCache.txt', 'build-throughput-fast-socket/compile_commands.json',
                       'tests/output/native.exe', 'pc_tools/scratch.tmp', 'test.gcda', 'test.o', 'test.d', 'edit.py~'):
            with self.subTest(suffix=suffix):
                self.assertTrue(self.ignored('esp32c61/'+suffix))

    def test_source_and_configuration_inputs_remain_visible(self):
        for suffix in ('.vscode/settings.json', 'main/app_main.c', 'pc_tools/bmode_web_viewer.py',
                       'pc_tools/bmode_assets/graymap_lut.txt', 'dependencies.lock', 'sdkconfig.defaults',
                       'sdkconfig.throughput.defaults', 'docs/WIFI_IMAGE_VIEWER.md'):
            with self.subTest(suffix=suffix):
                self.assertFalse(self.ignored('esp32c61/'+suffix))

    def test_rules_do_not_expand_to_other_projects(self):
        self.assertFalse(self.ignored('common/example.json'))
        self.assertFalse(self.ignored('build/src/ps/example.json'))


if __name__ == '__main__':
    unittest.main()
