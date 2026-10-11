from pathlib import Path
import json
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


if __name__ == "__main__":
    unittest.main()
