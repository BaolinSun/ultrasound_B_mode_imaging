from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]

class TuningTests(unittest.TestCase):
    def test_tiers_only_change_tx_capacity_and_stage_not_rx_or_protocol(self):
        for name,capacity in (('baseline',5760),('mid',11520),('fast',23040)):
            text=(ROOT/f'sdkconfig.tcp-{name}.defaults').read_text()
            values={k:int(v) for k,v in re.findall(r'^(CONFIG_\w+)=(\d+)$',text,re.M)}
            self.assertEqual(values,{'CONFIG_LWIP_TCP_SND_BUF_DEFAULT':capacity,
                                     'CONFIG_PSRAM_TCP_STAGE_BYTES':5760})
            self.assertEqual(capacity%1440,0)

    def test_balanced_default_uses_isolated_tuning_dirs(self):
        script=(ROOT/'build.ps1').read_text()
        self.assertIn("elseif ($Profile -eq 'throughput') { 'mid' }",script)
        self.assertIn('build-$Profile-$resolvedTuning',script)
        self.assertIn('build-$Profile-$Transport-$resolvedTuning',script)
        header=(ROOT/'main/wifi_transport_config.h').read_text()
        self.assertIn('#define WIFI_FRAME_TRANSPORT WIFI_FRAME_TCP',header)

if __name__=='__main__':unittest.main()
