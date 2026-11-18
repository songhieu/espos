#!/usr/bin/env python3
"""Talks to QEMU's serial port (tools/qemu/run.sh --tcp PORT): prints what the firmware says, sends commands.
  tools/qemu/console.py PORT [--wait SECONDS] [CMD ...]   each CMD a dev console line ("TAP 160 300"), "sleep:2.5"
  between commands waits, "shot:FILE.ppm" saves the screen (QEMU's monitor); with no CMD it reads for --wait
  seconds (default: until Ctrl-C)."""
import socket
import sys
import time


def shot(path):
    from pathlib import Path
    # the 2.8" board's QEMU (run.sh --board 28) listens on 5556 and has its own monitor socket
    name = "build-qemu/monitor-28.sock" if len(sys.argv) > 1 and sys.argv[1] == "5556" else "build-qemu/monitor.sock"
    sock = Path(__file__).resolve().parents[2] / name
    m = socket.socket(socket.AF_UNIX)
    m.connect(str(sock))
    m.sendall(f"screendump {Path(path).resolve()}\n".encode())
    time.sleep(0.5)
    m.close()
    print(f"[shot {path}]")


def main():
    port = int(sys.argv[1])
    args = sys.argv[2:]
    wait = None
    if args[:1] == ["--wait"]:
        wait = float(args[1])
        args = args[2:]
    for _ in range(100):
        try:
            s = socket.create_connection(("127.0.0.1", port))
            break
        except OSError:
            time.sleep(0.1)
    else:
        sys.exit(f"no QEMU on port {port}")
    s.settimeout(0.05)

    def drain(t):
        end = time.time() + t
        while time.time() < end:
            try:
                b = s.recv(65536)
                if not b:
                    return
                sys.stdout.write(b.decode("utf-8", "replace"))
                sys.stdout.flush()
            except socket.timeout:
                pass

    for c in args:
        if c.startswith("sleep:"):
            drain(float(c[6:]))
        elif c.startswith("shot:"):
            shot(c[5:])
        else:
            s.sendall((c + "\n").encode())
            drain(0.3)
    drain(wait if wait is not None else 1e9)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
