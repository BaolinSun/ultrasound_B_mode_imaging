"""Self-check the local 2026.1 BSP workaround without starting Vitis."""
import ast
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "build/scripts/uart_bsp_compat.py"


class UartBspTests(unittest.TestCase):
    def load_function(self, workspace):
        tree = ast.parse(SCRIPT.read_text(encoding="utf-8"))
        fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "configure_uart_bsp")
        namespace = dict(Path=Path)
        exec(compile(ast.Module(body=[fn], type_ignores=[]), str(SCRIPT), "exec"), namespace)
        return lambda: namespace[fn.name](workspace)

    def fixture(self, workspace):
        libsrc = Path(workspace) / "ubmi_p/ps7_cortexa9_0/standalone_ps7_cortexa9_0/bsp/libsrc"
        cmake = libsrc / "standalone/src/xilstandalone.cmake"
        driver = libsrc / "uartps/src/xuartps_hw.c"
        cmake.parent.mkdir(parents=True)
        driver.parent.mkdir(parents=True)
        cmake.write_text('if(STANDALONE)\nset(STDIN_BASEADDRESS 0xe0001000)\n'
                         'set(STDOUT_BASEADDRESS 0xe0001000)\nelse()\n'
                         '    set(FREERTOS_UARTCONFIG_INCLUDE "#include \\"FreeRTOSUARTConfig.h\\"")\nendif()\n',
                         encoding="utf-8")
        driver.write_text('void outbyte(char c) { XUartPs_SendByte(STDOUT_BASEADDRESS,c); }\n'
                          'char inbyte(void) { return XUartPs_RecvByte(STDIN_BASEADDRESS); }\n', encoding="utf-8")
        return cmake, driver

    def test_stdout_resolved_independently_without_driver_patch(self):
        with tempfile.TemporaryDirectory() as workspace:
            cmake, driver = self.fixture(workspace)
            self.load_function(workspace)()
            corrected = cmake.read_text(encoding="utf-8")
            self.assertIn("LIST_INDEX(${index} ${standalone_stdout}", corrected)
            self.assertLess(corrected.index("set(STDIN_BASEADDRESS"), corrected.index("set(STDOUT_BASEADDRESS ${${reg}})"))
            self.assertNotIn("__attribute__((weak))", driver.read_text(encoding="utf-8"))
            self.assertIn("XUartPs_RecvByte(STDIN_BASEADDRESS)", driver.read_text(encoding="utf-8"))

    def test_repair_is_idempotent(self):
        with tempfile.TemporaryDirectory() as workspace:
            cmake, _ = self.fixture(workspace)
            repair = self.load_function(workspace)
            self.assertTrue(repair())
            before = cmake.read_text(encoding="utf-8")
            self.assertFalse(repair())
            self.assertEqual(before, cmake.read_text(encoding="utf-8"))

    def test_existing_workspace_repair_never_recreates_app(self):
        source = (ROOT / "build/scripts/repair_vitis_workspace.py").read_text(encoding="utf-8")
        self.assertNotRegex(source, r"\b(?:rmtree|remove_workspace|create_platform_component|import_files|create_app_component)\s*\(")
        self.assertNotIn("vitis.create_client", source)
        self.assertIn('"--target", "clean"', source)
        self.assertIn('"build_app", "-s", app / "src", "-b", app / "build"', source)

    def test_unknown_template_rejected(self):
        with tempfile.TemporaryDirectory() as workspace:
            cmake, driver = self.fixture(workspace)
            cmake.write_text("unexpected template", encoding="utf-8")
            before = driver.read_text(encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "template changed"):
                self.load_function(workspace)()
            self.assertEqual(before, driver.read_text(encoding="utf-8"))

    def test_failed_launcher_cannot_pack_stale_elf(self):
        scripts = ROOT / "build/scripts"
        runner = (scripts / "run_vitis_2026_1.ps1").read_text(encoding="utf-8-sig")
        self.assertIn("$completed.Count -ne $expectedMarkers", runner)
        build = (scripts / "build_sdio.ps1").read_text(encoding="utf-8-sig")
        self.assertLess(build.index("Software build failed; refusing BOOT.BIN"), build.index("create_boot_image.ps1"))
