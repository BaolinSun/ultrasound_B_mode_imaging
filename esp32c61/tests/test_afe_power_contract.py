"""Source synchronization and AFE-only integration, complement executable tests."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AfePowerContracts(unittest.TestCase):
    def test_ps_and_vitis_power_sources_match(self):
        for name in ('AFE5832/afe_power.c', 'AFE5832/afe_power.h'):
            self.assertEqual((ROOT/'build/src/ps'/name).read_text(encoding='utf-8'),
                             (ROOT/'workspace/B_mode_focus_imaging/src'/name).read_text(encoding='utf-8'))

    def test_afe_only_and_no_system_clock_cpu_or_tx_power_writes(self):
        source = (ROOT/'build/src/ps/AFE5832/afe_power.c').read_text(encoding='utf-8')
        for forbidden in ('wfi', 'cpu1', 'A9_CPU_RST', 'tx_stdby', 'AD9512', 'LMK61E2', 'Pl_Reset('):
            self.assertNotIn(forbidden, source)
        self.assertIn('AFE5832_PDN_GBL_PIN_EMIO', source)
        self.assertIn('AP_ACK_EPOCH', source)
        self.assertIn('usb_transport_power_idle()', source)
        self.assertIn('sdio_bridge_power_idle()', source)
        # BSP can define COUNTS_PER_SECOND as an unparenthesized division.
        self.assertIn('/(uint64_t)(COUNTS_PER_SECOND)', source)

    def test_new_afe_cdc_is_not_hidden_by_inherited_domain_exceptions(self):
        xdc = (ROOT/'build/constraints/frame_store_cdc.xdc').read_text(encoding='utf-8')
        self.assertIn('NAME !~ *afe_power_i/*', xdc)
        own = (ROOT/'build/constraints/afe_power_cdc.xdc').read_text(encoding='utf-8')
        self.assertIn('set_bus_skew', own)
        self.assertNotIn('set_clock_groups', own)
        self.assertNotRegex(own, r'(?m)^\s*(if|foreach)\s')
        self.assertNotIn('reg*/Q', own)
        self.assertIn('REF_PIN_NAME == C', own)

    def test_two_stage_sequence_and_temperature_service_retained(self):
        source = (ROOT/'esp32c61/main/zynq_control.c').read_text(encoding='utf-8')
        pos = source.index('static bool afe_operation_inner')
        sequence = source[pos:source.index('static void control_task', pos)]
        self.assertLess(sequence.index('exchange("AFE PREPARE"'), sequence.index('if(!drain())'))
        self.assertLess(sequence.index(' idle=1 '), sequence.index('exchange(command,answer)'))
        main = (ROOT/'build/src/ps/main.c').read_text(encoding='utf-8')
        self.assertIn('temperature_service_poll();', main)


if __name__ == '__main__':
    unittest.main()
