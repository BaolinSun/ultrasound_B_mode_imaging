from pathlib import Path
import re
import unittest
ROOT=Path(__file__).resolve().parents[2]
HOST=ROOT/'build/src/ps/data_transfer/sdio_host.c'

class SdioInitContractTests(unittest.TestCase):
    def test_reset_restores_poll_events(self):
        source=HOST.read_text(encoding='utf-8')
        init=source[source.index('static int sdio_enumerate_card'):]
        self.assertLess(init.index('XSdPs_ResetConfig('),init.index('XSdPs_ConfigInterrupt('))
        self.assertLess(init.index('XSdPs_ConfigInterrupt('),init.index('sdio_cmd(host, CMD0'))
        self.assertIn('sdio_cmd52(host,1u,0u,0x06u,0x08u,NULL)',init)
    def test_no_memory_card_or_long_sdk_wait_path(self):
        source=HOST.read_text(encoding='utf-8')
        self.assertNotRegex(source,r'\b(?:XSdPs_SetupTransfer|XSdPs_SetBlkSize|XSdPs_CmdTransfer|XSdPs_SetupCmd)\s*\(')
        self.assertIn('cfg->BaseAddress!=SDIO_HOST_BASEADDR',source)
    def test_logs_use_32_bit_formats(self):
        for name in ('sdio_host.c','sdio_bridge.c'):
            source=(HOST.parent/name).read_text(encoding='utf-8')
            self.assertNotRegex(source,r'%\d*l[uxd]')
            self.assertEqual(source,(ROOT/'workspace/B_mode_focus_imaging/src/data_transfer'/name).read_text(encoding='utf-8'))
    def test_sdk_frequency_expression_is_parenthesized(self):
        for name in ('sdio_host.c','sdio_bridge.c'):
            source=(HOST.parent/name).read_text(encoding='utf-8')
            self.assertNotRegex(source,r'/\s*(?:\(u64\))?COUNTS_PER_SECOND')
