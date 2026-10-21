from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]

class FrameRateContract(unittest.TestCase):
    def test_ps_sources_and_build_registration_match(self):
        for name in ('frame_crc.c','frame_crc.h','sdio_bridge.c'):
            a=(ROOT/'build/src/ps/data_transfer'/name).read_bytes()
            b=(ROOT/'workspace/B_mode_focus_imaging/src/data_transfer'/name).read_bytes()
            self.assertEqual(a,b,name)
        config=(ROOT/'workspace/B_mode_focus_imaging/src/UserConfig.cmake').read_text(encoding='utf-8')
        self.assertIn('"data_transfer/frame_crc.c"',config)
        self.assertIn('set(USER_COMPILE_OPTIMIZATION_LEVEL -O0)',config)

    def test_crc_stays_inside_same_ddr_lease_and_log_is_limited(self):
        source=(ROOT/'build/src/ps/data_transfer/sdio_bridge.c').read_text(encoding='utf-8')
        self.assertLess(source.index('frame_store_acquire('),source.index('frame_crc32(0,frame.data,frame.bytes)'))
        self.assertLess(source.index('frame_crc32(0,frame.data,frame.bytes)'),source.index('write32(BR_CRC,frame_crc)'))
        self.assertIn('if(now>=frame_log_at)',source)
        self.assertIn('frame_log_at=now+1000',source)
        self.assertIn('if(result==BR_DONE)',source)
        crc=(ROOT/'build/src/ps/data_transfer/frame_crc.c').read_text(encoding='utf-8')
        self.assertIn('__builtin_assume_aligned',crc)
        self.assertIn('optimize("O3")',crc)
        self.assertNotIn('Xil_DCacheEnable',crc)

    def test_sender_uses_bounded_partial_budget_not_a_lower_reserve(self):
        source=(ROOT/'esp32c61/main/psram_udp.c').read_text(encoding='utf-8')
        self.assertIn('tcp_memory_write_limit(',source)
        self.assertIn('n,CONFIG_PSRAM_TCP_HEAP_HEADROOM,TCP_MSS)',source)
        self.assertIn('send(ctx->socket,data,allowed,0)',source)
        self.assertIn('status.tcp_queue_us=',source)
        self.assertIn('status.tcp_idle_us=',source)

if __name__=='__main__':unittest.main()
