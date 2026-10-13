"""Self-check the approved cleanup without starting Vitis or accessing a board."""
import ast
import os
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
CANONICAL = ROOT / "build/src/ps"
APP = ROOT / "workspace/B_mode_focus_imaging/src"
REMOVED = (
    "data_transfer/axidma.c", "data_transfer/axidma.h",
    "data_process/compress.c", "data_process/compress.h",
    "linux_wifi/linux_wifi.c", "linux_wifi/linux_wifi.h",
    "eeprom/eeprom.c", "eeprom/eeprom.h",
    "TX7332/tx7332.c", "TX7332/tx7332.h",
    "TX7332/tx7332_spi.c", "TX7332/tx7332_spi.h",
    "key/key.c", "key/key.h", "timer/timer.c", "timer/timer.h",
    "data_transfer/data_recv.c", "data_transfer/data_recv.h",
    "CLOCK/AD9512.c", "CLOCK/AD9512.h", "CLOCK/system_clk.c", "CLOCK/system_clk.h",
    "pus_operation/pus_oper.c", "pus_operation/pus_oper.h",
)


def cmake_list(text, name):
    match = re.search(r"set\(" + re.escape(name) + r"\s*(.*?)\n\)", text, re.S)
    if not match:
        raise AssertionError("Missing CMake list: " + name)
    return re.findall(r'"([^"]+)"', match.group(1))


def source_configurator():
    # The generator's top level recreates a workspace. Execute ONLY the two
    # pure configuration helpers: never import/run its side-effecting entry.
    tree = ast.parse((ROOT / "build/scripts/build_vitis_2026_1.py").read_text(encoding="utf-8"))
    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name in ("set_cmake_list", "configure_application_sources")]
    assert len(functions) == 2
    namespace = {"os": os, "re": re, "SDIO_CLOCK_MHZ": 25}
    exec(compile(ast.Module(body=functions, type_ignores=[]), "source_helpers", "exec"), namespace)
    return namespace["configure_application_sources"]


class CleanupTests(unittest.TestCase):
    def test_approved_files_removed_from_both_trees(self):
        self.assertEqual(len(REMOVED), 24)
        for root in (CANONICAL, APP):
            for relative in REMOVED:
                self.assertFalse((root / relative).exists(), str(root / relative))

    def test_temperature_preserved_and_core_sources_synced(self):
        for relative in ("system_ctrl/tmp.h", "usb/usb.c",
                         "data_transfer/sdio_bridge.c",
                         "data_transfer/output_config.h", "outbyte.c"):
            a = (CANONICAL / relative).read_bytes().replace(b"\r\n", b"\n")
            b = (APP / relative).read_bytes().replace(b"\r\n", b"\n")
            self.assertEqual(a, b, relative)
        # Keep temperature APIs, without freezing future legitimate driver edits
        # to the cleanup-day hashes (the delivery report records those hashes).
        # The workspace user removed empty temp_init before relocation and
        # cleaned main.c during the task. Preserve those divergences rather
        # than overwriting user edits to force identical file contents.
        for root in (CANONICAL, APP):
            temperature = (root / "system_ctrl/tmp.c").read_text(encoding="utf-8")
            self.assertRegex(temperature, r"\bfloat\s+read_temp\s*\(")
            self.assertIn("IIC_polled_read", temperature)

    def test_no_deleted_includes_or_empty_receive_call(self):
        deleted_headers = {Path(path).name for path in REMOVED if path.endswith(".h")}
        for root in (CANONICAL, APP):
            for path in list(root.rglob("*.c")) + list(root.rglob("*.h")):
                # Include paths are ASCII; legacy comments need not be UTF-8.
                # Decode losslessly instead of changing the user's source encoding.
                text = path.read_bytes().decode("latin-1")
                for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
                    self.assertNotIn(Path(name).name, deleted_headers, str(path))
                    if name.startswith(".") or "/" in name:
                        self.assertTrue((path.parent / name).exists(), str(path) + " -> " + name)
            self.assertNotIn("data_recv_init", (root / "main.c").read_text(encoding="utf-8"))

    def test_current_cmake_lists_all_remaining_sources(self):
        text = (APP / "UserConfig.cmake").read_text(encoding="utf-8")
        sources = cmake_list(text, "USER_COMPILE_SOURCES")
        expected = {p.relative_to(APP).as_posix() for p in APP.rglob("*.c")}
        self.assertEqual(set(sources), expected)
        self.assertEqual(len(sources), len(set(sources)))
        self.assertIn("system_ctrl/tmp.c", sources)
        self.assertIn("system_ctrl", cmake_list(text, "USER_INCLUDE_DIRECTORIES"))
        self.assertNotIn("tmp", cmake_list(text, "USER_INCLUDE_DIRECTORIES"))
        self.assertIn("-Wl,--wrap=outbyte", text)

    def test_regenerator_ignores_deleted_empty_dirs_and_editor_cache(self):
        configure = source_configurator()
        with tempfile.TemporaryDirectory(prefix="vitis_cleanup_test_") as tmp:
            root = Path(tmp)
            (root / "UserConfig.cmake").write_text((APP / "UserConfig.cmake").read_text(encoding="utf-8"), encoding="utf-8")
            for relative in ("main.c", "outbyte.c", "system_ctrl/tmp.c", "system_ctrl/tmp.h",
                             "data_transfer/sdio_bridge.c", "data_transfer/output_config.h"):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("/* fixture */\n")
            for directory in ("TX7332", "linux_wifi", "key", "eeprom", "timer", "data_process", "CLOCK", "tmp", "pus_operation", ".cache"):
                (root / directory).mkdir()
            (root / ".cache/not_a_source.c").write_text("/* never imported */\n")
            configure(str(root))
            once = (root / "UserConfig.cmake").read_text(encoding="utf-8")
            configure(str(root))
            self.assertEqual(once, (root / "UserConfig.cmake").read_text(encoding="utf-8"))
            self.assertEqual(cmake_list(once, "USER_COMPILE_SOURCES"),
                             ["data_transfer/sdio_bridge.c", "main.c", "outbyte.c", "system_ctrl/tmp.c"])
            self.assertEqual(cmake_list(once, "USER_INCLUDE_DIRECTORIES"), [".", "data_transfer", "system_ctrl"])
            self.assertIn("SDIO_RUN_CLOCK_HZ=25000000u", once)
            self.assertIn("-Wl,--wrap=outbyte", once)

    def test_temperature_relocated_without_old_directory(self):
        for root in (CANONICAL, APP):
            self.assertFalse((root / "tmp").exists())
            for name in ("tmp.c", "tmp.h"):
                self.assertTrue((root / "system_ctrl" / name).is_file())
            main = (root / "main.c").read_text(encoding="utf-8")
            self.assertIn('#include "system_ctrl/tmp.h"', main)
            self.assertNotIn("tmp/tmp.h", main)

    def test_afe_initialized_directly_once_in_original_order_with_settling(self):
        for root in (CANONICAL, APP):
            self.assertFalse((root / "pus_operation").exists())
            main = (root / "main.c").read_text(encoding="utf-8")
            main = re.sub(r"/\*.*?\*/|//[^\n]*", "", main, flags=re.S)
            self.assertEqual(len(re.findall(r"\bAFE5832_init\s*\(\s*\)", main)), 1)
            self.assertRegex(main, r"\bAFE5832_init\(\);\s*sleep\(2\);\s*led_init\(\);")
            self.assertNotRegex(main, r"\b(pus_init|pus_chip_init)\s*\(")
            self.assertIn('#include "AFE5832/afe5832.h"', main)
            for before, after in (("gpio_init();", "AFE5832_init();"),
                                  ("IIC_Init();", "AFE5832_init();"),
                                  ("hv_init();", "AFE5832_init();"),
                                  ("AFE5832_init();", "para_down();"),
                                  ("para_down();", "usb_init();"),
                                  ("usb_init();", "sdio_bridge_init();")):
                self.assertLess(main.index(before), main.index(after))
        text = (APP / "UserConfig.cmake").read_text(encoding="utf-8")
        self.assertNotIn("pus_operation", cmake_list(text, "USER_INCLUDE_DIRECTORIES"))

    def test_hardware_clock_does_not_depend_on_removed_chip_setup(self):
        for root in (CANONICAL, APP):
            self.assertFalse((root / "CLOCK").exists())
            main = (root / "main.c").read_text(encoding="utf-8")
            for symbol in ("hw_spi_init", "sys_clk_init", "clk_61e2_init", "ad9512_init"):
                self.assertNotRegex(main, r"\b" + symbol + r"\s*\(")
            # Clock removal must not disable the still-needed IIC/HV/SDIO init.
            for symbol in ("IIC_Init", "hv_init", "sdio_bridge_init"):
                self.assertRegex(main, r"\b" + symbol + r"\s*\(")

    def test_migration_checker_does_not_read_removed_interrupt_sources(self):
        text = (ROOT / "build/scripts/check_migration.ps1").read_text(encoding="utf-8")
        self.assertNotIn("$DataRecv = Get-Content", text)
        self.assertNotIn("'key\\key.c'", text)
        self.assertNotIn("'timer\\timer.c'", text)
        self.assertIn("Test-Path -LiteralPath (Join-Path $NewApp $RemovedReceiveSource)", text)
        self.assertIn("'uart\\uart.c'", text)
        self.assertIn("'usb\\usb.c'", text)


if __name__ == "__main__":
    unittest.main()
