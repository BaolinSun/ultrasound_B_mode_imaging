"""Temperature integration contracts; host C tests exercise register transactions."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
PS = ROOT / "build/src/ps"
IDE = ROOT / "workspace/B_mode_focus_imaging/src"
ESP = ROOT / "esp32c61/main"


class TemperatureContractTests(unittest.TestCase):
    def test_temperature_sources_are_synced_without_overwriting_main(self):
        for name in ("system_ctrl/temp.c", "system_ctrl/temp.h",
                     "system_ctrl/temp_hw.c", "system_ctrl/temp_hw.h"):
            self.assertEqual((PS / name).read_bytes().replace(b"\r\n", b"\n"),
                             (IDE / name).read_bytes().replace(b"\r\n", b"\n"))

    def test_foreground_init_poll_and_usb_wait_hooks(self):
        for root in (PS, IDE):
            main = (root / "main.c").read_text(encoding="utf-8")
            self.assertLess(main.index("IIC_Init();"), main.index("temperature_service_init();"))
            self.assertRegex(main, r"while \(1\) \{\s*temperature_service_poll\(\);")
            usb = (root / "usb/usb.c").read_text(encoding="utf-8")
            wait = usb[usb.index("while (recv_new_data"):usb.index("while (recv_new_data")+900]
            self.assertIn("temperature_service_poll();", wait)
            # The other hook is in the foreground packet loop, not a USB ISR.
            self.assertEqual(usb.count("temperature_service_poll();"), 2)
            self.assertNotIn("temperature_service_poll", usb[usb.index("u32 xusbps_ep_buffer_handle"):])

    def test_no_unbounded_sensor_or_uart_driver_calls(self):
        source = (PS / "system_ctrl/temp_hw.c").read_text(encoding="utf-8")
        self.assertNotRegex(source, r"\b(?:XIicPs_MasterRecvPolled|XIicPs_MasterSendPolled|XAdcPs_GetAdcData|XUartPs_SendByte)\s*\(")
        self.assertIn("TEMP_IO_TIMEOUT_MS", source)
        self.assertIn("XPAR_XIICPS_0_BASEADDR", source)
        self.assertNotIn("XPAR_XIICPS_1_BASEADDR", source)
        self.assertIn("cfr&~XADCPS_CFR1_SEQ_VALID_MASK", source)
        self.assertNotIn("SetAlarmThreshold", source)
        self.assertNotIn("SetAlarmEnables", source)

    def test_raw_parse_precedes_monitor_output_and_loss_clears_cache(self):
        source = (ESP / "zynq_uart_bridge.c").read_text(encoding="utf-8")
        self.assertLess(source.index("temperature_cache_feed(bytes"), source.index("uart_log_framer_feed(&framer,bytes"))
        self.assertEqual(source.count("temperature_cache_uart_lost();"), 2)
        self.assertNotRegex(source, r"\b(?:uart_write_bytes|uart_tx_chars)\s*\(")

    def test_only_status_endpoint_exposes_telemetry(self):
        source = (ESP / "web_server.c").read_text(encoding="utf-8")
        config = source[source.index("static esp_err_t get_config"):source.index("static bool copy_json_string")]
        status = source[source.index("static esp_err_t get_status"):source.index("static esp_err_t upload")]
        self.assertNotIn("temperatures", config)
        self.assertIn('cJSON_AddObjectToObject(json,"temperatures")', status)
        self.assertIn('cJSON_AddNullToObject(channel,"celsius")', status)
        self.assertIn('cJSON_AddStringToObject(channel,"status",state)', status)

    def test_shared_protocol_is_a_build_input_on_both_mcus(self):
        self.assertIn('"../../common"', (ESP / "CMakeLists.txt").read_text())
        cmake = (IDE / "UserConfig.cmake").read_text()
        self.assertIn('"../../../common"', cmake)
        self.assertEqual(cmake.count('"system_ctrl/temp_hw.c"'), 1)
        header = (ROOT / "common/temperature_protocol.h").read_text()
        self.assertIn("#define TEMP_PERIOD_MS 10000U", header)
        self.assertIn("#define TEMP_STALE_MS 30000U", header)


if __name__ == "__main__":
    unittest.main()
