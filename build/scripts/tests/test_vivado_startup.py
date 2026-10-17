"""Self-checks for the precise, backed-up per-user Vivado startup repair."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / 'build/scripts/repair_vivado_startup.ps1'
TEMPLATE = ROOT / 'build/scripts/Vivado_init_2026_1.tcl'
SHELL = shutil.which('pwsh') or shutil.which('powershell')


@unittest.skipUnless(SHELL, 'PowerShell is required')
class StartupRepairTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='vivado-startup-test-')
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.config = self.base / 'settings'
        self.manifest = self.config / 'tclapp/manifest.tcl'
        self.init = self.config / '2026.1/Vivado_init.tcl'
        self.manifest.parent.mkdir(parents=True)

    def repair(self, backup='backup'):
        return subprocess.run([SHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(SCRIPT),
                               '-ConfigRoot', str(self.config),
                               '-BackupDirectory', str(self.base / backup)],
                              capture_output=True, text=True, encoding='utf-8', errors='replace')

    def test_only_exact_obsolete_entry_removed_and_original_backed_up(self):
        original = ('# keep aldec::activehdl mentioned in a comment\n'
                    '::tclapp::load_app -namespace {aldec::activehdl} aldec::activehdl\n'
                    '::tclapp::load_apps -namespace {xilinx::designutils} xilinx::designutils\n'
                    'set harmless {aldec::activehdl}\n')
        self.manifest.write_text(original, encoding='utf-8')
        result = self.repair()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.base/'backup/manifest.tcl').read_text(), original)
        self.assertEqual(self.manifest.read_text(), original.replace(original.splitlines(True)[1], ''))
        self.assertEqual(self.init.read_bytes(), TEMPLATE.read_bytes())

    def test_missing_manifest_is_not_created(self):
        result = self.repair()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.manifest.exists())
        self.assertEqual(self.init.read_bytes(), TEMPLATE.read_bytes())

    def test_repeated_repair_is_idempotent(self):
        self.manifest.write_text('::tclapp::load_apps -namespace aldec::activehdl aldec::activehdl\n')
        self.assertEqual(self.repair().returncode, 0)
        once = self.manifest.read_bytes()
        result = self.repair('second-backup')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.manifest.read_bytes(), once)
        self.assertEqual(self.init.read_bytes(), TEMPLATE.read_bytes())

    def test_unknown_personal_init_is_never_overwritten(self):
        self.init.parent.mkdir(parents=True)
        self.init.write_text('puts {personal init}\n')
        self.manifest.write_text('::tclapp::load_app -namespace {aldec::activehdl} aldec::activehdl\n')
        original = self.manifest.read_bytes()
        result = self.repair()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.init.read_text(), 'puts {personal init}\n')
        self.assertEqual(self.manifest.read_bytes(), original)
        self.assertFalse((self.base/'backup').exists())

    def test_new_guard_has_no_shared_file_writes_or_message_suppression(self):
        text = TEMPLATE.read_text()
        self.assertIn('set_param tclapp.enableGitAccess 0', text)
        self.assertNotIn('[open ', text)
        self.assertNotIn('set_msg_config', text)
        self.assertNotIn('file delete', text)


if __name__ == '__main__':
    unittest.main()
