"""Integration checks: restore the real ILA without changing original probes."""
from pathlib import Path
import json
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
CANONICAL = ROOT/'build/src/imports'
MANAGED = ROOT/'vivado/ultrasound_B_mode_imaging.srcs/sources_1/imports/imports'


class BfIlaTests(unittest.TestCase):
    def test_saved_project_keeps_real_core_and_implementation_hook(self):
        tree = ET.parse(ROOT/'vivado/ultrasound_B_mode_imaging.xpr')
        self.assertIsNotNone(tree.find(".//FileSet[@Name='ila_bf']/File"))
        self.assertIsNotNone(tree.find(".//Run[@Id='ila_bf_synth_1']"))
        step = tree.find(".//Run[@Id='impl_1']/Strategy/Step[@Id='opt_design']")
        self.assertIn('bf_ila_impl_pre.tcl', step.attrib['PreStepTclHook'])

    def test_original_probe_connections_are_unchanged_in_both_copies(self):
        expected = {'clk': 'data_clk', 'probe0': '{proc_en,proc_in[15:0]}',
                    'probe1': '{rtc_rxen,proc_top_i.rtc_bfen}', 'probe2': '{up_dataen,up_data}',
                    'probe3': 'proc_top_i.rx_foc_delta_rddata', 'probe4': 'rtc_line'}
        for base in (CANONICAL, MANAGED):
            text = (base/'sources/heart_top.sv').read_text(encoding='utf-8', errors='replace')
            block = re.search(r'\bila_bf\s+ila_bf_i\s*\((.*?)\);', text, re.S).group(1)
            actual = {name: re.sub(r'\s+', '', value) for name, value in
                      re.findall(r'\.(\w+)\s*\(([^()]*)\)', block)}
            self.assertEqual(actual, expected)
            self.assertNotIn('bf_ila_probe_tap', text)

    def test_no_empty_bf_stub_remains(self):
        for base in (CANONICAL, MANAGED):
            text = (base/'release/ila_release_stubs.v').read_text(encoding='utf-8')
            self.assertNotRegex(text, r'\bmodule\s+ila_bf\b')
            self.assertRegex(text, r'\bmodule\s+ila_tx\b')
            self.assertRegex(text, r'\bmodule\s+ila_timing\b')

    def test_original_ip_geometry(self):
        text = (ROOT/'build/scripts/configure_bf_ila.tcl').read_text(encoding='utf-8')
        for prop, value in [('C_NUM_OF_PROBES',5), ('C_DATA_DEPTH',8192),
                            ('C_INPUT_PIPE_STAGES',0), *[(f'C_PROBE{i}_WIDTH',w) for i,w in enumerate((17,2,17,32,7))]]:
            self.assertRegex(text, rf'CONFIG\.{prop}\s+\{{{value}\}}')
        self.assertNotIn('CONFIG.C_PROBE5_WIDTH', text)
        self.assertIn('get_runs -quiet ila_bf_synth_1', text)
        self.assertIn('create_ip_run -force', text)

    def test_generated_ip_definition_has_original_geometry(self):
        for path in (ROOT/'build/src/ip/ila_bf/ila_bf.xci',
                     ROOT/'vivado/ultrasound_B_mode_imaging.srcs/sources_1/ip/ila_bf/ila_bf.xci'):
            data = json.loads(path.read_text(encoding='utf-8'))
            self.assertEqual(data['ip_inst']['component_reference'], 'xilinx.com:ip:ila:6.2')
            params = data['ip_inst']['parameters']['component_parameters']
            for name, value in [('C_NUM_OF_PROBES',5), ('C_DATA_DEPTH',8192), ('C_INPUT_PIPE_STAGES',0),
                                *[(f'C_PROBE{i}_WIDTH',w) for i,w in enumerate((17,2,17,32,7))]]:
                self.assertEqual(int(params[name][0]['value']), value)

    def test_all_build_entries_restore_and_verify_real_core_and_export_ltx(self):
        for name in ('create_vivado_2026_1.tcl', 'build_sdio_current.tcl', 'build_current_project_2026_1.tcl'):
            text = (ROOT/'build/scripts'/name).read_text(encoding='utf-8')
            self.assertIn('configure_bf_ila.tcl', text)
            self.assertIn('verify_bf_ila.tcl', text)
            self.assertIn('write_debug_probes -force', text)
        text = (ROOT/'build/scripts/create_vivado_2026_1.tcl').read_text(encoding='utf-8')
        self.assertIn('$ip_name in {ila_timing ila_tx}', text)
        self.assertNotIn('$ip_name in {ila_bf ila_timing ila_tx}', text)

    def test_hub_clock_does_not_depend_on_adc_clock(self):
        text = (ROOT/'build/scripts/bf_ila_impl_pre.tcl').read_text(encoding='utf-8')
        self.assertIn('get_nets -quiet clk_100m', text)
        self.assertIn('C_CLK_INPUT_FREQ_HZ 100000000', text)
        self.assertIn('connect_debug_port dbg_hub/clk', text)
        self.assertNotIn('get_nets -quiet data_clk', text)

    def test_only_exact_empty_stubs_are_pruned_and_no_board_programming(self):
        text = (ROOT/'build/scripts/bf_ila_impl_pre.tcl').read_text(encoding='utf-8')
        self.assertIn('{proc_top_i/ila_timing_i proc_top_i/tx_top_i/ila_tx_i}', text)
        self.assertIn('get_property IS_BLACKBOX', text)
        self.assertIn('DIRECTION != IN', text)
        for name in ('bf_ila_impl_pre.tcl', 'configure_bf_ila.tcl', 'prepare_bf_ila.tcl', 'verify_bf_ila.tcl'):
            code = (ROOT/'build/scripts'/name).read_text(encoding='utf-8')
            for forbidden in ('program_hw_devices', 'connect_hw_server', 'open_hw_target', 'HV ENABLE'):
                self.assertNotIn(forbidden, code)


if __name__ == '__main__':
    unittest.main()
