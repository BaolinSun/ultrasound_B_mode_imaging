import sys
import struct
import unittest
import zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pc_tools'))
import sdio_tcp_receiver as tcp
import sdio_udp_receiver as udp

class DynamicFrames(unittest.TestCase):
    def test_five_lengths_udp_reordering_and_tcp_header(self):
        receiver=udp.FrameReceiver(check_pattern=True)
        for frame,n in enumerate((787456,394240,197632,99328,50176),1):
            data=udp.test_pattern(n)
            head=tcp.HEADER.pack(tcp.MAGIC,1,24,0,123,frame,n,zlib.crc32(data))
            self.assertEqual(tcp.parse_header(head)[2],n)
            with self.assertRaises(ValueError): tcp.parse_header(head,n+1)
            complete=None
            for offset in reversed(range(0,n,1440)):
                _,out=receiver.feed(udp.encode_packet(data,123,frame,offset),('127.0.0.1',1),frame)
                if out: complete=out
            self.assertEqual(bytes(complete[1]),data)
        self.assertEqual(receiver.valid_frames,5)
        self.assertEqual(receiver.valid_bytes,sum((787456,394240,197632,99328,50176)))
    def test_bad_length_and_txt_tail(self):
        for n in (0,1048577):
            with self.assertRaises(ValueError):
                tcp.parse_header(tcp.HEADER.pack(tcp.MAGIC,1,24,0,1,1,n,0))
        from io import StringIO
        out=StringIO()
        udp.write_hex_words(out,struct.pack('<3H',1,2,65535),2)
        self.assertEqual(out.getvalue(),'0001\n0002\nFFFF\n')

if __name__=='__main__': unittest.main()
