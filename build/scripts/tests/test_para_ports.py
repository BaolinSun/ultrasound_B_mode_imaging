"""Regression for the actual saved BD and its cold-load verification contract."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'build/scripts'))
from check_bd_connectivity import signature

SPECS = (
    ('para_addr', 'O', 24, 'bram_addr_a', 'ctrl_addr'),
    ('para_din', 'O', 32, 'bram_wrdata_a', 'ctrl_wdata'),
    ('para_en', 'O', 1, 'bram_en_a', 'ctrl_en'),
    ('para_we', 'O', 4, 'bram_we_a', 'ctrl_we'),
    ('para_clk', 'O', 1, 'bram_clk_a', None),
    ('para_rst', 'O', 1, 'bram_rst_a', None),
    ('para_dout', 'I', 32, 'bram_rddata_a', None),
)


def validate(design):
    assert 'para' not in design['interface_ports']
    for name, direction, width, source, sink in SPECS:
        port = design['ports'][name]
        assert port['direction'] == direction, name
        assert abs(int(port.get('left', 0)) - int(port.get('right', 0))) + 1 == width, name
        nets = [n['ports'] for n in design['nets'].values() if name in n['ports']]
        assert len(nets) == 1, name
        assert 'axi_bram_ctrl_0/' + source in nets[0], name
        if sink:
            assert 'w_ddr_0/' + sink in nets[0], name


class ParaPorts(unittest.TestCase):
    def setUp(self):
        self.path = ROOT / 'build/src/bd/pus_ctrl_system.bd'
        self.design = json.loads(self.path.read_text())['design']

    def test_saved_canonical_and_managed_are_identical(self):
        self.assertEqual(self.path.read_bytes(), (ROOT / 'vivado/ultrasound_B_mode_imaging.srcs/sources_1/bd/bd/pus_ctrl_system.bd').read_bytes())
        validate(self.design)

    def test_missing_port_wrong_width_and_net_are_detected(self):
        for name, *_ in SPECS:
            broken = copy.deepcopy(self.design)
            del broken['ports'][name]
            with self.assertRaises((AssertionError, KeyError)):
                validate(broken)
        broken = copy.deepcopy(self.design)
        broken['ports']['para_addr']['left'] = '22'
        with self.assertRaises(AssertionError):
            validate(broken)
        broken = copy.deepcopy(self.design)
        for net in broken['nets'].values():
            if 'para_en' in net['ports']:
                net['ports'].remove('w_ddr_0/ctrl_en')
        with self.assertRaises(AssertionError):
            validate(broken)

    def test_cold_verification_never_repairs_ports(self):
        verifier = (ROOT / 'build/scripts/verify_para_ports.tcl').read_text()
        self.assertNotRegex(verifier, r'(?m)^\s*(configure_para_ports|save_bd_design|generate_target)\b')
        self.assertIn('open_project -read_only', verifier)
        self.assertIn('assert_para_ports', verifier)
        builder = (ROOT / 'build/scripts/configure_frame_store.tcl').read_text()
        self.assertLess(builder.index('save_reload_para_ports'), builder.index('generate_target'))
        creator = (ROOT / 'build/scripts/create_vivado_2026_1.tcl').read_text()
        self.assertNotIn('w_ddr_v1_0_M00_AXI_inst/wr_ddr_start_ff_reg/D', creator)
        self.assertIn('frame_store_i/${pin}_reg/D', creator)

    def test_graph_comparison_catches_swapped_connection_and_rejects_behaviour(self):
        text = (ROOT / 'vivado/ultrasound_B_mode_imaging.gen/sources_1/bd/bd/synth/pus_ctrl_system.v').read_text()
        self.assertEqual(signature(text), signature('// metadata\n' + text))
        self.assertIn('.ctrl_addr(para_addr)', text)
        self.assertNotEqual(signature(text), signature(text.replace('.ctrl_addr(para_addr)', '.ctrl_addr(para_din)')))
        with self.assertRaises(ValueError):
            signature(text + '\nalways @(posedge clk) a <= b;')


if __name__ == '__main__':
    unittest.main()
