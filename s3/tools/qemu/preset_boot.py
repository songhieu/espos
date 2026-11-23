#!/usr/bin/env python3
"""A product's delivery bundle (tools/make_bundle.py --preset) booted in QEMU as a unit off the line
(docs/app-slots.md §17.5): this env's firmware, the bundle's LittleFS (/sys/preset.json) and its card, nothing else.
Headless, on its own port and sockets (it runs beside tools/qemu/run.sh): reads the serial port, asks PRESET, then
takes the first boot's screens: Home, the Control Center (its assistant tile), the saver. Landscape shots are turned
upright (QEMU's screendump is the panel's own portrait raster).

  python3 tools/qemu/preset_boot.py BUNDLE OUT [--board 28] [--port 5581] [--boot 45] [--no-build]
"""
import argparse
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
QEMU = ROOT.parent / "tools-ext/qemu/qemu/bin/qemu-system-xtensa"


def main():
    a = argparse.ArgumentParser()
    a.add_argument("bundle")
    a.add_argument("out")
    a.add_argument("--board", default="35")
    a.add_argument("--port", type=int, default=5581)
    a.add_argument("--boot", type=float, default=45, help="seconds for the first boot")
    a.add_argument("--no-build", action="store_true", help="the QEMU env is built already")
    o = a.parse_args()
    env = "s3desk-28-qemu" if o.board == "28" else "s3desk-qemu"
    out = Path(o.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if not o.no_build:
        subprocess.run([str(Path.home() / ".platformio/penv/bin/pio"), "run", "-e", env, "-j", "4"], cwd=ROOT, check=True,
                       capture_output=True)
    img, sd = out / "flash.bin", out / "sd.img"
    mon = Path(f"/tmp/espos-preset-{o.port}.sock")  # a unix socket's path is short (104 bytes on macOS)
    subprocess.run([sys.executable, str(ROOT / "tools/qemu/mkimage.py"), str(img), "--env", env, "--bundle", o.bundle,
                    "--sd", str(sd)], cwd=ROOT, check=True)
    mon.unlink(missing_ok=True)
    q = subprocess.Popen([str(QEMU), "-machine", "esp32s3", "-m", "8M", "-global", "driver=ssi_psram,property=is_octal,value=true",
                          "-drive", f"file={img},if=mtd,format=raw", "-drive", f"file={sd},if=sd,format=raw",
                          "-nic", "user,model=open_eth", "-monitor", f"unix:{mon},server,nowait", "-display", "none",
                          "-serial", f"tcp:127.0.0.1:{o.port},server,wait"],
                         env={"DYLD_FALLBACK_LIBRARY_PATH": "/opt/homebrew/opt/sdl2/lib", "PATH": "/usr/bin:/bin"},
                         stdout=subprocess.DEVNULL, stderr=open(out / "qemu.txt", "w"))
    log = []
    try:
        for _ in range(100):
            try:
                s = socket.create_connection(("127.0.0.1", o.port))
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise SystemExit(f"no QEMU on port {o.port}")
        s.settimeout(0.05)

        def drain(t):
            end = time.time() + t
            while time.time() < end:
                try:
                    b = s.recv(65536)
                    if not b:
                        return
                    log.append(b.decode("utf-8", "replace"))
                except socket.timeout:
                    pass

        def send(cmd, t=1.0):
            s.sendall((cmd + "\n").encode())
            drain(t)

        def shot(name):
            m = socket.socket(socket.AF_UNIX)
            m.connect(str(mon))
            ppm = out / f"{name}.ppm"
            m.sendall(f"screendump {ppm}\n".encode())
            time.sleep(1.0)
            m.close()
            return ppm

        dev = json.loads((Path(o.bundle) / "preset.json").read_text())
        land = dev["settings"].get("s3.rot", "0") in ("1", "3")
        drain(o.boot)
        send("PRESET", 1.5)
        shots = [shot("01-home")]
        w, h = (480, 320) if land else (320, 480)
        send(f"DRAG {w - 40} 4 {w - 40} {h - 40} 400", 3)
        shots.append(shot("02-control"))
        send(f"DRAG {w // 2} {h - 30} {w // 2} 30 300", 2)
        send("SAVER", 6)
        shots.append(shot("03-saver"))
    finally:
        q.terminate()
        try:
            q.wait(5)
        except subprocess.TimeoutExpired:
            q.kill()
    text = "".join(log)
    (out / "serial.txt").write_text(text)
    try:
        from PIL import Image
        rot = {"1": 270, "3": 90}.get(dev["settings"].get("s3.rot", "0"), 0)
        for p in shots:
            if p.exists():
                im = Image.open(p)
                if rot:
                    im = im.rotate(rot, expand=True)
                im.save(p.with_suffix(".png"))
                p.unlink()
    except ImportError:
        pass
    line = next((l for l in text.splitlines() if l.startswith("ESPOS:PRESET")), "ESPOS:PRESET (no answer)")
    print(f"{Path(o.bundle).name}: {line}")
    for l in text.splitlines():
        if "preset:" in l or "assistant:" in l:
            print("  " + l.strip())


if __name__ == "__main__":
    main()
