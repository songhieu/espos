#!/usr/bin/env python3
"""The store catalog's paid marks (docs/lock.md, approved 2026-11-19): "paid": true on the paid packages,
"free_ids" on a package with a free part. The firmware only follows these marks; nothing is hard-coded on the device.

  tools/paid_marks.py CATALOG [--out FILE] [--off]

  CATALOG   apps-s3/catalog.json as it will be published (build-pkgs/espos/s3/catalog.json after `i18n.py catalog`)
  --out     write there instead of in place
  --off     remove the marks again (a rollback)

Publish ONLY after the server's GET /v1/device/pkg-link is live: firmware that knows the marks fetches every part of
a paid package through it. Firmware 0.5.6 ignores the marks and keeps using the public "url" while it exists.
The parts' private storage "key" (admin-users' contract) is added by whoever uploads the private copies; this script
leaves "url" and "key" alone."""
import json
import sys
from pathlib import Path

FREE_DOGS = {"bao", "momo"}
FREE_MAPS = {"bazaar"}
TANK = "vn.pawboop.tank"
TANK_FREE = ["planted", "guppy", "molly", "swordtail", "neon", "cory", "snail"]


def mark(p, off):
    pid = p["id"]
    want = None  # None: free; [] paid as a whole; [ids]: paid with a free part
    if pid.startswith("vn.pawboop.dog.") and pid.split(".")[-1] not in FREE_DOGS:
        want = []
    elif pid.startswith("vn.pawboop.map.") and pid.split(".")[-1] not in FREE_MAPS:
        want = []
    elif pid == TANK:
        want = TANK_FREE
    before = (p.get("paid"), p.get("free_ids"))
    p.pop("paid", None)
    p.pop("free_ids", None)
    if want is not None and not off:
        p["paid"] = True
        if want:
            p["free_ids"] = want
    return before != (p.get("paid"), p.get("free_ids"))


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("-"):
        sys.exit(__doc__)
    src = Path(args[0])
    out = Path(args[args.index("--out") + 1]) if "--out" in args else src
    off = "--off" in args
    cat = json.loads(src.read_text())
    changed = [p["id"] for p in cat["packages"] if mark(p, off)]
    out.write_text(json.dumps(cat, ensure_ascii=False, indent=1))
    paid = [p["id"] + (" (free: " + ", ".join(p["free_ids"]) + ")" if p.get("free_ids") else "")
            for p in cat["packages"] if p.get("paid")]
    print(f"{out}: {len(changed)} packages changed; paid now ({len(paid)}):")
    for x in paid:
        print("  " + x)


if __name__ == "__main__":
    main()
