from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[3]
PS = ROOT / 'build/src/ps'
IDE = ROOT / 'workspace/B_mode_focus_imaging/src'


class Cpu0Integration(unittest.TestCase):
    def test_source_sync(self):
        for relative in ('system_ctrl/cpu0_idle.c', 'system_ctrl/cpu0_idle.h',
                         'system_ctrl/cpu0_idle_config.h', 'system_ctrl/temp.c', 'system_ctrl/temp.h',
                         'uart/uart.c', 'uart/uart.h', 'uart/uart_log.c', 'uart/uart_log.h',
                         'usb/usb.c', 'usb/usb.h', 'usb/usb_transport.c', 'usb/usb_transport.h',
                         'AFE5832/afe_power.c', 'AFE5832/afe_power.h', 'TX7332/tx_power.c', 'TX7332/tx_power.h',
                         'data_transfer/sdio_bridge.c', 'data_transfer/sdio_bridge.h'):
            self.assertEqual((PS / relative).read_text(), (IDE / relative).read_text(), relative)
        for root in (PS, IDE):
            main = (root / 'main.c').read_text(errors='replace')
            self.assertIn('cpu0_idle_init();', main)
            loop = main.split('while (1) {', 1)[1].split('}', 1)[0]
            self.assertEqual(loop.strip(), 'cpu0_foreground_poll();')
        self.assertIn('system_ctrl/cpu0_idle.c', (IDE / 'UserConfig.cmake').read_text())

    def test_atomic_snapshot_is_read_only(self):
        for relative, name in (('uart/uart.c', 'uart_idle_pending'), ('uart/uart_log.c', 'zynq_uart_idle_pending'),
                               ('usb/usb.c', 'usb_commands_idle_pending'), ('usb/usb_transport.c', 'usb_transport_idle_pending')):
            text = (PS / relative).read_text(errors='replace')
            body = text.split('int ' + name + '(void)', 1)[1].split('}', 1)[0]
            for forbidden in ('EnableMask', 'WriteReg', 'pop_', 'poll(', 'Send', 'Receive'):
                self.assertNotIn(forbidden, body)
        adapter = (PS / 'system_ctrl/cpu0_idle.c').read_text()
        self.assertNotIn('XSCUGIC_INT_ACK_OFFSET', adapter)
        self.assertNotIn('XSCUGIC_ACK_INT', adapter)
        self.assertIn('XSCUGIC_HI_PEND_OFFSET', adapter)
        self.assertIn('if(!q)sdio_bridge_poll();', adapter)
        self.assertIn('frame_store_pause_retained()', adapter)
        self.assertIn('temperature_service_remaining_ms()', adapter)
        self.assertIn('#define TIMER_IRQ 29U', adapter)
        self.assertIn('led_due=now+500000U', adapter)

    def test_status_fits_existing_wire_limit(self):
        text = (PS / 'uart/uart.c').read_text()
        template = re.search(r'"(@STATUS v=1 cfg=.*?wfi=%u)"', text).group(1)
        # uint32 seq maximum, nine-char power transition names, bounded gain/downsample.
        worst = template % (1, 2, 4294967295, 1, 1, 1, 4, 'quiescing', 'quiescing', 1, 1)
        self.assertLess(len(worst), 128)
        self.assertIn('char response[128]', text)

    def test_timer_never_modifies_shared_power_resources(self):
        text = (PS / 'system_ctrl/cpu0_idle.c').read_text()
        for forbidden in ('Xil_DCache', 'Xil_L2', 'F800', 'SLCR', 'cpu1_power_init(', 'XTime_SetTime', 'XScuGic_CfgInitialize'):
            self.assertNotIn(forbidden, text)
        self.assertIn('#define CPU0_IDLE_WFI_ENABLE 1', (PS / 'system_ctrl/cpu0_idle_config.h').read_text())
        script = (ROOT / 'build/scripts/build_frame_store_apps.ps1').read_text()
        self.assertIn('CPU0_IDLE_WFI_ENABLE=$Wfi', script)
        self.assertIn('@(\'usb\',\'sdio\')', script)


if __name__ == '__main__':
    unittest.main()
