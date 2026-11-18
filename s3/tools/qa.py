"""Device QA over one serial session: runs steps, records the frame rate (LVGL sysmon), heap,
panics and reboots, and saves screenshots. Prints a summary per step.

usage: qa.py PORT OUTDIR SCRIPT     (script: one step per line, '#' comments)
steps: TAP x y | DRAG x0 y0 x1 y1 ms | APP id [arg] | HOME | WAIT s | SHOT name | MEM | BENCH n
       | LABEL text (names the following steps in the summary)
"""
import re
import subprocess
import sys
import threading
import time

import serial

port, outdir, script = sys.argv[1], sys.argv[2], sys.argv[3]
steps = [ln.strip() for ln in open(script) if ln.strip() and not ln.strip().startswith("#")]
s = serial.Serial(port, 115200, timeout=0.05)
time.sleep(0.2)
s.reset_input_buffer()

lock = threading.Lock()
log = []          # (t, line)
img_wait = None   # set while a SHOT is being read
stop = False
t0 = time.time()
raw = bytearray()


def reader():
    global raw
    while not stop:
        if img_wait is not None:
            time.sleep(0.01)
            continue
        data = s.read(4096)
        if not data:
            continue
        with lock:
            raw += data
            while b"\n" in raw:
                ln, raw = raw.split(b"\n", 1)
                log.append((time.time() - t0, ln.decode("utf-8", "replace").strip()))


th = threading.Thread(target=reader, daemon=True)
th.start()


def read_image(tag):
    buf = b""
    while True:
        c = s.read(1)
        if not c:
            raise RuntimeError("no " + tag)
        if c == b"\n":
            ln = buf.decode("utf-8", "replace").strip()
            buf = b""
            if ln.startswith(tag + " "):
                break
            continue
        buf += c
    p = ln.split()
    w, h = int(p[1]), int(p[2])
    bpp = int(p[3]) if len(p) > 3 else 0
    if not w:
        return None
    s.timeout = 10
    data = s.read(w * h * bpp)
    s.read(1)
    s.timeout = 4
    return w, h, bpp, data


def shot(name):
    global img_wait
    img_wait = True
    time.sleep(0.15)
    with lock:
        s.reset_input_buffer()
    s.write(b"SHOT\n")
    s.timeout = 4
    try:
        scr = read_image("SHOT")
        top = read_image("TOP")
    finally:
        s.timeout = 0.05
        img_wait = None
    w, h, _, d = scr
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        v = d[2 * i] | (d[2 * i + 1] << 8)
        rgb[3 * i] = ((v >> 11) & 31) * 255 // 31
        rgb[3 * i + 1] = ((v >> 5) & 63) * 255 // 63
        rgb[3 * i + 2] = (v & 31) * 255 // 31
    if top:
        tw, tht, _, t = top
        for i in range(min(w * h, tw * tht)):
            b, g, r, a = t[4 * i], t[4 * i + 1], t[4 * i + 2], t[4 * i + 3]
            if a:
                for k, c in enumerate((r, g, b)):
                    rgb[3 * i + k] = (c * a + rgb[3 * i + k] * (255 - a)) // 255
    ppm = f"{outdir}/{name}.ppm"
    with open(ppm, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h) + bytes(rgb))
    subprocess.run(["magick", ppm, f"{outdir}/{name}.png"], check=False)


marks = []  # (t, label)
label = "start"
for st in steps:
    if st.startswith("LABEL "):
        label = st[6:]
        marks.append((time.time() - t0, label))
        continue
    if st.startswith("WAIT "):
        time.sleep(float(st[5:]))
        continue
    if st.startswith("SHOT "):
        try:
            shot(st[5:])
        except Exception as e:  # noqa: BLE001
            print("shot failed", st, e)
        continue
    s.write((st + "\n").encode())
    time.sleep(0.9 if st.startswith("DRAG") else 0.5 if st.startswith(("APP", "HOME")) else 0.35)
time.sleep(1.5)
stop = True
th.join()
s.close()

# ---------------------------------------------------------------- summary
fps_re = re.compile(r"sysmon: (\d+) FPS \(refr_cnt: (\d+) \| redraw_cnt: (\d+)\), refr (\d+)ms \(render (\d+)ms \| flush (\d+)ms\), CPU (\d+)%")
marks.append((1e9, "end"))
bad = [ (t, l) for t, l in log if any(k in l for k in ("Guru", "panic", "Backtrace", "abort()", "rst:", "assert failed", "Error:")) ]
with open(f"{outdir}/qa_log.txt", "w") as f:
    for t, l in log:
        f.write(f"{t:7.2f} {l}\n")
for i in range(len(marks) - 1):
    a, name = marks[i]
    b = marks[i + 1][0]
    rows = [fps_re.search(l) for t, l in log if a <= t < b and l.startswith("sysmon")]
    rows = [r for r in rows if r and int(r.group(3)) > 0]
    if not rows:
        print(f"{name:28} (no redraws)")
        continue
    renders = [int(r.group(5)) for r in rows]
    fpsv = [int(r.group(1)) for r in rows]
    busy = [r for r in rows if int(r.group(3)) >= 3]  # windows with continuous redraw
    bf = [int(r.group(1)) for r in busy]
    print(f"{name:28} busy windows {len(busy):2}  fps min {min(bf) if bf else '-':>3} avg {sum(bf)//len(bf) if bf else '-':>3}"
          f"  render max {max(renders):3} ms")
mem = [l for t, l in log if l.startswith("MEM ")]
for m in mem:
    print(m)
print("problems:", len(bad))
for t, l in bad[:20]:
    print(f"  {t:7.2f} {l}")
