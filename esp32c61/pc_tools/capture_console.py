"""Receive-only bounded USB console capture. Does not send monitor commands/reset."""
import argparse
from pathlib import Path
import time
import serial
from log_paths import LOG_HELP, resolve_log_path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',required=True)
    p.add_argument('--seconds',type=float,default=120)
    p.add_argument('--log',type=Path,required=True,help=LOG_HELP)
    a=p.parse_args()
    if not 1<=a.seconds<=3600:p.error('seconds must be 1..3600')
    a.log=resolve_log_path(a.log)
    a.log.parent.mkdir(parents=True,exist_ok=True)
    # Set neutral control levels before opening; never pulse reset/boot lines.
    console=serial.Serial(port=None,baudrate=115200,timeout=.1)
    console.dtr=False;console.rts=False;console.port=a.port
    with a.log.open('xb') as output:
        console.open()
        try:
            print('Console capture ready (receive only)',flush=True)
            until=time.monotonic()+a.seconds
            while time.monotonic()<until:
                data=console.read(4096)
                if data:output.write(data);output.flush()
        finally:console.close()
    return 0

if __name__=='__main__':raise SystemExit(main())
