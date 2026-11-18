#!/usr/bin/env python3
"""The analog clock faces (docs/widgets.md §3, clock styles): one table, written for the firmware (src/wid/faces.h)
and the web (web/app/src/os/wid/faces.ts) so both draw the same faces. Each face is a few numbers for the one renderer
in src/wid/clock.cpp face() / clock.ts face(). Run from s3/: python3 tools/faces.py
"""
import os

NO = 0x1000000  # a colour that is not drawn (no face fill: the card shows through; hour hand = the minute hand's)

# Fields, in order (clock.cpp struct Look):
#  face, face2      fill, and the fill's lower colour (a vertical gradient; NO = flat)
#  rim, hour        rim colour; the hour hand's colour (NO = the hand colour)
#  tick, minor      hour ticks, minute ticks (and small dots)
#  num, hand        numerals, hands
#  second, cap      the seconds hand (and the station disc), the centre cap
#  rimW             rim width = max(2, size / rimW); 0 none
#  ticks            0 none, 12 or 60 line ticks, 112 or 160 dot ticks
#  tickL, tickW     hour tick length (% of size), width = max(2, size / tickW)
#  inset            ticks (or dots) start this far in from the edge, per mille of size
#  nums, numKind    0, 4 or 12 numerals; 0 Arabic, 1 Roman (four below 170 px), 2 Arabic in the kids' colours
#  numPx, numPxM    numerals at 170 px and up, and at 100..169 px (smaller: 12)
#  numR             numerals' distance from the edge, % of size
#  hB, hL, hW       hour hand: tail and length in % of r, width = max(3, size / hW)
#  mB, mL, mW       minute hand: the same (width at least 2)
#  sL               seconds hand length, % of r
#  handKind         0 round lines, 1 flat bars (and flat ticks)
#  extra            bits: 1 date window at 3, 2 weather at 6, 4 sub-dials at 9 and 3, 8 diver's bezel marks,
#                   16 the station's seconds disc
#  mode             0 the colours above; 1 light/dark (the minimal palette turns dark); 2 the card's text colours
FIELDS = "face face2 rim hour tick minor num hand second cap rimW ticks tickL tickW inset nums numKind numPx numPxM numR hB hL hW mB mL mW sL handKind extra mode".split()
COLOURS = 10

BASE = dict(face=0xffffff, face2=NO, rim=0xd9dbe1, hour=NO, tick=0x16181d, minor=0xb9bdc8, num=0x16181d, hand=0x16181d,
            second=0xff9500, cap=0xff9500, rimW=40, ticks=60, tickL=7, tickW=48, inset=45, nums=4, numKind=0, numPx=20,
            numPxM=15, numR=19, hB=10, hL=52, hW=24, mB=12, mL=80, mW=34, sL=86, handKind=0, extra=0, mode=0)

# id, Vietnamese name, what differs from BASE. The first four are the faces from before (unchanged).
FACES = [
    ("analog", "Kim cổ điển", {}),
    ("roman", "La Mã", dict(face=0xf5efe4, rim=0xb08d57, tick=0x2b2622, minor=0x9a8f80, num=0x2b2622, hand=0x2b2622,
                            second=0xb3161c, cap=0x2b2622, nums=12, numKind=1, numPx=16, numPxM=12, numR=20)),
    ("minimal", "Kim tối giản", dict(second=0xff9500, cap=0xff9500, ticks=112, inset=90, nums=0, mode=1)),
    ("station", "Đồng hồ ga", dict(rim=0x16181d, minor=0x16181d, second=0xd92d20, cap=0x16181d, nums=0, tickL=12, tickW=30,
                                   hB=18, hL=62, hW=16, mB=18, mL=90, mW=20, sL=66, handKind=1, extra=16)),
    ("arabic", "Số Ả Rập", dict(rim=0x16181d, rimW=60, minor=0x9aa0aa, second=0xe0312b, cap=0x16181d, tickL=6, tickW=60,
                                inset=40, nums=12, numPxM=12, numR=18, hB=12, hL=50, hW=22, mB=14, mW=32, sL=88)),
    ("sticks", "Vạch mảnh", dict(rimW=0, second=0xff3b30, cap=0x16181d, ticks=12, tickL=10, tickW=60, inset=50, nums=0,
                                 hB=0, hL=50, hW=30, mB=0, mL=84, mW=46, sL=90, mode=1)),
    ("gold", "Vàng sang trọng", dict(face=0x141210, face2=0x2a2520, rim=0xc9a24a, rimW=20, tick=0xd4af37, minor=0x6b5a35,
                                     num=0xe5c76b, hand=0xe5c76b, second=0xe5c76b, cap=0xd4af37, tickL=6, inset=75,
                                     nums=4, numKind=1, numPx=18, numPxM=13, numR=22)),
    ("wood", "Gỗ ấm", dict(face=0x8b5a2b, face2=0x5e3a1a, rim=0x3e2410, rimW=28, tick=0xf6e7c8, minor=0xc9a97a,
                           num=0xf6e7c8, hand=0xfff4dc, second=0xffb347, cap=0xfff4dc, tickW=40, inset=55, nums=12,
                           numPx=18, numPxM=12, numR=20, hW=20, mW=30)),
    ("bauhaus", "Bauhaus", dict(face=0xf4f1ea, rimW=0, tick=0x1a1a1a, minor=0x1a1a1a, num=0x1a1a1a, hand=0x1a1a1a,
                                second=0xd23c2a, cap=0xd23c2a, tickL=5, tickW=80, inset=35, nums=12, numPx=15, numPxM=12,
                                numR=15, hB=0, hL=55, hW=40, mB=0, mL=86, mW=60, sL=90, handKind=1)),
    ("chrono", "Bấm giờ", dict(face=0x22262c, face2=0x14171b, rim=0x8c939c, rimW=26, tick=0xf2f4f7, minor=0x5b636f,
                               num=0xf2f4f7, hand=0xf2f4f7, second=0xff6a00, cap=0xff6a00, tickL=8, tickW=36, inset=60,
                               nums=0, hB=14, hL=50, hW=18, mB=16, mL=82, mW=26, sL=88, extra=4)),
    ("outline", "Trong suốt", dict(face=NO, rimW=70, tickW=60, nums=4, numPx=18, numPxM=13, hW=20, mW=30, hL=54, mL=82,
                                   sL=88, mode=2)),
    ("pilot", "Phi công", dict(face=0x15171a, rim=0x2a2d33, tick=0xffffff, minor=0x9aa0a8, num=0xffffff, hand=0xffffff,
                               second=0xffffff, cap=0xffffff, tickW=30, inset=40, nums=12, numPx=24, numPxM=12, hB=8,
                               hL=50, hW=13, mB=10, mW=18, sL=88)),
    ("kids", "Trẻ em", dict(face=0xfff7e0, rim=0xffb703, rimW=14, hour=0xe63946, tick=0x3a3a3a, minor=0xd0c4a8,
                            hand=0x1d4ed8, second=0x2a9d8f, cap=0x1d1d1d, inset=80, nums=12, numKind=2, numPx=22,
                            numPxM=13, numR=21, hB=0, hL=50, hW=11, mB=0, mL=76, mW=16)),
    ("night", "Đêm đỏ", dict(face=0x080808, rim=0x2a0a0a, tick=0xb71c1c, minor=0x4a1010, num=0xc62828, hand=0xd32f2f,
                             second=0xff5252, cap=0xd32f2f, tickW=40)),
    ("datewin", "Có ô ngày", dict(face=0xfbfbf8, rim=0x6b7280, tick=0x1f2937, minor=0xa3a9b4, num=0x1f2937,
                                  hand=0x1f2937, second=0xc0392b, cap=0x1f2937, tickW=40, nums=12, numPx=18, numPxM=12,
                                  hW=22, mW=32, extra=1)),
    ("wxface", "Kèm thời tiết", dict(face=0xeaf4ff, face2=0xcfe3ff, rim=0x3b82f6, tick=0x1e3a8a, minor=0x93b4e6,
                                     num=0x1e3a8a, hand=0x1e293b, second=0xf59e0b, cap=0x1e293b, tickW=40, nums=4,
                                     numPx=18, numPxM=12, hW=22, mW=32, extra=2)),
    ("diver", "Thợ lặn", dict(face=0x0b2545, face2=0x13315c, rim=0x1a1d23, rimW=9, tick=0xffffff, minor=0x8ba0c0,
                              num=0xffffff, hand=0xf5f5f5, second=0xff7a00, cap=0xff7a00, tickL=9, tickW=22, inset=135,
                              nums=0, hB=10, hL=46, hW=14, mB=12, mL=72, mW=20, sL=80, extra=8)),
]


def rows():
    out = []
    for fid, name, d in FACES:
        v = dict(BASE, **d)
        out.append((fid, name, [v[f] for f in FIELDS]))
    return out


def cval(i, x):
    return f"0x{x:06x}" if i < COLOURS else str(x)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    head = "Generated by s3/tools/faces.py (the analog clock faces): edit that file and run it, not this one."
    rs = rows()
    h = [f"// {head}", "#pragma once", "// Included by src/wid/clock.cpp only (Look is defined there).", "",
         f"constexpr uint32_t NO_COLOUR = 0x{NO:x};", f"constexpr int FACE_COUNT = {len(rs)};",
         "const char* const FACE_IDS[FACE_COUNT] = {" + ", ".join(f'"{r[0]}"' for r in rs) + "};",
         "const char* const FACE_NAMES[FACE_COUNT] = {" + ", ".join(f'"{r[1]}"' for r in rs) + "};",
         "const Look FACE_LOOKS[FACE_COUNT] = {"]
    for fid, name, vals in rs:
        h.append("    {" + ", ".join(cval(i, x) for i, x in enumerate(vals)) + "},  // " + fid)
    h.append("};")
    open(os.path.join(root, "src/wid/faces.h"), "w").write("\n".join(h) + "\n")
    t = [f"// {head}", "// The analog clock faces, each the Look of clock.ts face() (fields in clock.ts Look's order).",
         f"export const NO_COLOUR = 0x{NO:x};",
         "export const FACE_STYLES = [" + ", ".join(f'{{ id: "{r[0]}", name: "{r[1]}" }}' for r in rs) + "];",
         "export const FACE_LOOKS: Record<string, number[]> = {"]
    for fid, name, vals in rs:
        t.append(f"  {fid}: [" + ", ".join(cval(i, x) for i, x in enumerate(vals)) + "],")
    t.append("};")
    open(os.path.join(root, "../web/app/src/os/wid/faces.ts"), "w").write("\n".join(t) + "\n")
    print(len(rs), "faces")


if __name__ == "__main__":
    main()
