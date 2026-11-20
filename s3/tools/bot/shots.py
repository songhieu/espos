#!/usr/bin/env python3
"""The host build's pictures (sim/bot/run.sh) as PNGs and GIFs at 3x, and a page with all of them: tools/bot/out/ui.html.
  python3 tools/bot/shots.py FRAMES_DIR OUT_DIR"""
import html
import os
import sys

from PIL import Image

W, H, S = 128, 64, 3
ON, OFF = (236, 244, 255), (8, 9, 12)


def pictures(path):
    with open(path, "rb") as f:
        n, step = map(int, f.readline().split())
        data = f.read()
    out = []
    for k in range(n):
        fb = data[k * 1024:(k + 1) * 1024]
        im = Image.new("RGB", (W, H), OFF)
        p = im.load()
        for y in range(H):
            for x in range(W):
                if fb[(y >> 3) * W + x] >> (y & 7) & 1:
                    p[x, y] = ON
        out.append(im.resize((W * S, H * S), Image.NEAREST))
    return out, step


def main():
    src, dst = sys.argv[1], sys.argv[2]
    shots = os.path.join(dst, "ui")
    os.makedirs(shots, exist_ok=True)
    rows = {}
    for fn in sorted(os.listdir(src)):
        if not fn.endswith(".frames"):
            continue
        name = fn[:-7]
        ims, step = pictures(os.path.join(src, fn))
        # drop repeats: a GIF only when the picture changes
        keep, durs = [ims[0]], [step]
        for im in ims[1:]:
            if im.tobytes() == keep[-1].tobytes():
                durs[-1] += step
            else:
                keep.append(im)
                durs.append(step)
        if len(keep) == 1:
            out = name + ".png"
            keep[0].save(os.path.join(shots, out))
        else:
            out = name + ".gif"
            keep[0].save(os.path.join(shots, out), save_all=True, append_images=keep[1:], duration=durs, loop=0)
        base = name.split("_")
        # names: <state>_<look>_<lang> or <state> (one-offs)
        if len(base) >= 3 and base[-2] in ("B", "C", "D") and base[-1] in ("en", "vi"):
            state, col = "_".join(base[:-2]), base[-2] + " " + base[-1]
        else:
            state, col = name, ""
        rows.setdefault(state, []).append((col, out))
    page = ["<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>",
            "<title>EspOS Bot screens</title><style>body{background:#111;color:#ddd;font:14px system-ui;margin:16px}",
            "h2{font-size:15px;margin:22px 0 6px;color:#9cf}figure{display:inline-block;margin:4px 6px}",
            "img{image-rendering:pixelated;border:3px solid #2a2a2a;width:384px;max-width:90vw}",
            "figcaption{color:#888;font-size:12px}</style>",
            "<h1>EspOS Bot: every screen from the firmware's drawing code (host build)</h1>"]
    for state, items in rows.items():
        page.append(f"<h2>{html.escape(state)}</h2>")
        for col, out in items:
            page.append(f"<figure><img src='ui/{out}'><figcaption>{html.escape(col)}</figcaption></figure>")
    open(os.path.join(dst, "ui.html"), "w").write("\n".join(page))
    print(f"{sum(len(v) for v in rows.values())} shots in {os.path.join(dst, 'ui.html')}")


if __name__ == "__main__":
    main()
