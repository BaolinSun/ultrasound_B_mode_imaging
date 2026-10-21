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

    def test_receive_fuses_copy_crc_without_changing_frame_ownership(self):
        source=(ROOT/'esp32c61/main/sdio_bridge.c').read_text(encoding='utf-8')
        self.assertIn('bridge_crc32_copy(crc,s->data+offset,p,length)',source)
        self.assertNotIn('memcpy(s->data+offset,p,length)',source)
        self.assertLess(source.index('bridge_crc32_copy('),source.index('if(crc!=s->meta.crc32)'))
        self.assertIn('if(crc!=s->meta.crc32)',source)
        crc=(ROOT/'esp32c61/main/bridge_crc.c').read_text(encoding='utf-8')
        self.assertIn('__builtin_assume_aligned(p,4)',crc)
        self.assertIn('memcpy(dest,p,n)',crc)  # ROM/allocation fallback still copies.

    def test_fairness_credit_requires_real_wait_and_retains_byte_budget(self):
        source=(ROOT/'esp32c61/main/tcp_transfer.c').read_text(encoding='utf-8')
        self.assertIn('if(!reading && spent>=1000)',source)
        self.assertIn('since_yield>=TCP_YIELD_BYTES',source)
        self.assertIn('io->yield_ms(io->ctx,1)',source)
        self.assertIn('r=check(io,deadline)',source)

    def test_direct_body_stays_bounded_and_uses_the_same_ack_and_socket_guard(self):
        source=(ROOT/'esp32c61/main/tcp_transfer.c').read_text(encoding='utf-8')
        self.assertIn('const uint8_t *source=data+m->sent',source)
        self.assertIn('#if !TCP_DIRECT_PSRAM',source)
        self.assertIn('exact(io,NULL,source,n,true,0,deadline,m,&since_yield)',source)
        self.assertIn('if(n>TCP_STAGE_BYTES)n=TCP_STAGE_BYTES',source)
        self.assertIn('be32(ack+16)!=frame->crc32',source)
        sender=(ROOT/'esp32c61/main/psram_udp.c').read_text(encoding='utf-8')
        self.assertIn('heap_caps_malloc(TCP_STAGING_ALLOC_BYTES, MALLOC_CAP_INTERNAL',sender)
        self.assertLess(sender.index('result=tcp_frame_send('),sender.index('raw_finish(&session'))

    def test_experiment_defaults_are_orthogonal_and_keep_regular_psram_at_40(self):
        base=ROOT/'esp32c61'
        for name in ('core','direct','combined','psram80'):
            text=(base/f'sdkconfig.experiment-{name}.defaults').read_text()
            self.assertEqual('CONFIG_PSRAM_TCP_DIRECT=y' in text,name!='core')
            self.assertEqual('CONFIG_LWIP_TCPIP_CORE_LOCKING=y' in text,name!='direct')
            self.assertIn('CONFIG_SPIRAM_SPEED_80M=y' if name=='psram80' else 'CONFIG_SPIRAM_SPEED_40M=y',text)
        self.assertIn('CONFIG_SPIRAM_SPEED_40M=y',(base/'sdkconfig.defaults').read_text())

if __name__=='__main__':unittest.main()
