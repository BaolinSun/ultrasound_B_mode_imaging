"""Structural guards supplement the executable capture/DDR RTL testbenches."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
RTL = ROOT/'build/src/imports/sources'
MANAGED = ROOT/'vivado/ultrasound_B_mode_imaging.srcs/sources_1/imports/imports/sources'


class CaptureDrainContract(unittest.TestCase):
    def test_managed_sources_match_canonical(self):
        for relative in ('proc_top.sv', 'process_top/bproc_top.sv'):
            self.assertEqual((RTL/relative).read_bytes(), (MANAGED/relative).read_bytes())
        for relative in ('data_transfer/frame_store.c', 'data_transfer/frame_store.h', 'uart/uart.c'):
            self.assertEqual((ROOT/'build/src/ps'/relative).read_bytes(),
                             (ROOT/'workspace/B_mode_focus_imaging/src'/relative).read_bytes())

    def test_true_pipeline_events_not_a_bf_level_rearm(self):
        proc = (RTL/'proc_top.sv').read_text(encoding='utf-8')
        processing = (RTL/'process_top/bproc_top.sv').read_text(encoding='utf-8')
        tracker = (RTL/'capture_drain_tracker.sv').read_text(encoding='utf-8')
        self.assertNotIn('tail_pending<=1', proc)
        self.assertIn('capture_drain_tracker capture_drain_i', proc)
        self.assertIn('.shot_done(drain_shot_done)', proc)
        self.assertIn('.line_start(drain_line_start)', proc)
        self.assertIn('.line_done(log_last)', proc)
        self.assertIn('processing_enable = beamform_active_sync_ff2 | capture_busy', proc)
        self.assertRegex(proc, r'\.rtc_enable\s*\(processing_enable\)')
        self.assertIn('assign drain_shot_done = trans_last', processing)
        self.assertIn("trans_phase == 2'd3", processing)
        self.assertIn('shot_start = bf_window && !bf_window_d', tracker)
        self.assertNotRegex(tracker, r'\binitial\s+begin')
        self.assertNotIn('timer', tracker.lower().split('module capture_drain_tracker', 1)[1])

    def test_cdc_keeps_new_tracker_paths_timed(self):
        cdc = (ROOT/'build/constraints/frame_store_cdc.xdc').read_text()
        self.assertIn('NAME !~ proc_top_i/capture_drain_i/*', cdc)
        self.assertIn('proc_top_i/bf_sync1_reg', cdc)
        self.assertIn('busy_sync1', cdc)
        integration = (ROOT/'build/scripts/configure_frame_store.tcl').read_text()
        self.assertIn('capture_drain_tracker.sv process_top/bproc_top.sv', integration)

    def test_joint_simulation_requests_pause_before_last_line_drains(self):
        tb = (ROOT/'build/src/sim/tb_ddr_frame_store.sv').read_text()
        self.assertIn('send_tracked_frame(4)', tb)
        self.assertIn('wait(dut.active && dut.line_number==63)', tb)
        self.assertIn('PAUSE acknowledged before real processing/AXI/FIFO drain', tb)
        self.assertIn('capture_drain_sim', (ROOT/'build/scripts/test_frame_store.ps1').read_text())


if __name__ == '__main__':
    unittest.main()
