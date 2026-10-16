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
    def run_action(self, action, tail="", version="ESP-IDF v6.1"):
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
            tool = base / "fake IDF/tools"
            tool.mkdir(parents=True)
            calls = base / "calls.jsonl"
            (tool / "idf.py").write_text(
                "import json, sys\nfrom pathlib import Path\n"
                f"with Path({str(calls)!r}).open('a') as out:\n"
                "    out.write(json.dumps(sys.argv[1:]) + '\\n')\n"
                f"print({version!r})\n", encoding="utf-8")
            activation = base / "activate.ps1"
            activation.write_text(
                f"$env:IDF_PYTHON_ENV_PATH = {ps_quote(IDF_ENV)}\n",
                encoding="utf-8")
            command = (
                "$ErrorActionPreference='Stop'; "
                f"Set-Location {ps_quote(base)}; $before=(Get-Location).Path; "
                f"& {ps_quote(project / 'build.ps1')} -Action {action} "
                f"-ActivationScript {ps_quote(activation)} -IdfPath {ps_quote(tool.parent)}; "
                "if ((Get-Location).Path -ne $before) { throw 'Location changed' }; "
                + tail)
            result = subprocess.run(
                [shutil.which("powershell.exe"), "-NoProfile", "-NonInteractive",
                 "-ExecutionPolicy", "Bypass", "-Command", command],
                capture_output=True, text=True, timeout=30)
            argv = [json.loads(line) for line in calls.read_text().splitlines()]
            builds = sorted(p.name for p in project.glob("build*"))
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
        self.assertEqual(calls[-1], ["-DWIFI_FRAME_TRANSPORT_OVERRIDE=", "-p", "COM_TEST", "monitor"])
        self.assertEqual(len(calls), 2)

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


if __name__ == "__main__":
    unittest.main()
