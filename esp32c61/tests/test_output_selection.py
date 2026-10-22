"""Static contract checks; actual mode builds execute in run_tests.ps1."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
CANONICAL = ROOT / "build/src/ps"
IDE = ROOT / "workspace/B_mode_focus_imaging/src"


class OutputSelectionTests(unittest.TestCase):
    def test_canonical_and_ide_sources_match(self):
        for name in ("main.c", "uart/uart.c", "usb/usb.c", "usb/usb_transport.c",
                     "usb/usb_transport.h", "usb/usb_frame_tx.c", "usb/usb_frame_tx.h",
                     "usb/xusbps_COMMS.c", "usb/xusbps_ch9_winusb.c", "usb/USB_CONFIG.h",
                     "data_transfer/output_config.h", "data_transfer/sdio_bridge.h",
                     "data_transfer/sdio_bridge.c"):
            with self.subTest(name=name):
                canonical = (CANONICAL / name).read_text(encoding="utf-8")
                ide = (IDE / name).read_text(encoding="utf-8")
                if name == "main.c":
                    # Preserve the user's IDE comment/banner cleanup. Still
                    # compare every executable statement except that banner.
                    def normalized(text):
                        text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
                        text = text.replace('DEBUG_PRINT("---------------cpu1 start----------\\r\\n");', '')
                        return re.sub(r'\s+', '', text)
                    self.assertEqual(normalized(canonical), normalized(ide))
                else:
                    self.assertEqual(canonical, ide)

    def test_no_serial_runtime_selector(self):
        for name in ("uart/uart.c", "usb/usb.c", "data_transfer/sdio_bridge.c",
                     "data_transfer/sdio_bridge.h"):
            source = (CANONICAL / name).read_text(encoding="utf-8")
            self.assertNotIn("sdio_bridge_set_output", source)
            self.assertNotIn('strcmp(cmd, "OUTPUT")', source)
        html = (ROOT / "esp32c61/main/index.html").read_text(encoding="utf-8")
        self.assertNotIn("OUTPUT SDIO", html)

    def test_main_data_paths_are_compile_time_exclusive(self):
        source = (CANONICAL / "main.c").read_text(encoding="utf-8")
        self.assertRegex(source,
                         r"#if DATA_OUTPUT_MODE == DATA_OUTPUT_SDIO\s+"
                         r"sdio_bridge_poll\(\);\s+#else\s+usb_send_adc_data\([^;]+;\s+#endif")
        header = (CANONICAL / "data_transfer/output_config.h").read_text(encoding="utf-8")
        self.assertIn('#error "DATA_OUTPUT_MODE must be', header)


if __name__ == "__main__":
    unittest.main()
