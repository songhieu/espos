"""Talk to the s3desk dev console over USB serial.

python s3cli.py PORT hello                 set the board clock from this computer
python s3cli.py PORT shot out.png          screenshot (screen + top layer composited)
python s3cli.py PORT tap X Y
python s3cli.py PORT drag X0 Y0 X1 Y1 MS
python s3cli.py PORT log SECONDS           print the board's log
python s3cli.py PORT touchlog 0|1
"""
import struct
import subprocess
import sys
import tempfile
import time

import serial


def open_port(port):
    s = serial.Serial(port, 115200, timeout=2)
    time.sleep(0.2)
    s.reset_input_buffer()
    return s


def read_line(s):
    return s.readline().decode("utf-8", "replace").strip()


def read_image(s, tag):
    while True:
        ln = read_line(s)
        if ln.startswith(tag + " "):
            break
        if not ln and s.in_waiting == 0:
            raise SystemExit(f"no {tag} header")
    parts = ln.split()
    w, h = int(parts[1]), int(parts[2])
    bpp = int(parts[3]) if len(parts) > 3 else 0
    if not w:
        return None
    data = s.read(w * h * bpp)
    s.read(1)
    return w, h, bpp, data


def shot(port, out):
    s = open_port(port)
    s.timeout = 10
    s.write(b"SHOT\n")
    scr = read_image(s, "SHOT")
    top = read_image(s, "TOP")
    s.close()
    w, h, _, d = scr
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        v = d[2 * i] | (d[2 * i + 1] << 8)
        rgb[3 * i] = ((v >> 11) & 31) * 255 // 31
        rgb[3 * i + 1] = ((v >> 5) & 63) * 255 // 63
        rgb[3 * i + 2] = (v & 31) * 255 // 31
    if top:
        tw, th, _, t = top
        for i in range(min(w * h, tw * th)):
            b, g, r, a = t[4 * i], t[4 * i + 1], t[4 * i + 2], t[4 * i + 3]
            if a:
                for k, c in enumerate((r, g, b)):
                    rgb[3 * i + k] = (c * a + rgb[3 * i + k] * (255 - a)) // 255
    with tempfile.NamedTemporaryFile(suffix=".ppm", delete=False) as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h) + bytes(rgb))
        ppm = f.name
    subprocess.run(["magick", ppm, out], check=True)
    print(out, w, h)


def main():
    port, cmd = sys.argv[1], sys.argv[2]
    if cmd == "shot":
        return shot(port, sys.argv[3])
    s = open_port(port)
    if cmd == "hello":
        s.write(b"ESPOS:HELLO %d\n" % int(time.time() * 1000))
        end = time.time() + 3
        while time.time() < end:
            ln = read_line(s)
            if ln.startswith("ESPOS:"):
                print(ln)
                break
    elif cmd == "tap":
        s.write(f"TAP {sys.argv[3]} {sys.argv[4]}\n".encode())
    elif cmd == "drag":
        s.write(("DRAG " + " ".join(sys.argv[3:8]) + "\n").encode())
    elif cmd == "touchlog":
        s.write(f"TOUCHLOG {sys.argv[3]}\n".encode())
    elif cmd == "log":
        end = time.time() + float(sys.argv[3])
        while time.time() < end:
            ln = read_line(s)
            if ln:
                print(ln, flush=True)
    s.close()


main()
