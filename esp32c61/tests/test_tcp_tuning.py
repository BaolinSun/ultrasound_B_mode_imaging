from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]

class TuningTests(unittest.TestCase):
    def test_tiers_only_change_tx_capacity_and_stage_not_rx_or_protocol(self):
        for name,capacity in (('baseline',5760),('mid',11520),('fast',23040),('faster',28800),
                              ('large',46080),('xlarge',57600)):
            text=(ROOT/f'sdkconfig.tcp-{name}.defaults').read_text()
            values={k:int(v) for k,v in re.findall(r'^(CONFIG_\w+)=(\d+)$',text,re.M)}
            self.assertEqual(values,{'CONFIG_LWIP_TCP_SND_BUF_DEFAULT':5760,
                                     'CONFIG_PSRAM_TCP_SEND_BYTES':capacity,
                                     'CONFIG_PSRAM_TCP_STAGE_BYTES':5760})
            self.assertEqual(capacity%1440,0)
            self.assertIn("'"+name+"'",(ROOT/'build.ps1').read_text())

    def test_performance_default_uses_isolated_tuning_dirs(self):
        script=(ROOT/'build.ps1').read_text()
        self.assertIn("elseif ($Profile -eq 'throughput') { 'fast' }",script)
        self.assertIn('build-$Profile-$resolvedTuning-socket',script)
        self.assertIn('build-$Profile-$Transport-$resolvedTuning-socket',script)
        header=(ROOT/'main/wifi_transport_config.h').read_text()
        self.assertIn('#define WIFI_FRAME_TRANSPORT WIFI_FRAME_TCP',header)

    def test_socket_tuning_precedes_connect_and_is_worker_owned(self):
        source=(ROOT/'main/psram_udp.c').read_text()
        self.assertLess(source.index('tcp_upload_set_send_buffer(tcp_socket'),
                        source.index('int r=connect(tcp_socket'))
        adapter=(ROOT/'main/tcp_socket_tuning.c').read_text()
        self.assertIn('tcpip_api_call(tune_in_core',adapter)
        self.assertIn('pcb->state != CLOSED',adapter)
        self.assertNotIn('TCP_SNDBUF,',source)
        cmake=(ROOT/'main/CMakeLists.txt').read_text()
        self.assertIn('target_compile_definitions(${lwip_lib} PRIVATE TCP_SND_QUEUELEN=${upload_queue})',cmake)
        self.assertIn('target_compile_definitions(${COMPONENT_LIB} PRIVATE TCP_SND_QUEUELEN=${upload_queue})',cmake)

if __name__=='__main__':unittest.main()
