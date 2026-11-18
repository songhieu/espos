#!/usr/bin/env python3
# Test vectors for the VM's deflate decoder (tools/j2me/inflate_test.cpp), made by an independent encoder (zlib):
# every block kind (stored, fixed, dynamic), many blocks, the longest matches and distances, a PNG-like zlib stream.
import os, random, sys, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
rng = random.Random(7)
text = "".join("line %d of some text that repeats a little\n" % i for i in range(4000)).encode()
data = {
    "empty": b"",
    "hello": b"Hello from a JAR\n",
    "text": text,
    "random": bytes(rng.getrandbits(8) for _ in range(50000)),
    "runs": b"a" * 200000 + b"b" * 3 + bytes(range(256)) * 40,
    "far": (bytes(rng.getrandbits(8) for _ in range(32000)) * 4),
    "pixels": bytes((x * 3 + y * 7) & 0xff for y in range(320) for x in range(240 * 4)),
}

def raw(d, level=6, strategy=zlib.Z_DEFAULT_STRATEGY):
    c = zlib.compressobj(level, zlib.DEFLATED, -15, 9, strategy)
    return c.compress(d) + c.flush()

for name, d in data.items():
    for tag, level, strat in (("", 6, zlib.Z_DEFAULT_STRATEGY), ("-stored", 0, zlib.Z_DEFAULT_STRATEGY),
                              ("-fixed", 6, zlib.Z_FIXED), ("-best", 9, zlib.Z_DEFAULT_STRATEGY),
                              ("-huff", 6, zlib.Z_HUFFMAN_ONLY), ("-rle", 6, zlib.Z_RLE)):
        n = name + tag
        open(os.path.join(out, n + ".raw"), "wb").write(d)
        open(os.path.join(out, n + ".def"), "wb").write(raw(d, level, strat))
    open(os.path.join(out, name + ".zlib"), "wb").write(zlib.compress(d, 6))
# a stream flushed in many small blocks (Z_FULL_FLUSH between pieces: empty stored blocks too)
c = zlib.compressobj(6, zlib.DEFLATED, -15)
pieces = [text[i:i + 3000] for i in range(0, len(text), 3000)]
s = b"".join(c.compress(p) + c.flush(zlib.Z_FULL_FLUSH) for p in pieces) + c.flush()
open(os.path.join(out, "blocks.raw"), "wb").write(text)
open(os.path.join(out, "blocks.def"), "wb").write(s)
