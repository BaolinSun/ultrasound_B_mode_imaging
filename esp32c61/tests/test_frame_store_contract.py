from pathlib import Path
import re
import unittest
ROOT=Path(__file__).resolve().parents[2]

class FrameStoreContract(unittest.TestCase):
    def test_retained_pause_reads_only_actual_pl_read_registers(self):
        rtl=(ROOT/'ip_repo/xilinx.com_user_w_ddr_1.1/hdl/ddr_frame_store.sv').read_text()
        decoder=rtl.split('case(ctrl_addr-BASE)',1)[1].split('endcase',1)[0]
        readable={int(n) for n in re.findall(r'^\s*(\d+)\s*:\s*ctrl_rdata',decoder,re.M)}
        self.assertNotIn(8,readable) # FS_SEQ is write-only; do not model it as RAM.
        self.assertNotIn(4,readable)
        self.assertIn('80:ctrl_rdata=seq;',decoder)
        self.assertIn('84:ctrl_rdata=op;',decoder)
        header=(ROOT/'build/src/ps/data_transfer/frame_store.h').read_text()
        offsets={n:int(v) for n,v in re.findall(r'\b(FS_[A-Z_]+)=(\d+)\b',header)}
        source=(ROOT/'build/src/ps/data_transfer/frame_store.c').read_text()
        proof=source.split('int frame_store_pause_retained(void)',1)[1].split('int frame_store_resume',1)[0]
        reads=re.findall(r'Xil_In32\(FS_BASE\+(FS_\w+)\)',proof)
        self.assertTrue(reads)
        for name in reads:self.assertIn(offsets[name],readable,name)
        self.assertIn('FS_DIAG_VERSION',reads)
        self.assertIn('FS_DIAG_COMMAND',reads)
        self.assertGreaterEqual(reads.count('FS_DIAG_SEQUENCE'),2)
        self.assertGreaterEqual(reads.count('FS_ACK'),2)
        self.assertNotIn('Xil_Out',proof)
        self.assertNotIn('command(',proof)

    def test_diagnostics_never_observe_first_synchronizer_stages(self):
        rtl=(ROOT/'ip_repo/xilinx.com_user_w_ddr_1.1/hdl/ddr_frame_store.sv').read_text()
        source=re.search(r'wire \[15:0\] diag_source=(.*?);',rtl,re.S).group(1)
        axi=re.search(r'76:ctrl_rdata=(.*?);',rtl,re.S).group(1)
        self.assertNotIn('req_sync1',source)
        self.assertNotIn('ack_sync1',axi)
        self.assertIn('req_sync2',source)
        self.assertIn('ack_sync2',axi)
        self.assertIn('xpm_cdc_handshake',rtl)
    def test_sources_and_protocol_sync(self):
        for name in ('data_transfer/frame_store.c','data_transfer/frame_store.h',
                     'data_transfer/sdio_bridge.c','data_transfer/bridge_protocol.h','imaging_config.h'):
            self.assertEqual((ROOT/'build/src/ps'/name).read_text(),
                             (ROOT/'workspace/B_mode_focus_imaging/src'/name).read_text())
        self.assertEqual((ROOT/'esp32c61/protocol/bridge_protocol.h').read_text(),
                         (ROOT/'build/src/ps/data_transfer/bridge_protocol.h').read_text())
    def test_no_global_group_overrides_fifo(self):
        pin=(ROOT/'build/constraints/pin.xdc').read_text()
        self.assertNotRegex(pin,r'(?m)^set_clock_groups\b.*afe_to_system_async')
        cdc=(ROOT/'build/constraints/frame_store_cdc.xdc').read_text()
        self.assertIn('NAME !~ *frame_store_i/*',cdc)
        self.assertIn('req_sync1|ack_sync1|ov_sync1|busy_sync1',cdc)
    def test_all_application_sections_exclude_slots(self):
        for path in ('build/src/ps/lscript.ld','workspace/B_mode_focus_imaging/src/lscript.ld'):
            text=(ROOT/path).read_text()
            origin,length=re.search(r'ps7_ddr_0_memory_0\s*:\s*ORIGIN = (0x\w+), LENGTH = (0x\w+)',text).groups()
            self.assertLessEqual(int(origin,16)+int(length,16),0x1c000000)
    def test_raw_udp_and_reserved_registers(self):
        proto=(ROOT/'esp32c61/protocol/bridge_protocol.h').read_text()
        self.assertIn('BR_SDIO_VERSION 3U',proto)
        self.assertIn('BR_SOURCE_PUBLISHED=52',proto)
        for name,value in re.findall(r'(BR_SOURCE_\w+)=(\d+)',proto):
            size=4 if name in ('BR_SOURCE_DROPS','BR_SOURCE_ERRORS','BR_SOURCE_LATEST','BR_SOURCE_PUBLISHED') else 1
            self.assertFalse(set(range(int(value),int(value)+size))&set(range(28,32)),name)
        source=(ROOT/'esp32c61/main/sdio_bridge.c').read_text()
        self.assertIn('psram_udp_submit(s->data,CONFIG_PSRAM_UDP_TEST_BYTES',source)
        self.assertIn('psram_frame_submit(s->data,s->meta.frame_bytes',source)

if __name__=='__main__': unittest.main()
