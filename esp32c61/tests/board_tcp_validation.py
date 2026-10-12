"""Explicit, fixed-DDR board acceptance. Requires authorized board execution.
Changes only PC target port (restored in finally), never imaging/HV/Zynq output.
"""
import argparse
import json
from pathlib import Path
import socket
import sys
import threading
import time
import urllib.request
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pc_tools'))
import sdio_tcp_receiver as tcp

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--device-ip',required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    def api(path,body=None,method=None):
        req=urllib.request.Request('http://'+args.device_ip+path,
            data=json.dumps(body).encode() if body is not None else None,
            headers={'Content-Type':'application/json'},method=method)
        with urllib.request.urlopen(req,timeout=5) as r:return json.load(r)
    original=api('/api/config');before=api('/api/status');results=[];failure=None
    assert before['sdio_transport']=='tcp' and not before['busy'] and not before['sdio']['running']
    assert before['sdio']['host_sdio'] and before['sdio']['free_slots']==2
    def target(port):
        api('/api/config',{'ssid':original['ssid'],'target_ip':original['target_ip'],'target_port':port},'PUT')
    def listener():
        s=socket.socket();s.bind(('0.0.0.0',0));s.listen(1);s.settimeout(6);return s
    def idle(timeout=8):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            s=api('/api/status')
            if not s['busy'] and not s['sdio']['running'] and s['sdio']['free_slots']==2 and s['sdio']['host_sdio']:
                return s
            time.sleep(.1)
        raise AssertionError('device did not return to idle/handshaken')
    expected=tcp.test_pattern(787456)
    def frame(conn):
        conn.settimeout(.1);stop=threading.Event();end=time.monotonic()+30
        session,fid,n,crc=tcp.parse_header(tcp.read_exact(conn,24,end,stop))
        data=tcp.read_exact(conn,n,end,stop)
        assert data==expected and zlib.crc32(data)==crc
        return session,fid,crc,bytes(data)
    def record(name,details=None):
        s=idle();results.append({'case':name,'passed':True,'details':details,'status':s})
        print(name+': PASS',flush=True)
    try:
        # Single frame: ACK-gated ownership and callback accounting.
        with listener() as server:
            target(server.getsockname()[1]);api('/api/sdio/start',{'frames':1})
            with server.accept()[0] as conn:
                session,fid,crc,data=frame(conn)
                held=api('/api/status');assert held['busy'] and held['sdio']['sent_frames']==before['sdio']['sent_frames']
                assert held['sdio']['free_slots']==1
                for byte in tcp.encode_ack(session,fid,crc):
                    conn.sendall(bytes([byte]));time.sleep(.002)
            args.output.with_suffix('.frame.bin').write_bytes(data)
        record('single_frame_ack_ownership',{'crc32':f'{crc:08x}','bytes':len(data)})
        api('/api/sdio/stop',{});idle()
        # Change target during a persistent connection, after receive but before
        # ACK, so the next submitted frame must use the new destination.
        with listener() as old,listener() as new:
            target(old.getsockname()[1]);api('/api/sdio/start',{'frames':0})
            with old.accept()[0] as c1:
                a=frame(c1);target(new.getsockname()[1]);c1.sendall(tcp.encode_ack(*a[:3]))
                with new.accept()[0] as c2:
                    b=frame(c2);assert b[0]==a[0] and b[1]>a[1];c2.sendall(tcp.encode_ack(*b[:3]))
                    api('/api/sdio/stop',{})
        record('next_frame_target_port_switch',{'first':a[:3],'next':b[:3]})
        # PC validation rejection must stop, not replay/reconnect.
        with listener() as server:
            target(server.getsockname()[1]);api('/api/sdio/start',{'frames':0})
            with server.accept()[0] as conn:
                a=frame(conn);conn.sendall(tcp.encode_ack(a[0],a[1],a[2],1))
        s=idle();assert s['tcp_failed'] and s['tcp_error_reason']=='pc_rejected'
        record('negative_crc_ack_stops_test')
        with listener() as server:
            target(server.getsockname()[1]);api('/api/sdio/start',{'frames':0})
            with server.accept()[0] as conn:
                a=frame(conn);conn.sendall(tcp.encode_ack(a[0]^1,a[1],a[2]))
        s=idle();assert s['tcp_failed'] and s['tcp_error_reason']=='invalid_ack'
        record('wrong_session_ack_stops_test')
        # Full payload without an application ACK is NOT a completed frame.
        with listener() as server:
            target(server.getsockname()[1]);api('/api/sdio/start',{'frames':1})
            started=time.monotonic()
            with server.accept()[0] as conn:
                frame(conn);s=idle(35)
                assert s['tcp_failed'] and s['result']=='timeout' and s['tcp_error_reason']=='ack_timeout'
            assert 28<time.monotonic()-started<35
        record('shared_30s_send_ack_deadline')
        # Exit mid-payload. Discard the incomplete frame and release both slots.
        with listener() as server:
            target(server.getsockname()[1]);api('/api/sdio/start',{'frames':0})
            with server.accept()[0] as conn:
                conn.settimeout(2);assert conn.recv(24)
                conn.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,__import__('struct').pack('hh',1,0))
        s=idle();assert s['tcp_failed'];record('receiver_disconnect_stops_test')
        # No listening server: bounded failure, no endless retries.
        with listener() as server:closed_port=server.getsockname()[1]
        target(closed_port);api('/api/sdio/start',{'frames':0})
        s=idle();assert s['tcp_failed'] and s['tcp_connect_errors']>0;record('connect_refused')
        # Explicit restart after each fatal condition is supported.
        with listener() as server:
            target(server.getsockname()[1]);api('/api/sdio/start',{'frames':1})
            with server.accept()[0] as conn:
                a=frame(conn);conn.sendall(tcp.encode_ack(*a[:3]))
        s=idle();assert not s['tcp_failed'];record('manual_restart_after_failure')
    except Exception as exc:
        failure={'error':repr(exc),'status_before_cleanup':api('/api/status')}
        raise
    finally:
        api('/api/sdio/stop',{})
        target(original['target_port'])
        after=idle()
        args.output.write_text(json.dumps({'passed':failure is None and len(results)==8,
            'before':before,'cases':results,'failure':failure,'after':after},indent=2),encoding='utf-8')
    return 0

if __name__=='__main__':raise SystemExit(main())
