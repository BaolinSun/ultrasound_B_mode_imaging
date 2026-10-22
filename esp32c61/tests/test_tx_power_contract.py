"""Independent power integration contracts; executable RTL/C tests complement these."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
class TxPowerContracts(unittest.TestCase):
    def test_ps_sources_synchronized(self):
        for name in ('TX7332/tx_power.c','TX7332/tx_power.h','system_ctrl/imaging_power.h',
                     'AFE5832/afe_power.c','uart/uart.c','bram/bram.c','usb/usb_transport.c','data_transfer/frame_store.c'):
            self.assertEqual((ROOT/'build/src/ps'/name).read_bytes(),(ROOT/'workspace/B_mode_focus_imaging/src'/name).read_bytes())
    def test_all_four_shifters_and_pending_read_protect_standby(self):
        s=(ROOT/'build/src/imports/sources/tx_top/tx_top.v').read_text(encoding='utf-8')
        for n in range(4):self.assertIn(f'.power_idle(spi_power_idle[{n}])',s)
        self.assertIn('(&spi_power_idle)',s)
        self.assertIn('!tx_spi_rdreq_pending',s)
        self.assertIn('assign tx_stdby = power_standby',s)
    def test_standby_is_not_a_shared_reset_or_clock_configuration(self):
        s=(ROOT/'build/src/ps/TX7332/tx_power.c').read_text(encoding='utf-8')
        for text in ('PL_Reset','PLLRST','LMK61E2','AD9512','wfi','CPU1','Tx7332_WriteSectionWords'):
            self.assertNotIn(text,s)
        self.assertIn('frame_store_pause_retained()',s)
        self.assertIn('Beamform_RequestStop()',s)
    def test_aggregate_guards_and_local_cdc(self):
        s=(ROOT/'build/src/ps/system_ctrl/imaging_power.h').read_text(encoding='utf-8')
        self.assertIn('afe_power_allowed() && tx_power_allowed()',s)
        xdc=(ROOT/'build/constraints/tx_power_cdc.xdc').read_text(encoding='utf-8')
        self.assertNotIn('set_clock_groups',xdc)
        self.assertIn('set_max_delay -datapath_only',xdc)
        self.assertIn('NAME !~ *tx_power_i/*',(ROOT/'build/constraints/frame_store_cdc.xdc').read_text())
    def test_cli_only_public_commands(self):
        import sys
        sys.path.insert(0,str(ROOT/'esp32c61/pc_tools'))
        import zynq_wifi_control as control
        for command in ('TX7332 SLEEP','TX7332 WAKE','TX7332 STATUS'):control.validate_command(command)
        for command in ('TX7332 PREPARE','TX7332 SLEEP 1','TX7332 WAKE 1'):
            with self.assertRaises(ValueError):control.validate_command(command)
