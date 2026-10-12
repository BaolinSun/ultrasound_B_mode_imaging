from pathlib import Path
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
PS = ROOT / "build/src/ps"
ESP = ROOT / "esp32c61/main"


class UartContractTests(unittest.TestCase):
    def test_rx_only_and_usb_console(self):
        source = (ESP / "zynq_uart_bridge.c").read_text(encoding="utf-8")
        self.assertNotRegex(source, r"\b(?:uart_write_bytes|uart_tx_chars|uart_wait_tx_done|fgets|getchar)\s*\(")
        self.assertIn("UART_PIN_NO_CHANGE,RX_GPIO", source)
        self.assertIn("gpio_set_direction(UNUSED_TX_GPIO,GPIO_MODE_INPUT)", source)
        self.assertIn("3072,NULL,2,NULL", source)
        self.assertIn("#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG", source)

    def test_early_initialization(self):
        esp = (ESP / "app_main.c").read_text(encoding="utf-8")
        self.assertLess(esp.index("zynq_uart_bridge_start();"), esp.index("sdio_bridge_init();"))
        ps = (PS / "main.c").read_text(encoding="utf-8")
        self.assertLess(ps.index("zynq_uart_log_init()"), ps.index("intr_init();"))

    def test_uart1_has_no_instance_zero_assumption(self):
        source = (PS / "uart/uart.c").read_text(encoding="utf-8")
        self.assertIn("#define UART_BASEADDR       0xE0001000U", source)
        self.assertNotIn("XPAR_XUARTPS_0", source)
        self.assertIn("XGet_IntrId(uartps.Config.IntrId)", source)
        self.assertIn("XGet_IntrOffset(uartps.Config.IntrId)", source)
        self.assertIn("XUartPs_SetBaudRate(&uartps, 460800)", source)

    def test_source_and_bd_copies_match(self):
        for name in ("outbyte.c", "uart/uart_log.c", "uart/uart_log.h"):
            self.assertEqual((PS / name).read_text(encoding="utf-8"),
                             (ROOT / "workspace/B_mode_focus_imaging/src" / name).read_text(encoding="utf-8"))
        bd = ROOT / "build/src/bd/pus_ctrl_system.bd"
        managed = ROOT / "vivado/ultrasound_B_mode_imaging.srcs/sources_1/bd/bd/pus_ctrl_system.bd"
        self.assertEqual(bd.read_text(encoding="utf-8").rstrip(), managed.read_text(encoding="utf-8").rstrip())
        props = json.loads(bd.read_text(encoding="utf-8"))["design"]["components"]["processing_system7_0"]["parameters"]
        for name, value in (("PCW_UART0_PERIPHERAL_ENABLE", "1"), ("PCW_UART0_UART0_IO", "MIO 26 .. 27"),
                            ("PCW_UART1_UART1_IO", "MIO 16 .. 17"), ("PCW_SPI1_GRP_SS1_ENABLE", "0"),
                            ("PCW_SPI1_GRP_SS2_ENABLE", "0")):
            self.assertEqual(props[name]["value"], value)

    def test_sdk_format_and_link_wrapper(self):
        source = (PS / "uart/uart_log.c").read_text(encoding="utf-8")
        for field in (".BaudRate=ZYNQ_LOG_UART_BAUD", ".DataBits=XUARTPS_FORMAT_8_BITS",
                      ".Parity=XUARTPS_FORMAT_NO_PARITY", ".StopBits=XUARTPS_FORMAT_1_STOP_BIT"):
            self.assertIn(field, source)
        self.assertIn("void __wrap_outbyte(char c)", (PS / "outbyte.c").read_text(encoding="utf-8"))
        self.assertIn('"-Wl,--wrap=outbyte"',
                      (ROOT / "workspace/B_mode_focus_imaging/src/UserConfig.cmake").read_text(encoding="utf-8"))
        self.assertIn('"-Wl,--wrap=outbyte"',
                      (ROOT / "build/scripts/build_vitis_2026_1.py").read_text(encoding="utf-8"))

    def test_stub_matches_actual_sdk_format(self):
        sdk = ROOT / "workspace/ubmi_p/export/ubmi_p/sw/standalone_ps7_cortexa9_0/include/xuartps.h"
        if not sdk.exists():
            self.skipTest("Real BSP not generated on this host")
        actual = sdk.read_text(encoding="utf-8")
        fake = (ROOT / "esp32c61/tests/stubs/zynq_uart/xuartps.h").read_text(encoding="utf-8")
        definition = re.search(r"typedef struct \{([^{}]+)\} XUartPsFormat;", actual).group(1)
        self.assertEqual(re.findall(r"(u32|u8)\s+(\w+);", definition),
                         [("u32", "BaudRate"), ("u32", "DataBits"), ("u32", "Parity"), ("u8", "StopBits")])
        self.assertIn("u32 BaudRate,DataBits,Parity;u8 StopBits;", fake)
        for macro, value in (("XUARTPS_FORMAT_8_BITS", "0"), ("XUARTPS_FORMAT_NO_PARITY", "4"),
                             ("XUARTPS_FORMAT_1_STOP_BIT", "0")):
            for header in (actual, fake):
                self.assertRegex(header, rf"#define {macro}\s+{value}U\b")


if __name__ == "__main__":
    unittest.main()
