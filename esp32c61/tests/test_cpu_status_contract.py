"""CPU telemetry stays passive and preserves the existing PS power policy."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding='utf-8')


class CpuStatusContracts(unittest.TestCase):
    def test_ps_sources_are_synchronized(self):
        for name in ('cpu0_idle.c', 'temp.c', 'temp.h'):
            self.assertEqual(source('build/src/ps/system_ctrl/' + name),
                             source('workspace/B_mode_focus_imaging/src/system_ctrl/' + name))

    def test_report_uses_existing_temperature_wake_and_read_only_cpu1(self):
        ps = source('build/src/ps/system_ctrl/cpu0_idle.c')
        report = ps[ps.index('static cpu_status_message_t cpu_snapshot'):ps.index('void cpu0_idle_init')]
        self.assertIn('Xil_In32(0xF8000244U)', report)
        for forbidden in ('Xil_Out32', 'timer_arm(', 'wfi', 'cpu1_power'):
            self.assertNotIn(forbidden, report)
        self.assertIn('temperature_service_sequence()', ps)
        self.assertIn('publish_cpu_status(); /* Existing temperature wake/transition, no new timer. */', ps)
        temp = source('build/src/ps/system_ctrl/temp.c')
        self.assertIn('uint32_t temperature_service_sequence(void) { return sequence; }', temp)

    def test_uart_parses_before_usb_output_and_invalidates_on_loss(self):
        bridge = source('esp32c61/main/zynq_uart_bridge.c')
        pos = bridge.index('cpu_status_cache_feed(bytes,')
        self.assertLess(pos, bridge.index('uart_log_framer_feed', pos))
        self.assertEqual(bridge.count('cpu_status_cache_uart_lost();'), 2)
        cache = source('esp32c61/main/cpu_status_cache.c')
        json = source('esp32c61/main/cpu_status_json.c')
        for text in (cache, json):
            for forbidden in ('zynq_control_submit', 'uart_write_bytes', 'httpd_req_recv', 'malloc('):
                self.assertNotIn(forbidden, text)
        self.assertIn('cpu_status_add_json(json,', source('esp32c61/main/web_server.c'))

    def test_card_rendering_does_not_issue_ps_commands(self):
        html = source('esp32c61/main/index.html')
        start = html.index('function renderCpus(')
        end = html.index('\nfunction ', start + 1)
        render = html[start:end]
        self.assertIn('cpu1-control', render)
        for forbidden in ('fetch(', 'submitCommands(', '/api/zynq', 'POST'):
            self.assertNotIn(forbidden, render)
        self.assertIn('不是 CPU0 瞬时睡眠指示', html)


if __name__ == '__main__':
    unittest.main()
