"""The dogs' packages with new assistant lines (vn.pawboop.dog.<dog> 1.0.2): the published 1.0.0 package, every file
copied unchanged, and its assist.pba with clips appended from data/pawboop/lock/<dog>.pba (2026-11-19: the AI
trial's "talking is locked" lines: ai_trial_end, ai_locked) and data/pawboop/ack/<dog>.pba (the spoken answer to
"gâu gâu": ack_1..ack_4), both made by ../../wake/tools/lock_lines.py (SET=ack for the second). The old clips keep
their bytes, their order and their loudness; only the new ones were levelled (to the same -16 LUFS).

  python3 tools/pawboop/dog_lines.py [--catalog] [--sim DIR]
    --catalog  build-pkgs/espos/s3/catalog.json again afterwards (tools/i18n.py catalog)
    --sim      install the new packages on the simulator's memory card (tools/espk.py --install-sim)

A firmware that does not know the new moments never asks for them: min_fw stays the 1.0.0 package's.
"""
import argparse
import json
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import espk  # noqa: E402

OUT = ROOT / "build-pkgs/espos/s3"
EXTRA = [ROOT / "data/pawboop/lock", ROOT / "data/pawboop/ack"]  # appended in this order
DOGS = ["bao", "momo", "boop", "leaf", "volt", "byte", "shiba", "corgi", "husky", "golden"]
FROM, TO = "1.0.0", "1.0.2"  # 1.0.1: the lock lines; 1.0.2: + the four spoken acks (moment "ack")


def parse(b):
    """PBA1: (rate, channels, packet_ms, pre_skip, [clip tuples], texts, data)"""
    magic, rate, ch, pms, n, pre, toff, doff = struct.unpack_from("<4sHBBHHII", b, 0)
    assert magic == b"PBA1" and toff == 20 + 48 * n, "not a PBA1 file"
    clips = [struct.unpack_from("<16s16sIIIHH", b, 20 + 48 * i) for i in range(n)]
    return rate, ch, pms, pre, clips, b[toff:doff], b[doff:]


def append(old, new):
    r0, c0, p0, s0, clips0, t0, d0 = parse(old)
    r1, c1, p1, s1, clips1, t1, d1 = parse(new)
    assert (r0, c0, p0, s0) == (r1, c1, p1, s1), f"format differs: {(r0, c0, p0, s0)} vs {(r1, c1, p1, s1)}"
    names = {c[0] for c in clips0}
    assert not any(c[0] in names for c in clips1), "a clip of that name is already in"
    moved = [(n, o, off + len(d0), nb, ns, npk, tx + len(t0)) for n, o, off, nb, ns, npk, tx in clips1]
    clips = clips0 + moved
    head_len = 20 + 48 * len(clips)
    texts = t0 + t1
    head = struct.pack("<4sHBBHHII", b"PBA1", r0, c0, p0, len(clips), s0, head_len, head_len + len(texts))
    tab = b"".join(struct.pack("<16s16sIIIHH", *c) for c in clips)
    out = head + tab + texts + d0 + d1
    # the old clips: the same bytes where the table says
    _, _, _, _, chk, _, data = parse(out)
    for c_old, c_new in zip(clips0, chk):
        assert d0[c_old[2]:c_old[2] + c_old[3]] == data[c_new[2]:c_new[2] + c_new[3]]
    return out, len(clips1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--catalog", action="store_true")
    ap.add_argument("--sim")
    a = ap.parse_args()
    built = []
    for dog in DOGS:
        src = OUT / f"vn.pawboop.dog.{dog}-{FROM}.espk"
        m, files, b = espk.read(src)
        assert m["version"] == FROM
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            added = 0
            for name, off, size, crc in files:
                data = b[off:off + size]
                if name == "assist.pba":
                    for folder in EXTRA:
                        data, n = append(data, (folder / f"{dog}.pba").read_bytes())
                        added += n
                (t / name).parent.mkdir(parents=True, exist_ok=True)
                (t / name).write_bytes(data)
            assert added, f"{dog}: no assist.pba"
            m2 = dict(m, version=TO, code=espk_code(TO))
            (t / "manifest.json").write_text(json.dumps(m2, ensure_ascii=False))
            p = espk.build(t, OUT / f"vn.pawboop.dog.{dog}-{TO}.espk")
        jpg = OUT / f"vn.pawboop.dog.{dog}-{FROM}.jpg"
        if jpg.exists():
            shutil.copyfile(jpg, OUT / f"vn.pawboop.dog.{dog}-{TO}.jpg")
        built.append(p)
    if a.catalog:
        subprocess.run([sys.executable, str(ROOT / "tools/i18n.py"), "catalog"], cwd=ROOT, check=True)
    if a.sim:
        subprocess.run([sys.executable, str(ROOT / "tools/espk.py"), "--install-sim", a.sim] + [str(p) for p in built], check=True)


def espk_code(v):
    x, y, z = (list(map(int, v.split("."))) + [0, 0, 0])[:3]
    return x * 10000 + y * 100 + z


if __name__ == "__main__":
    main()
