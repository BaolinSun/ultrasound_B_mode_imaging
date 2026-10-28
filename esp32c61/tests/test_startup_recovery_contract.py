from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
PS = ROOT / 'build/src/ps'
IDE = ROOT / 'workspace/B_mode_focus_imaging/src'


class StartupRecoveryContract(unittest.TestCase):
    def test_sources_and_build_registration(self):
        for name in ('system_ctrl/startup_recovery.c', 'system_ctrl/startup_recovery.h',
                     'system_ctrl/temp.c', 'system_ctrl/temp.h', 'system_ctrl/imaging_power.h',
                     'system_ctrl/cpu0_idle.c', 'AFE5832/afe_power.c', 'AFE5832/afe_power.h',
                     'TX7332/tx_power.c', 'TX7332/tx_power.h', 'data_transfer/sdio_bridge.c',
                     'data_transfer/sdio_bridge.h', 'uart/uart.c', 'bram/bram.c'):
            self.assertEqual((PS/name).read_bytes(), (IDE/name).read_bytes(), name)
        self.assertIn('"system_ctrl/startup_recovery.c"', (IDE/'UserConfig.cmake').read_text())
        for root in (PS, IDE):
            main = (root/'main.c').read_text(errors='replace')
            self.assertIn('startup_recovery_init(frame_init_rc,inherited_usb_idle);', main)
            self.assertLess(main.index('tx_power_init();'), main.index('startup_recovery_init('))
            self.assertLess(main.index('afe_power_init();'), main.index('startup_recovery_init('))

    def test_gate_and_foreground_only(self):
        gate = (PS/'system_ctrl/imaging_power.h').read_text()
        self.assertIn('!startup_recovery_blocks()', gate)
        scheduler = (PS/'system_ctrl/cpu0_idle.c').read_text()
        self.assertLess(scheduler.index('startup_recovery_poll();'), scheduler.index('if(!q)sdio_bridge_poll();'))
        self.assertIn('BOOT_RECOVERY_WAIT_TEMP_OR_CLOCK', (PS/'uart/uart.c').read_text())
        sdio = (PS/'data_transfer/sdio_bridge.c').read_text()
        self.assertIn('frame_store_is_ready() && !startup_recovery_blocks()', sdio)

    def test_no_full_reinit_or_analog_clock_configuration(self):
        source = (PS/'system_ctrl/startup_recovery.c').read_text()
        executable = '\n'.join(line.split('/*')[0] for line in source.splitlines()
                               if not line.lstrip().startswith('*'))
        for forbidden in ('AFE5832_init();', 'para_down();', 'gpio_init();', 'hv_enable();',
                          'Beamform_Resume();', 'Pl_Reset();', 'tx_power_init();',
                          'usb_transport_boot_quiesce();', 'AFE5832_WriteRegMap('):
            self.assertNotIn(forbidden, executable)
        self.assertIn('temperature_service_zynq_mc(&mc)', source)
        self.assertIn('frame_store_pause()', source)


if __name__ == '__main__':
    unittest.main()
