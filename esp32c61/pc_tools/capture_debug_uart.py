"""Bounded passive COM capture for a user-authorized debug session; no reset/TX."""
import argparse
from pathlib import Path
import time
import serial

p = argparse.ArgumentParser()
p.add_argument("--port", required=True)
p.add_argument("--seconds", type=float, default=20)
p.add_argument("--output", required=True)
a = p.parse_args()
s = serial.Serial()
s.port = a.port
s.baudrate = 115200
s.timeout = 0.1
s.dtr = False
s.rts = False
s.open()
pending = bytearray()
start = time.monotonic()
with Path(a.output).open("w", encoding="utf-8") as log:
    print("UART_CAPTURE_READY", flush=True)
    while time.monotonic()-start < a.seconds:
        pending.extend(s.read(4096))
        while b"\n" in pending:
            line, _, pending = pending.partition(b"\n")
            text = line.decode("utf-8", errors="replace").rstrip("\r")
            # Exclude Wi-Fi/AP credentials from debug output.
            if "[ZYNQ]" in text or "sdio_bridge" in text or "zynq_uart" in text:
                log.write(text+"\n")
                log.flush()
                print(text, flush=True)
s.close()
