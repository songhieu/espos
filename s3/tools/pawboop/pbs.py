"""Read Biscuit's picture pack (PBS2, see src/pet/formats.h) with Pillow: a clip's picture as RGBA (soft edge
as on the board: alpha = the mask's 3x3 mean). Used by make_icon.py; also handy to look at a picture:
  python3 tools/pawboop/pbs.py sim/data/pawboop/biscuit_M.pbs happy 0 out.png
"""
import io
import struct
import sys

from PIL import Image, ImageFilter


class Pack:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        magic, self.w, self.h, self.fps, n, nframes, self.hdr_len, self.dims = struct.unpack_from("<4sHHHHIHH", self.d, 0)
        assert magic == b"PBS2"
        self.hdr = self.d[20:20 + self.hdr_len]
        p = 20 + self.hdr_len
        self.clips = {}
        for _ in range(n):
            name, first, count, loop, view = struct.unpack_from("<16sIHBB", self.d, p)
            self.clips[name.rstrip(b"\0").decode()] = (first, count, loop)
            p += 24
        self.offs = struct.unpack_from("<%dI" % (nframes + 1), self.d, p)

    def picture(self, clip, i):
        """(RGBA image of the 320x304 canvas, the frame's record)"""
        first, count, _ = self.clips[clip]
        o = self.offs[first + i % count]
        bx, by, bw, bh, sx, sy, sr, fl, scan, mlen = struct.unpack_from("<HHHHhhBBII", self.d, o)
        canvas = Image.new("RGBA", (self.w, self.h), (0, 0, 0, 0))
        if bw:
            hd = bytearray(self.hdr)
            hd[self.dims:self.dims + 4] = bytes((bh >> 8, bh & 255, bw >> 8, bw & 255))
            jpg = bytes(hd) + self.d[o + 22:o + 22 + scan] + b"\xff\xd9"
            rgb = Image.open(io.BytesIO(jpg)).convert("RGB")
            m = bytearray(bw * bh)
            at, run, v = 0, 0, 0
            for x in self.d[o + 22 + scan:o + 22 + scan + mlen]:
                run += x
                if x == 255:
                    continue
                if v:
                    m[at:at + run] = b"\xff" * run
                at += run
                v ^= 1
                run = 0
            mask = Image.frombytes("L", (bw, bh), bytes(m)).filter(ImageFilter.BoxBlur(1))  # 3x3 mean
            part = rgb.convert("RGBA")
            part.putalpha(mask)
            canvas.alpha_composite(part, (bx, by))
        return canvas, (bx, by, bw, bh, sx, sy, sr)


if __name__ == "__main__":
    pk = Pack(sys.argv[1])
    im, rec = pk.picture(sys.argv[2], int(sys.argv[3]))
    bg = Image.new("RGBA", im.size, (255, 255, 255, 255))
    bg.alpha_composite(im)
    bg.convert("RGB").save(sys.argv[4])
    print(rec)
