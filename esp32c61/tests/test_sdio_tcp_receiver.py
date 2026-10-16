import pathlib
import socket
import sys
import threading
import time
import types
import subprocess
import tempfile
import json
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'pc_tools'))
import sdio_tcp_receiver as t

class Writer:
    def __init__(self): self.events=[];self.outputs=[]
    def submit(self,event,complete=None):
        self.events.append(event)
        if complete:self.outputs.append(complete)

def header(data,frame=1,crc=None):
    return t.HEADER.pack(t.MAGIC,1,24,0,7,frame,len(data),zlib.crc32(data) if crc is None else crc)

class ReceiverTests(unittest.TestCase):
    def make_server(self,**overrides):
        args=types.SimpleNamespace(length=8193,test_pattern=True,source_ip='127.0.0.1',
            seconds=3,idle_timeout=3,frame_timeout=1,frames=1,output_dir=True)
        for k,v in overrides.items():setattr(args,k,v)
        writer=Writer();rx=t.TCPReceiver(args,writer)
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(.1)
        thread=threading.Thread(target=rx.serve,args=(listener,));thread.start()
        self.addCleanup(lambda: self.cleanup(rx,thread,listener))
        return rx,writer,listener.getsockname(),thread

    @staticmethod
    def cleanup(rx,thread,listener):
        rx.stop.set();thread.join(1);listener.close()

    @staticmethod
    def ack(conn): return bytes(t.read_exact(conn,20,time.monotonic()+2,threading.Event()))

    def test_strict_header_and_network_ack(self):
        data=t.test_pattern(8193)
        self.assertEqual(t.parse_header(header(data),8193),(7,1,8193,zlib.crc32(data)))
        for field,value in ((0,0),(1,2),(2,32),(3,1),(4,0),(5,0),(6,0),(6,1048577),(6,787456)):
            values=list(t.HEADER.unpack(header(data)));values[field]=value
            with self.assertRaises(ValueError):t.parse_header(t.HEADER.pack(*values),8193)
        with self.assertRaises(ValueError):t.parse_header(b'UBST')
        self.assertEqual(t.encode_ack(7,1,123),b'UBSA\x01\x14\x00\x00'+(7).to_bytes(4,'big')+(1).to_bytes(4,'big')+(123).to_bytes(4,'big'))

    def test_split_header_payload_and_persistent_frames(self):
        rx,w,address,thread=self.make_server(frames=2)
        data=t.test_pattern(8193)
        with socket.create_connection(address) as conn:
            conn.settimeout(.2)
            for byte in header(data):conn.sendall(bytes([byte]))
            for off in range(0,len(data),73):conn.sendall(data[off:off+73])
            self.assertEqual(self.ack(conn),t.encode_ack(7,1,zlib.crc32(data)))
            # Combined header/body read and the same TCP connection.
            conn.sendall(header(data,2)+data)
            self.assertEqual(self.ack(conn),t.encode_ack(7,2,zlib.crc32(data)))
        thread.join(1)
        self.assertFalse(thread.is_alive());self.assertEqual(rx.connections,1)
        self.assertTrue(rx.summary()['passed']);self.assertEqual(rx.valid_frames,2)
        self.assertEqual([bytes(item[1]) for item in w.outputs],[data,data])

    def test_crc_pattern_rejection_then_manual_new_connection(self):
        for pattern_error in (False,True):
            rx,w,address,thread=self.make_server()
            data=bytes(8193);crc=zlib.crc32(data) if pattern_error else 0
            with socket.create_connection(address) as conn:
                conn.settimeout(.2);conn.sendall(header(data,crc=crc)+data)
                ack=t.ACK.unpack(self.ack(conn));self.assertEqual(ack[3],2 if pattern_error else 1)
            good=t.test_pattern(8193)
            with socket.create_connection(address) as conn:
                conn.settimeout(.2);conn.sendall(header(good,2)+good);self.ack(conn)
            thread.join(1)
            self.assertEqual(rx.valid_frames,1);self.assertEqual(rx.integrity_errors,1)
            self.assertEqual(len(w.outputs),1);self.assertFalse(rx.summary()['passed'])

    def test_partial_frame_discard_and_next_frame(self):
        rx,w,address,thread=self.make_server()
        data=t.test_pattern(8193)
        with socket.create_connection(address) as conn:conn.sendall(header(data)+data[:100])
        with socket.create_connection(address) as conn:
            conn.settimeout(.2);conn.sendall(header(data,2)+data);self.ack(conn)
        thread.join(1);self.assertEqual(rx.incomplete_errors,1)
        self.assertEqual(rx.dropped_frames,1);self.assertEqual(len(w.outputs),1)
        self.assertFalse(rx.summary()['passed'])

    def test_invalid_header_closes_without_payload_scan(self):
        rx,w,address,thread=self.make_server()
        data=t.test_pattern(8193)
        with socket.create_connection(address) as conn:
            conn.settimeout(1);bad=bytearray(header(data));bad[0]=0
            conn.sendall(bad+header(data)+data)
            try:self.assertEqual(conn.recv(20),b'')
            except ConnectionResetError:pass
        rx.stop.set();thread.join(1)
        self.assertEqual(rx.invalid_headers,1);self.assertEqual(rx.valid_frames,0);self.assertFalse(w.outputs)

    def test_stop_partial_and_source_filter(self):
        rx,w,address,thread=self.make_server(frames=0)
        data=t.test_pattern(8193)
        with socket.create_connection(address) as conn:
            conn.sendall(header(data)+data[:100]);time.sleep(.05);rx.stop.set();thread.join(1)
        self.assertEqual(rx.incomplete_errors,0);self.assertEqual(rx.dropped_frames,1)
        self.assertFalse(w.outputs)
        rx,w,address,thread=self.make_server(source_ip='192.0.2.1')
        with socket.create_connection(address) as conn:
            conn.settimeout(1);self.assertEqual(conn.recv(1),b'')
        rx.stop.set();thread.join(1);self.assertEqual(rx.ignored_connections,1)

    def test_exact_read_deadline_stop_and_eof(self):
        for mode in ('timeout','stop','eof'):
            a,b=socket.socketpair()
            try:
                a.settimeout(.01);stop=threading.Event()
                if mode=='stop':stop.set()
                if mode=='eof':b.close()
                with self.assertRaises(t.ReadFailure) as exc:t.read_exact(a,10,time.monotonic()+.02,stop)
                self.assertEqual(exc.exception.received,0)
                self.assertIn({'timeout':'timeout','stop':'stopped','eof':'disconnected'}[mode],exc.exception.reason)
            finally:a.close();b.close()

    def test_planned_device_cancellation_is_discard_not_connection_fault(self):
        rx,w,address,thread=self.make_server(frames=0)
        data=t.test_pattern(8193)
        with socket.create_connection(address) as conn:
            conn.settimeout(.2);conn.sendall(header(data)+data);self.ack(conn)
            rx.quiescing.set();conn.sendall(header(data,2)+data[:100])
        time.sleep(.1);rx.stop.set();thread.join(1)
        self.assertEqual(rx.dropped_frames,1);self.assertEqual(rx.incomplete_errors,0)
        self.assertTrue(rx.summary()['passed']);self.assertEqual(len(w.outputs),1)

    def test_actual_cli_ack_and_only_valid_output(self):
        for corrupt in (False,True):
            with tempfile.TemporaryDirectory() as folder:
                out=pathlib.Path(folder);log=out/'run.jsonl'
                with socket.socket() as reserved:
                    reserved.bind(('127.0.0.1',0));port=reserved.getsockname()[1]
                proc=subprocess.Popen([sys.executable,t.__file__,'--bind','127.0.0.1','--port',str(port),
                    '--length','8193','--word-bytes','1','--test-pattern','--seconds','1','--output-dir',folder,'--log',str(log)],
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                try:
                    self.assertIn('Listening UBST TCP',proc.stdout.readline())
                    data=bytes(8193) if corrupt else t.test_pattern(8193)
                    with socket.create_connection(('127.0.0.1',port)) as conn:
                        conn.settimeout(.2);conn.sendall(header(data)+data)
                        self.assertEqual(t.ACK.unpack(self.ack(conn))[3],2 if corrupt else 0)
                    stdout,stderr=proc.communicate(timeout=4)
                    self.assertEqual(proc.returncode,1 if corrupt else 0,stderr+stdout)
                    files=list(out.glob('frame_*.txt'));self.assertEqual(len(files),0 if corrupt else 1)
                    if files:self.assertEqual(bytes.fromhex(files[0].read_text()),data)
                    self.assertFalse(list(out.glob('*.bin')))
                    summary=json.loads(log.read_text().splitlines()[-1]);self.assertEqual(summary['passed'],not corrupt)
                finally:
                    if proc.poll() is None:proc.kill();proc.wait()

if __name__=='__main__':unittest.main()
