import importlib.util
import pathlib
import struct
import sys
import unittest
import zlib
import socket
import subprocess
import tempfile
import json

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pc_tools'))
import sdio_udp_receiver as r

class FramedReceiverTests(unittest.TestCase):
    def setUp(self):
        self.data = r.test_pattern(3000)
        self.rx = r.FrameReceiver(3000, check_pattern=True)
        self.source = ('192.168.3.27', 12345)
    def packets(self, frame=1, session=1, data=None):
        data = self.data if data is None else data
        return [r.encode_packet(data, session, frame, i) for i in range(0, len(data), r.PAYLOAD)]
    def feed(self, packets, now=0):
        events, outputs = [], []
        for i, p in enumerate(packets):
            e, output = self.rx.feed(p, self.source, now+i*.001)
            events += e
            if output: outputs.append(output)
        return events, outputs
    def test_order_and_tail(self):
        packets=self.packets()
        self.assertEqual(len(packets[-1]), 32+120)
        e, out=self.feed(packets)
        self.assertEqual(out[0][1], self.data)
        self.assertEqual(e[-1]['packets'], 3)
    def test_out_of_order_and_identical_duplicate(self):
        p=self.packets()
        e,out=self.feed([p[2],p[2],p[0],p[1]])
        self.assertEqual(len(out),1)
        self.assertEqual(self.rx.duplicates,1)
    def test_missing_first_middle_last_and_recover(self):
        for missing in range(3):
            with self.subTest(missing=missing):
                self.rx=r.FrameReceiver(3000)
                self.feed([p for i,p in enumerate(self.packets()) if i!=missing])
                self.assertEqual(len(self.rx.expire(3)),1)
                _,out=self.feed(self.packets(frame=2),4)
                self.assertEqual(len(out),1)
    def test_conflicting_duplicate(self):
        p=self.packets()
        bad=bytearray(p[0]); bad[-1]^=1
        e,out=self.feed([p[0],bytes(bad),p[1],p[2]])
        self.assertFalse(out)
        self.assertEqual(e[-1]['reason'],'conflicting duplicate')
    def test_corruption(self):
        p=self.packets(); bad=bytearray(p[1]); bad[-1]^=1; p[1]=bytes(bad)
        e,out=self.feed(p)
        self.assertFalse(out)
        self.assertEqual(e[-1]['reason'],'CRC32 mismatch')
    def test_wrong_pattern_with_valid_crc(self):
        e,out=self.feed(self.packets(data=b'x'*3000))
        self.assertFalse(out)
        self.assertEqual(e[-1]['reason'],'test pattern mismatch')
    def test_session_restart_and_old_late_packet(self):
        self.feed(self.packets()[:1])
        e,out=self.feed(self.packets(session=2),1)
        self.assertEqual(e[0]['reason'],'session changed')
        self.assertEqual(len(out),1)
        self.feed(self.packets(session=1),2)
        self.assertEqual(self.rx.session,2)
        self.assertEqual(self.rx.ignored_datagrams,3)
    def test_capacity_is_bounded(self):
        for frame in (1,2,3): self.feed(self.packets(frame=frame)[:1],frame*.01)
        self.assertEqual(len(self.rx.pending),2)
        self.assertEqual(self.rx.dropped_frames,1)
    def test_late_completed_packets_ignored(self):
        self.feed(self.packets())
        self.feed(self.packets(),1)
        self.assertEqual(self.rx.valid_frames,1)
        self.assertFalse(self.rx.pending)
    def test_very_old_frame_ignored_after_tombstone_eviction(self):
        for frame in range(1,260): self.feed(self.packets(frame=frame),frame*.01)
        self.feed(self.packets(frame=1),4)
        self.assertEqual(self.rx.valid_frames,259)
        self.assertFalse(self.rx.pending)
    def test_header_invalid_fields(self):
        original=self.packets()[0]
        for index,value in ((0,0),(1,2),(2,31),(3,1),(4,0),(5,0),(6,0),(7,1),(8,1439),(9,2)):
            values=list(r.HEADER.unpack_from(original)); values[index]=value
            with self.subTest(index=index), self.assertRaises(ValueError):
                r.parse_packet(r.HEADER.pack(*values)+original[32:],3000)
        for p in (b'',original[:-1],original+b'x'):
            with self.assertRaises(ValueError): r.parse_packet(p,3000)
    def test_other_source_ignored(self):
        self.rx.source='192.168.3.27'
        self.rx.feed(self.packets()[0],('192.168.3.99',1),0)
        self.assertEqual(self.rx.ignored_datagrams,1)
    def test_conflicting_metadata(self):
        p=self.packets(); v=list(r.HEADER.unpack_from(p[1])); v[-1]^=1
        e,out=self.feed([p[0],r.HEADER.pack(*v)+p[1][32:]])
        self.assertEqual(e[-1]['reason'],'conflicting metadata')
        self.assertFalse(out)
    def test_full_frame_and_crc_fixture(self):
        data=r.test_pattern(787456)
        self.rx=r.FrameReceiver(787456,check_pattern=True)
        e,out=self.feed(self.packets(data=data))
        self.assertEqual(len(self.packets(data=data)),547)
        self.assertEqual(out[0][1],data)
        self.assertEqual(e[-1]['crc32'],f'{zlib.crc32(data):08x}')
    def test_protocol_copy_matches(self):
        canonical=(ROOT/'protocol/bridge_protocol.h').read_text()
        host=(ROOT.parent/'build/src/ps/data_transfer/bridge_protocol.h').read_text()
        self.assertEqual(canonical,host)
    def test_actual_udp_cli_saves_only_complete_frame(self):
        with tempfile.TemporaryDirectory() as directory, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
                probe.bind(('127.0.0.1',0)); port=probe.getsockname()[1]
            command=[sys.executable,str(ROOT/'pc_tools/sdio_udp_receiver.py'),'--bind','127.0.0.1',
                     '--port',str(port),'--length','3000','--test-pattern','--idle-timeout','2',
                     '--output-dir',directory]
            p=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                self.assertIn('Listening UBSD',p.stdout.readline())
                for packet in reversed(self.packets()): sender.sendto(packet,('127.0.0.1',port))
                stdout,stderr=p.communicate(timeout=5)
                self.assertEqual(p.returncode,0,stderr+stdout)
                summary=json.loads(stdout.splitlines()[-1]); self.assertTrue(summary['passed'])
                files=list(pathlib.Path(directory).glob('*.bin'))
                self.assertEqual(len(files),1);self.assertEqual(files[0].read_bytes(),self.data)
            finally:
                if p.poll() is None: p.kill();p.communicate()
    def test_actual_udp_cli_timeout_never_saves_partial(self):
        with tempfile.TemporaryDirectory() as directory, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
                probe.bind(('127.0.0.1',0)); port=probe.getsockname()[1]
            command=[sys.executable,str(ROOT/'pc_tools/sdio_udp_receiver.py'),'--bind','127.0.0.1',
                     '--port',str(port),'--length','3000','--frame-timeout','0.05','--idle-timeout','0.2',
                     '--output-dir',directory]
            p=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                self.assertIn('Listening UBSD',p.stdout.readline())
                sender.sendto(self.packets()[0],('127.0.0.1',port))
                stdout,stderr=p.communicate(timeout=5)
                self.assertEqual(p.returncode,1,stderr+stdout)
                self.assertFalse(list(pathlib.Path(directory).glob('*.bin')))
                self.assertEqual(json.loads(stdout.splitlines()[-1])['discarded_frames'],1)
            finally:
                if p.poll() is None: p.kill();p.communicate()

if __name__=='__main__': unittest.main()
