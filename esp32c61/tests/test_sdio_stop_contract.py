from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[2]


class StopContract(unittest.TestCase):
    def test_protocol_and_host_sources_match(self):
        protocol = (ROOT/'esp32c61/protocol/bridge_protocol.h').read_bytes()
        for root in ('build/src/ps', 'workspace/B_mode_focus_imaging/src'):
            self.assertEqual(protocol, (ROOT/root/'data_transfer/bridge_protocol.h').read_bytes())
        self.assertIn(b'BR_SDIO_VERSION 4U', protocol)
        self.assertIn(b'BR_STOP_REQ=56, BR_STOP_ACK=57', protocol)
        self.assertEqual((ROOT/'build/src/ps/data_transfer/sdio_bridge.c').read_bytes(),
                         (ROOT/'workspace/B_mode_focus_imaging/src/data_transfer/sdio_bridge.c').read_bytes())

    def test_stop_is_not_reset_and_host_ack_proves_ownership(self):
        source = (ROOT/'esp32c61/main/sdio_bridge.c').read_text()
        body = source.split('static void poll_stop(void)', 1)[1].split('static void publish_window', 1)[0]
        self.assertNotIn('sdio_slave_deinit', body)
        self.assertNotIn('open_driver()', body)
        host = (ROOT/'build/src/ps/data_transfer/sdio_bridge.c').read_text()
        body = host.split('static void acknowledge_stop(void)', 1)[1].split('int sdio_bridge_power_idle', 1)[0]
        self.assertLess(body.index('sdio_host_quiesce'), body.index('write8(BR_STOP_ACK'))
        self.assertLess(body.index('frame_store_release'), body.index('write8(BR_STOP_ACK'))
        self.assertIn('current_session!=session', body)
        self.assertIn('RAW_CANCELLED || !slots[i].cancel_expected', source)

    def test_viewer_and_benchmark_wait_for_stop_ownership(self):
        for filename in ('bmode_viewer_service.py', 'sdio_benchmark.py'):
            source = (ROOT/'esp32c61/pc_tools'/filename).read_text()
            self.assertIn("get('stopping')", source)
            self.assertIn("get('free_slots') == 2", source)
            self.assertRegex(source, r'monotonic\(\)\s*\+\s*7')


if __name__ == '__main__':
    unittest.main()
