"""Exercise PowerShell entry points with a fake IDF, never touch a board."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
IDF_ENV = Path(r"C:\Espressif\tools\python\v6.1\venv")


def ps_quote(value):
    return "'" + str(value).replace("'", "''") + "'"


@unittest.skipUnless(shutil.which("powershell.exe") and
                     (IDF_ENV / "Scripts/python.exe").exists(),
                     "Requires Windows PowerShell and installed IDF Python")
class BuildActionsTests(unittest.TestCase):
    def run_action(self, action, tail="", version="ESP-IDF v6.1", experiment=None, tamper=False, legacy_partition=False):
        with tempfile.TemporaryDirectory(prefix="ubmi-build-actions-") as folder:
            base = Path(folder)
            project = base / "project with spaces"
            project.mkdir()
            shutil.copyfile(ROOT / "build.ps1", project / "build.ps1")
            (project / "main").mkdir()
            (project / "main/Kconfig.projbuild").write_text("# fixture\n")
            (project / "build/config").mkdir(parents=True)
            (project / "sdkconfig").write_text('CONFIG_IDF_TARGET="esp32c61"\n')
            (project / "build/config/sdkconfig.h").write_text("// fixture\n")
            for path in ROOT.glob('sdkconfig.experiment-*.defaults'):
                shutil.copyfile(path,project/path.name)
            selected=experiment or 'combined'
            direct=selected in ('direct','combined','psram80')
            core=selected in ('core','combined','psram80')
            config='CONFIG_IDF_TARGET="esp32c61"\n'+f'CONFIG_SPIRAM_SPEED={80 if experiment=="psram80" else 40}\n'
            for key,on in (('PSRAM_TCP_DIRECT',direct),('LWIP_TCPIP_CORE_LOCKING',core),
                           ('LWIP_TCPIP_CORE_LOCKING_INPUT',core),('LWIP_CHECK_THREAD_SAFETY',core)):
                config+=f'CONFIG_{key}=y\n' if on else f'# CONFIG_{key} is not set\n'
            tampered=None
            if tamper:
                output=project/f'build-throughput-fast-socket-exp-{experiment}'
                (output/'config').mkdir(parents=True)
                tampered=config.replace('CONFIG_PSRAM_TCP_DIRECT=y','# CONFIG_PSRAM_TCP_DIRECT is not set')
                (output/'sdkconfig').write_text(tampered)
                (output/'config/sdkconfig.h').write_text('// fixture\n')
            if legacy_partition:
                output=project/f'build-throughput-fast-socket-exp-{selected}'
                (output/'config').mkdir(parents=True)
                (output/'sdkconfig').write_text(config+'CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y\nCONFIG_PARTITION_TABLE_FILENAME="partitions_singleapp_large.csv"\n')
                (output/'config/sdkconfig.h').write_text('// fixture\n')
            tool = base / "fake IDF/tools"
            tool.mkdir(parents=True)
            calls = base / "calls.jsonl"
            (tool / "idf.py").write_text(
                "import json, sys\nfrom pathlib import Path\n"
                f"with Path({str(calls)!r}).open('a') as out:\n"
                "    out.write(json.dumps(sys.argv[1:]) + '\\n')\n"
                "for arg in sys.argv[1:]:\n"
                "    if arg.startswith('-DSDKCONFIG='):\n"
                "        target=Path(arg.split('=',1)[1])\n"
                "        if not target.exists():\n"
                f"            target.write_text({config!r})\n"
                f"print({version!r})\n", encoding="utf-8")
            activation = base / "activate.ps1"
            activation.write_text(
                f"$env:IDF_PYTHON_ENV_PATH = {ps_quote(IDF_ENV)}\n",
                encoding="utf-8")
            command = (
                "$ErrorActionPreference='Stop'; "
                f"Set-Location {ps_quote(base)}; $before=(Get-Location).Path; "
                f"& {ps_quote(project / 'build.ps1')} -Action {action} " +
                (f"-Experiment {experiment} " if experiment is not None else "") +
                f"-ActivationScript {ps_quote(activation)} -IdfPath {ps_quote(tool.parent)}; "
                "if ((Get-Location).Path -ne $before) { throw 'Location changed' }; "
                + tail)
            result = subprocess.run(
                [shutil.which("powershell.exe"), "-NoProfile", "-NonInteractive",
                 "-ExecutionPolicy", "Bypass", "-Command", command],
                capture_output=True, text=True, timeout=30)
            argv = [json.loads(line) for line in calls.read_text().splitlines()]
            builds = sorted(p.name for p in project.glob("build*"))
            if tamper:self.assertEqual((output/'sdkconfig').read_text(),tampered)
            if legacy_partition:
                migrated=(output/'sdkconfig').read_text(encoding='utf-8-sig')
                for line in config.splitlines():self.assertIn(line,migrated)
                self.assertIn('CONFIG_PARTITION_TABLE_CUSTOM=y',migrated)
                self.assertIn('CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"',migrated)
                self.assertNotIn('CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y',migrated)
            return result, argv, builds

    def test_activate_persists_command_and_does_not_build(self):
        result, calls, builds = self.run_action(
            "activate", "idf.py --version; if ($LASTEXITCODE -ne 0) { throw 'IDF command failed' }")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(calls, [["--version"], ["--version"]])
        self.assertEqual(builds, ["build", "build.ps1"])

    def test_monitor_forwards_port_and_never_flashes(self):
        result, calls, _ = self.run_action("monitor -Port COM_TEST")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(calls[0], ["--version"])
        self.assertIn('build-throughput-fast-socket-exp-combined',calls[-1][1])
        self.assertEqual(calls[-1][-4:], ["-DWIFI_FRAME_TRANSPORT_OVERRIDE=", "-p", "COM_TEST", "monitor"])
        self.assertFalse(any('flash' in call for call in calls))

    def test_no_arguments_choose_combined_fast_40_profile(self):
        result,calls,builds=self.run_action('build')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('build-throughput-fast-socket-exp-combined',builds)
        self.assertTrue(any('sdkconfig.tcp-fast.defaults' in x and
                            'sdkconfig.experiment-combined.defaults' in x for x in calls[-1]))
        self.assertFalse(any('flash' in call for call in calls))

    def test_existing_configuration_migrates_only_partition_selection(self):
        result,calls,_=self.run_action('build',legacy_partition=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertFalse(any('flash' in call for call in calls))

    def test_explicit_legacy_root_directory_remains_selectable(self):
        result,calls,_=self.run_action('monitor -Profile default -Port COM_TEST',experiment='none')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(calls[-1],['-DWIFI_FRAME_TRANSPORT_OVERRIDE=','-p','COM_TEST','monitor'])

    def test_monitor_requires_port_before_project_configuration(self):
        result, calls, _ = self.run_action("monitor")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("-Port COMx is required", result.stderr)
        self.assertEqual(calls, [["--version"]])

    def test_activate_rejects_wrong_idf(self):
        result, calls, _ = self.run_action("activate", version="ESP-IDF v5.5")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ESP-IDF 6.1 is required", result.stderr)
        self.assertEqual(calls, [["--version"]])

    def test_each_experiment_isolated_and_default_files_not_overwritten(self):
        for name in ('core','direct','combined','psram80'):
            with self.subTest(name=name):
                result,calls,builds=self.run_action('build -Profile throughput',experiment=name)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn(f'build-throughput-fast-socket-exp-{name}',builds)
                self.assertTrue(any(f'sdkconfig.experiment-{name}.defaults' in arg for arg in calls[-1]))
                self.assertFalse(any('flash' in call for call in calls))

    def test_mislabeled_existing_experiment_is_rejected_without_overwrite_or_flash(self):
        result,calls,_=self.run_action('flash -Profile throughput -Port COM_TEST',experiment='combined',tamper=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('Experiment config mismatch',result.stderr)
        self.assertFalse(any('flash' in call for call in calls))

    def test_experiment_monitor_never_flashes_and_keeps_selected_directory(self):
        result,calls,_=self.run_action('monitor -Profile throughput -Port COM_TEST',experiment='core')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('build-throughput-fast-socket-exp-core',calls[-1][1])
        self.assertEqual(calls[-1][-3:],['-p','COM_TEST','monitor'])
        self.assertFalse(any('flash' in call for call in calls))


if __name__ == "__main__":
    unittest.main()
