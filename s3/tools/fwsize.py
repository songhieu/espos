#!/usr/bin/env python3
"""The size check of an S3 firmware image against its partition table's app slot (docs/partitions.md).

  python3 tools/fwsize.py [firmware.bin] [--track v1|v2]    (default: .pio/build/s3desk/firmware.bin, track from
                                                              the build's partitions.bin next to it)
Prints the size, the slot of each table and the headroom; exit 1 when the image is over its track's budget.
Budgets keep a margin under the slot for the fixes that follow a release:
  v1  3,890,000 B (slot 3,932,160 = 0x3C0000; env s3desk, partitions_pawboop.csv)
  v2  4,920,000 B (slot 4,980,736 = 0x4C0000; env s3desk-v2, partitions_v2.csv)
An image under the v1 budget reaches every board by OTA; a bigger one only boards on v2 (the server offers by size).
"""
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TRACKS = {"v1": (3_932_160, 3_890_000), "v2": (4_980_736, 4_920_000)}


def table_of(partitions_bin):
    """v1 / v2 from a binary partition table: v2 has one data partition named "apps"."""
    names = []
    b = partitions_bin.read_bytes()
    for i in range(0, len(b) - 31, 32):
        if b[i : i + 2] != b"\xaa\x50":
            break
        names.append(struct.unpack_from("<16s", b, i + 12)[0].rstrip(b"\0").decode())
    return "v2" if "apps" in names else "v1"


def main():
    a = argparse.ArgumentParser()
    a.add_argument("bin", nargs="?", default=str(ROOT / ".pio/build/s3desk/firmware.bin"))
    a.add_argument("--track", choices=list(TRACKS))
    o = a.parse_args()
    fw = Path(o.bin)
    n = fw.stat().st_size
    part = fw.parent / "partitions.bin"
    track = o.track or (table_of(part) if part.exists() else "v1")
    slot, budget = TRACKS[track]
    print(f"{fw}: {n:,} B, track {track}")
    for t, (s, b) in TRACKS.items():
        print(f"  {t}: slot {s:,} B ({s - n:+,}), budget {b:,} B ({b - n:+,})")
    print(f"  OTA reaches: {'every board (v1 and v2)' if n <= TRACKS['v1'][1] else 'boards on table v2 only'}")
    if n > budget:
        print(f"OVER the {track} budget by {n - budget:,} B", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
