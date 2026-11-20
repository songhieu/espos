#!/usr/bin/env python3
"""Built-in fonts kept deflated in flash (as src/art.c is since 2026-11-16), inflated once into PSRAM at boot by
src/fonts.cpp. Run from s3/ after make_fonts.sh (it calls this): python3 tools/fontconv/zfonts.py

Each src/fonts/ub_*.c (but the KEEP ones, which code reaches by name: LVGL's default font in include/lv_conf.h and
the Java phones' fonts in src/j2me/midp_font.cpp) keeps its glyph bitmap in the source, inside `#if !UB_ZFONTS`, so
this tool can always read it again; its font_dsc points at UB_BITMAP (NULL when compressed). src/fonts/zfonts.c holds
one zlib stream of all those bitmaps and where each one starts; fonts.cpp gives each font a PSRAM copy of its
descriptions pointing into the inflated bitmaps. Added 2026-11-19 for the aquarium (src/tank) when the app slot was
full: 2026-11-19 the fonts were 320 KB of the 3.75 MB slot.
"""
import re
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "src/fonts"
KEEP = {"ub_r14"}  # LVGL's default font (include/lv_conf.h), and the one every font falls back to without PSRAM
# Reached by name too (src/j2me/midp_font.cpp) but deflated since 0.5.7 (26 KB of the app slot): their descriptions
# live in RAM (no const) and fonts.cpp points them at the inflated bitmaps in place.
INPLACE = {"ub_r12", "ub_r16", "ub_b12", "ub_b14", "ub_b16"}
DSC_CONST = "static const lv_font_fmt_txt_dsc_t font_dsc = {"
DSC_RAM = "static lv_font_fmt_txt_dsc_t font_dsc = {  /* RAM: fonts.cpp sets glyph_bitmap (zfonts.py INPLACE) */"
BEGIN = re.compile(r"^static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap\[\] = \{\s*$", re.M)
WRAP_ON = "#if !UB_ZFONTS  /* the bitmaps are deflated in zfonts.c (tools/fontconv/zfonts.py) */\n"


def bitmap_of(src):
    """(start, end, bytes) of the glyph_bitmap array definition in a font source."""
    m = BEGIN.search(src)
    if not m:
        sys.exit("no glyph_bitmap")
    end = src.index("};", m.end()) + 2
    body = re.sub(r"/\*.*?\*/", "", src[m.end():end - 2], flags=re.S)
    data = bytes(int(t, 0) for t in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", body))
    return m.start(), end, data


def convert(name):
    p = FONTS / f"{name}.c"
    src = p.read_text()
    start, end, data = bitmap_of(src)
    if WRAP_ON not in src:
        array = src[start:end]
        src = (src[:start] + '#include "zfonts.h"\n' + WRAP_ON + array + "\n#define UB_BITMAP glyph_bitmap\n#else\n"
               "#define UB_BITMAP ((const uint8_t*)0)  /* fonts.cpp gives the font its inflated bitmaps */\n#endif\n" + src[end:])
        n = src.count(".glyph_bitmap = glyph_bitmap,")
        if n != 1:
            sys.exit(f"{p}: {n} glyph_bitmap fields")
        src = src.replace(".glyph_bitmap = glyph_bitmap,", ".glyph_bitmap = UB_BITMAP,")
    if name in INPLACE and DSC_CONST in src:
        src = src.replace(DSC_CONST, DSC_RAM, 1)
    p.write_text(src)
    return data


def main():
    names = sorted(p.stem for p in FONTS.glob("ub_*.c") if p.stem not in KEEP)
    raw, table = b"", []
    for n in names:
        d = convert(n)
        table.append((n, len(raw), len(d), int(n in INPLACE)))
        raw += d
    z = zlib.compress(raw, 9)
    out = ['/* Made by tools/fontconv/zfonts.py: do not edit. The glyph bitmaps of the built-in fonts below, one zlib',
           ' * stream (src/fonts.cpp inflates it into PSRAM at boot). */',
           '#include "lvgl.h"', '#include "zfonts.h"', '']
    out.append("extern const lv_font_t " + ", ".join(t[0] for t in table) + ";")
    out.append(f"const uint32_t zfonts_raw = {len(raw)}, zfonts_size = {len(z)};")
    out.append(f"const zfont_t zfonts[ZFONTS_N] = {{" + ", ".join(f"{{&{n}, {o}, {l}, {k}}}" for n, o, l, k in table) + "};")
    out.append("const uint8_t zfonts_z[] = {")
    for i in range(0, len(z), 24):
        out.append("  " + ",".join(f"0x{b:02x}" for b in z[i:i + 24]) + ",")
    out.append("};")
    (FONTS / "zfonts.c").write_text("\n".join(out) + "\n")
    (FONTS / "zfonts.h").write_text(
        "/* tools/fontconv/zfonts.py: the built-in fonts' bitmaps kept deflated (src/fonts/zfonts.c, src/fonts.cpp). */\n"
        "#pragma once\n#include <stdint.h>\n#include \"lvgl.h\"\n"
        "#ifdef UB_ZFONTS_RAW  /* -DUB_ZFONTS_RAW: the bitmaps as they were (raw in flash), for before/after checks */\n#define UB_ZFONTS 0\n#else\n#define UB_ZFONTS 1\n#endif\n"
        f"#define ZFONTS_N {len(table)}\n"
        "#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
        "/* inplace: the font's own descriptions are in RAM (reached by name); fonts.cpp sets their glyph_bitmap */\n"
        "typedef struct { const lv_font_t* font; uint32_t off, len; uint8_t inplace; } zfont_t;\n"
        "extern const zfont_t zfonts[ZFONTS_N];\nextern const uint32_t zfonts_raw, zfonts_size;\nextern const uint8_t zfonts_z[];\n"
        "#ifdef __cplusplus\n}\n#endif\n")
    print(f"zfonts: {len(table)} fonts, {len(raw)} bytes of bitmaps -> {len(z)} deflated")


if __name__ == "__main__":
    main()
