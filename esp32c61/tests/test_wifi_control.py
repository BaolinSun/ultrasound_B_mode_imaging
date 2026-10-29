import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "esp32c61/pc_tools"))
import zynq_wifi_control as tool
import wifi_config_builder as builder


class ControlTests(unittest.TestCase):
    def test_cpu0_commands_are_explicit_esp_orchestration(self):
        for command in ('CPU0 SLEEP','CPU0 WAKE'):
            self.assertEqual(tool.validate_command(command),command)
        for command in ('CPU0 START','CPU0 SLEEP 1','CPU0 WFI','CPU0'):
            with self.assertRaises(ValueError):tool.validate_command(command)
    def test_cpu1_is_explicit_shutdown_only_and_dry_run_is_offline(self):
        self.assertEqual(tool.validate_command('CPU1 DISABLE'),'CPU1 DISABLE')
        for command in ('CPU1', 'CPU1 ENABLE', 'CPU1 WAKE', 'CPU1 DISABLE 1'):
            with self.assertRaises(ValueError):
                tool.validate_command(command)
        with patch.object(tool,'urlopen',side_effect=AssertionError('network')), contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(tool.main(['--device-ip','192.168.3.27','-c','CPU1 DISABLE','--dry-run']),0)
        self.assertEqual(out.getvalue().strip(),'CPU1 DISABLE')
    def test_afe_power_commands_are_explicit_and_prepare_is_internal(self):
        for command in ('AFE SLEEP', 'AFE WAKE', 'AFE STATUS'):
            tool.validate_command(command)
        for command in ('AFE PREPARE', 'AFE SLEEP 1', 'AFE WAKE extra'):
            with self.assertRaises(ValueError):
                tool.validate_command(command)
    def test_unsigned_numbers_and_strict_commands(self):
        for command in ("HV SET 1", "GAIN SET 9", "GAIN SET -1", "CFG END 1", "STATUS\nHV ENABLE",
                        "BRAM WR t_timing 0 1 2048", "BRAM WR x_element 63 2 0 0", "AFE WR ADC 256 0"):
            with self.subTest(command=command), self.assertRaises(ValueError):
                tool.validate_command(command)
        for command in ("STATUS", "HV ENABLE", "HV DISABLE", "GAIN SET 8", "CFG BEGIN 123", "CFG END",
                        "AFE RD VCA 0xC7", "TX7332 RD 0x3FF", "BRAM WR t_timing 0 1 6144"):
            self.assertEqual(tool.validate_command(command), command)

    def test_dry_run_never_connects_or_prompts(self):
        with patch.object(tool, "urlopen", side_effect=AssertionError("network")), \
             patch("builtins.input", side_effect=AssertionError("prompt")), \
             contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(tool.main(["--device-ip", "192.168.3.27", "--status", "--dry-run"]), 0)
        self.assertEqual(out.getvalue().strip(), "STATUS")

    def test_hv_requires_explicit_flag_even_in_dry_run(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            tool.main(["--device-ip", "192.168.3.27", "-c", "HV ENABLE", "--dry-run"])

    def test_live_cli_needs_no_usb_input_or_token(self):
        result = {"results": [{"command": "STATUS", "response": "@STATUS v=1 cfg=0"}]}
        with tempfile.TemporaryDirectory() as folder, \
             patch("builtins.input", side_effect=AssertionError("USB/interactive input")), \
             patch.object(tool, "Client") as client, \
             contextlib.redirect_stdout(io.StringIO()):
            client.return_value.batch.return_value = result
            client.return_value.history = []
            log = Path(folder) / "result.json"
            self.assertEqual(tool.main(["--device-ip", "192.168.3.27", "--status", "--log", str(log)]), 0)
            client.assert_called_once_with(tool.ipaddress.IPv4Address("192.168.3.27"), None, 300)
            self.assertTrue(json.loads(log.read_text())["passed"])

    def test_full_file_is_validated_before_any_http(self):
        with tempfile.TemporaryDirectory() as folder:
            file = Path(folder) / "commands.txt"
            file.write_text("STATUS\n" * 8 + "HV SET 1\n")
            with patch.object(tool, "urlopen", side_effect=AssertionError("network")), \
                 contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                tool.main(["--device-ip", "192.168.3.27", "--file", str(file)])

    def test_json_explicit_and_layout_rejected_before_commands(self):
        args = argparse.Namespace(target="t_timing", config=None, chunk_words=8)
        for target in tool.TARGETS:
            setattr(args, target + "_config", None)
        with self.assertRaisesRegex(ValueError, "explicit"):
            tool.make_config(args)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "timing.json"
            path.write_text(json.dumps({"bram": {"t_timing": [2048] + [0] * 9}}))
            args.config = path
            with self.assertRaisesRegex(ValueError, "6144"):
                tool.make_config(args)
            path.write_text(json.dumps({"bram": {"t_timing": [6144] + [0] * 7 + [6144, 0]}}))
            for command in tool.make_config(args):
                tool.validate_command(command)

    def test_http_client_matches_job_and_never_replays(self):
        requests = []
        def opener(request, timeout):
            self.assertIsNone(request.get_header("X-control-token"))
            requests.append(request)
            value = {"job_id": "00000001"} if request.data else {
                "busy": False, "state": "succeeded", "results": [{"command": "STATUS", "response": "@STATUS v=1 cfg=0"}]}
            return contextlib.closing(io.StringIO(json.dumps(value)))
        client = tool.Client("127.0.0.1", opener=opener)
        self.assertEqual(client.batch(["STATUS"])["state"], "succeeded")
        self.assertEqual(len(requests), 2)
        def lost(request, timeout):
            raise OSError("not reachable")
        client.opener = lost
        with self.assertRaises(tool.ControlFailure) as failure:
            client.batch(["HV DISABLE"])
        self.assertTrue(failure.exception.unknown)
        self.assertEqual(len(client.history), 2)

    def test_crc_or_command_failure_stops_and_tracks_transaction(self):
        pending = {"busy": False, "state": "failed", "error": "@NACK 12 AFE_WR_FAIL", "results": [
            {"command": "CFG BEGIN 12", "response": "@ACK 12 CFG_BEGIN"},
            {"command": "AFE WR ADC 1 1", "response": "@NACK 12 AFE_WR_FAIL"}]}
        def opener(request, timeout):
            return contextlib.closing(io.StringIO(json.dumps({"job_id": "00000001"} if request.data else pending)))
        client = tool.Client("127.0.0.1", opener=opener)
        with self.assertRaises(tool.ControlFailure) as failure:
            client.batch(["CFG BEGIN 12", "AFE WR ADC 1 1"])
        self.assertFalse(failure.exception.unknown)
        self.assertTrue(client.config_active)


class ReferenceTests(unittest.TestCase):
    def test_read_only_reference_builders_produce_identical_commands(self):
        reference = Path(r"D:\MyProjects\py_prj\ultrasound_dev_kit\tools")
        if not reference.exists():
            self.skipTest("Reference checkout not present")
        sys.path.insert(0, str(reference))
        previous_bytecode = sys.dont_write_bytecode
        sys.dont_write_bytecode = True
        try:
            spec = importlib.util.spec_from_file_location("reference_uart", reference / "ultrasound_config_uart.py")
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            cases = [("afe5832_default.json", "build_afe_commands"), ("tx7332_default.json", "build_tx7332_commands")]
            cases += [(name + "_default.json", "build_bram_commands") for name in builder.BRAM_TARGET_ORDER]
            for filename, function in cases:
                source = reference / "configs" / filename
                if filename == 'delay_profile_default.json' and not source.exists():
                    # The reference checkout now uses focus-specific files. Keep
                    # comparing both independent builders, using all 16 curated
                    # profiles rather than skipping the missing default fixture.
                    profiles = sorted((ROOT/'esp32c61/configs').glob('delay_profile_focus_*mm.json'))
                    self.assertEqual(len(profiles),16)
                    for profile in profiles:
                        data = json.loads(profile.read_text())
                        with self.subTest(filename=profile.name):
                            self.assertEqual(builder.build_bram_commands(data,8),module.build_bram_commands(data,8))
                    continue
                data = json.loads(source.read_text())
                if filename == "t_timing_default.json":
                    # Current hardware only accepts 6144 raw samples. Compare
                    # valid packing, never reintroduce the old 2048-point file.
                    data['bram']['t_timing'][0] = data['bram']['t_timing'][8] = 6144
                params = (data,) if function == "build_afe_commands" else (data, 8)
                with self.subTest(filename=filename):
                    self.assertEqual(getattr(builder, function)(*params), getattr(module, function)(*params))
        finally:
            sys.path.remove(str(reference))
            sys.dont_write_bytecode = previous_bytecode


class NativeGrammarTests(unittest.TestCase):
    def test_c_python_grammar_agree(self):
        compiler = Path(r"D:\AMDDesignTools\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe")
        if not compiler.exists():
            self.skipTest("Host GCC not available")
        with tempfile.TemporaryDirectory() as folder:
            executable = Path(folder) / "protocol.exe"
            subprocess.run([str(compiler), "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "common"),
                            str(ROOT / "esp32c61/tests/test_wifi_protocol.c"), "-o", str(executable)], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)
            for command in ("STATUS", "HV ENABLE", "HV SET 1", "GAIN SET 8", "GAIN SET -1", "CFG BEGIN 01",
                            "BRAM WR t_timing 0 1 6144", "BRAM WR t_timing 0 1 06144",
                            "BRAM WR t_timing 0 1 2048", "TX7332 RD 1024", "AFE WR ADC 0xFF 0xFFFF"):
                try:
                    tool.validate_command(command)
                    accepted = True
                except ValueError:
                    accepted = False
                result = subprocess.run([str(executable), command], capture_output=True)
                self.assertEqual(result.returncode == 0, accepted, command)


if __name__ == "__main__":
    unittest.main()
