"""Run a dev-console scenario over one serial session and print the board log meanwhile.
usage: scen.py PORT "TAP 10 20" "WAIT 0.5" "DRAG 280 300 40 300 250" ...
Lines starting with sysmon are hidden."""
import sys
import threading
import time

import serial

port, steps = sys.argv[1], sys.argv[2:]
s = serial.Serial(port, 115200, timeout=0.1)
stop = False
t0 = time.time()


def reader():
    buf = b""
    while not stop:
        buf += s.read(4096)
        while b"\n" in buf:
            ln, buf = buf.split(b"\n", 1)
            t = ln.decode("utf-8", "replace").strip()
            if t and not t.startswith("sysmon") and not t.startswith("heap"):
                print(f"{time.time() - t0:6.2f}   {t}", flush=True)


th = threading.Thread(target=reader, daemon=True)
th.start()
time.sleep(0.3)
for st in steps:
    if st.startswith("WAIT "):
        time.sleep(float(st[5:]))
        continue
    print(f"{time.time() - t0:6.2f} > {st}", flush=True)
    s.write((st + "\n").encode())
    time.sleep(0.6 if st.startswith(("DRAG", "PANEL")) else 0.3)
time.sleep(1.0)
stop = True
th.join()
s.close()
