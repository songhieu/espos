#!/usr/bin/env python3
"""Over the air: play clips through the Mac's speakers into the Mac's mic that the live simulator listens to, and
count how many fire the wake word (its log's "wake: heard" lines), with the best score of each try.

    python3 wake/tools/airtest.py SIMLOG CLIP.wav [CLIP2.wav ...] [--gap 7] [--volume 0.7]
"""
import argparse
import re
import subprocess
import time


def lines(path):
    with open(path, errors="replace") as f:
        return f.read().splitlines()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("clips", nargs="+")
    ap.add_argument("--gap", type=float, default=7.0)
    ap.add_argument("--volume", type=float, default=0.7)
    a = ap.parse_args()
    hits = 0
    for i, c in enumerate(a.clips):
        n0 = len(lines(a.log))
        subprocess.run(["afplay", "-v", str(a.volume), c])
        time.sleep(a.gap)
        new = lines(a.log)[n0:]
        fired = [l for l in new if "wake: heard" in l]
        best = 0.0
        for l in new:
            m = re.search(r"wake: (?:best|heard \()gau ([0-9.]+),? gau2 ([0-9.]+)", l)
            if m:
                best = max(best, float(m.group(2)))
        hits += bool(fired)
        print(f"{i + 1:2d}  {'HIT ' if fired else 'miss'}  best gau2 {best:.3f}  {c.rsplit('/', 1)[-1]}", flush=True)
    print(f"{hits}/{len(a.clips)} fired")


if __name__ == "__main__":
    main()
