#!/usr/bin/env python3
"""A fake EspOS dev console on a pseudo-terminal, for the device tests (no hardware is opened).
Prints the pty path on the first line, then answers like src/devtools.cpp until stdin closes.
  fake_s3.py [--kind s3|x4|none] [--log FILE]   x4: READY without "kind" (the X4 reader); none: never answers"""
import os
import pty
import select
import sys
import tty

kind = "s3"
log = None
args = sys.argv[1:]
if "--kind" in args:
    kind = args[args.index("--kind") + 1]
if "--log" in args:
    log = open(args[args.index("--log") + 1], "a")

master, slave = pty.openpty()
tty.setraw(slave)
print(os.ttyname(slave), flush=True)


def send(b):
    os.write(master, b)


def answer(line):
    if log:
        log.write(line + "\n")
        log.flush()
    if line.startswith("ESPOS:HELLO"):
        if kind == "s3":
            send(b'ESPOS:READY {"fw":"espos-s3-0.4.0","kind":"s3","board":"ES3C35P","linked":false}\r\n')
        elif kind == "x4":
            send(b'ESPOS:READY {"fw":"espos-0.3.5","linked":true}\r\n')
    elif line == "SHOT":
        w, h = 4, 2
        send(b"sysmon fps 30\r\n")
        send(b"SHOT %d %d 2\n" % (w, h))
        px = b""
        for i in range(w * h):
            v = 0xF800 if i % 2 == 0 else 0x07E0  # red, green
            px += bytes([v & 255, v >> 8])
        send(px + b"\n")
        top = bytes([0, 0, 255, 255]) + b"\0" * (4 * (w * h - 1))  # first pixel: opaque red (BGRA)
        send(b"TOP %d %d 4\n" % (w, h) + top + b"\n")
        send(b"ENDSHOT\r\n")
    elif line == "MEM":
        send(b"MEM internal 112 KB (largest 60 KB, min 80 KB), psram 5000 KB, lvgl used 300 KB of 1280 KB, frag 3%\r\n")
    elif line == "TRACE":
        for i in range(3):
            send(b"[  %d] trace line %d\r\n" % (1000 + i, i))
    elif line.startswith("APP "):
        send(("[  2100] app open %s\r\n" % line[4:]).encode())
    elif line.startswith("JS "):
        send(b"JS 2 (120 us)\r\n")


buf = b""
while True:
    r, _, _ = select.select([master, sys.stdin], [], [], 0.5)
    if sys.stdin in r and not sys.stdin.readline():
        break
    if master in r:
        try:
            data = os.read(master, 4096)
        except OSError:
            continue
        buf += data
        while b"\n" in buf:
            ln, buf = buf.split(b"\n", 1)
            answer(ln.decode("utf-8", "replace").strip())
