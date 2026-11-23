#!/usr/bin/env python3
"""EspOS languages (docs/i18n.md). Run from s3/.

  i18n.py extract                 the marked English (T_ N_ NP_ TC_ and i18n::List tables in src/) -> i18n/en.json
  i18n.py status                  per language: missing, unused and suspicious entries
  i18n.py build TAG ... | all     i18n/TAG.json -> the package dev.espos.lang.TAG (build-pkgs/lang/, .espk in OUT)
  i18n.py font NAME               a font package (FONTS below) -> build-pkgs/fonts/, .espk in OUT
  i18n.py languages               i18n/languages.json (the list the web shows)
  i18n.py catalog                 OUT/catalog.json again (apps/pack.py) with every package in OUT: the sample apps,
                                  the native packages, the languages and fonts (newest version of each id)
  i18n.py lint [FILE ...]         string literals in src/ that look like user text but carry no mark
  options: --out DIR (default build-pkgs/espos/s3)

i18n/TAG.json: {"English": "translation", ...}; lists ("@weekdays"...) as "item|item|..."; plural entries
("{n} app|{n} apps") as the language's forms joined by "|" (one form: vi zh-Hans ja ko id; one|other: en de es;
one|other with 0 as one: fr pt-BR; one|few|many: ru). Placeholders ({0} {n} {d} {MM}... and printf's %d %s) must
stay; a missing entry shows the English.
"""
import hashlib
import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import espk  # noqa: E402

I18N = ROOT / "i18n"
SRC = ROOT / "src"
OUT = ROOT / "build-pkgs" / "espos" / "s3"
BASE_URL = "https://api.espos.dev/storage/bb/149/apps-s3"
MIN_FW = 500
CONV = ROOT / "tools" / "fontconv" / "node_modules" / ".bin" / "lv_font_conv"
FONTDIR = ROOT / "data" / "fonts"

# tag, own name, English name, plural rule, number separators (group, decimal), font package
LANGS = [
    ("en", "English", "English", "e", ",.", None),
    ("vi", "Tiếng Việt", "Vietnamese", "1", ".,", "dev.espos.font.vi"),
    ("zh-Hans", "简体中文", "Chinese (Simplified)", "1", ",.", "dev.espos.font.sc"),
    ("es", "Español", "Spanish", "e", ".,", None),
    ("fr", "Français", "French", "f", "\u00a0,", None),
    ("de", "Deutsch", "German", "e", ".,", None),
    ("ja", "日本語", "Japanese", "1", ",.", "dev.espos.font.jp"),
    ("ko", "한국어", "Korean", "1", ",.", "dev.espos.font.kr"),
    ("pt-BR", "Português (Brasil)", "Portuguese (Brazil)", "f", ".,", None),
    ("ru", "Русский", "Russian", "r", "\u00a0,", "dev.espos.font.cyr"),
    ("id", "Bahasa Indonesia", "Indonesian", "1", ".,", None),
]
FORMS = {"e": 2, "f": 2, "r": 3, "1": 1}

# The built-in fonts' letters (tools/fontconv/make_fonts.sh): Latin-1 and a few marks. Vietnamese letters are the font
# package dev.espos.font.vi since 2026-11-18.
BUILTIN = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100)) | {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                                               0x2212, 0x20AC, 0x20AE, 0x20B4, 0x20B9}
VI_LETTERS = [0x102, 0x103, 0x110, 0x111, 0x128, 0x129, 0x168, 0x169, 0x1A0, 0x1A1, 0x1AF, 0x1B0]
VI_MARKED = list(range(0x1EA0, 0x1EFA))


def gb2312_level1():
    out = []
    for hi in range(0xB0, 0xD8):
        for lo in range(0xA1, 0xFF):
            try:
                out.append(ord(bytes([hi, lo]).decode("gb2312")))
            except Exception:
                pass
    return out


def jis_level1():
    out = []
    for hi in range(0xB0, 0xD0):  # EUC-JP rows 16..47: JIS X 0208 level 1 kanji
        for lo in range(0xA1, 0xFF):
            try:
                out.append(ord(bytes([hi, lo]).decode("euc_jp")))
            except Exception:
                pass
    return out


def ksx1001_hangul():
    out = []
    for hi in range(0xB0, 0xC9):
        for lo in range(0xA1, 0xFF):
            try:
                out.append(ord(bytes([hi, lo]).decode("euc_kr")))
            except Exception:
                pass
    return out


CJK_PUNCT = list(range(0x3000, 0x3020)) + list(range(0xFF01, 0xFF5F)) + [0x30FB, 0x30FC]
KANA = list(range(0x3041, 0x3097)) + list(range(0x30A1, 0x30FB)) + [0x30FD, 0x30FE]
JAMO = list(range(0x3131, 0x318F))
CYR = [0x401, 0x451] + list(range(0x410, 0x450))

# Font packages: the letters the built-in fonts lack. CJK: one weight, the sizes that matter (the others fall back
# to the nearest); Cyrillic: Ubuntu itself at every text size and weight, like the built-in fonts.
# The scripts each font package holds (letters of other scripts in the sources are left out).
HAN = [(0x3000, 0x303F), (0x4E00, 0x9FFF), (0xFF00, 0xFFEF)]
SCRIPTS = {"dev.espos.font.sc": HAN, "dev.espos.font.jp": HAN + [(0x3040, 0x30FF)],
           "dev.espos.font.kr": [(0x3000, 0x303F), (0xFF00, 0xFFEF), (0x1100, 0x11FF), (0x3130, 0x318F), (0xAC00, 0xD7AF)],
           "dev.espos.font.cyr": [(0x400, 0x4FF)]}
FONTS = {
    "dev.espos.font.vi": {"name": "Phông tiếng Việt", "en": "Vietnamese font", "lang": "vi",
                          "file": "Ubuntu-Regular.ttf", "bold": "Ubuntu-Bold.ttf",
                          "extra": ["Ubuntu-Vietnamese-Regular.ttf", "Ubuntu-Vietnamese-Bold.ttf", VI_MARKED],
                          "sizes": [12, 13, 14, 15, 16, 18, 20], "bold_sizes": [12, 13, 14, 15, 16, 17, 18, 20, 22, 24],
                          "chars": lambda: VI_LETTERS, "fixed": True},
    "dev.espos.font.sc": {"name": "简体中文字体", "en": "Chinese font", "lang": "zh-Hans",
                          "file": "noto/NotoSansSC-Regular.otf", "bold": None, "sizes": [12, 14, 20],
                          "chars": lambda: CJK_PUNCT + gb2312_level1()},
    "dev.espos.font.jp": {"name": "日本語フォント", "en": "Japanese font", "lang": "ja",
                          "file": "noto/NotoSansJP-Regular.otf", "bold": None, "sizes": [12, 14, 20],
                          "chars": lambda: CJK_PUNCT + KANA + jis_level1()},
    "dev.espos.font.kr": {"name": "한국어 글꼴", "en": "Korean font", "lang": "ko",
                          "file": "noto/NotoSansKR-Regular.otf", "bold": None, "sizes": [12, 14, 20],
                          "chars": lambda: CJK_PUNCT + JAMO + ksx1001_hangul()},
    "dev.espos.font.cyr": {"name": "Кириллица", "en": "Cyrillic font", "lang": "ru",
                           "file": "Ubuntu-Regular.ttf", "bold": "Ubuntu-Bold.ttf",
                           "sizes": [12, 13, 14, 15, 16, 18, 20], "bold_sizes": [12, 13, 14, 15, 16, 17, 18, 20, 22, 24],
                           "chars": lambda: CYR},
}
FONT_VERSION = "1.0.0"
LANG_VERSION = "1.0.13"  # bump for ANY change once a version is uploaded. 1.0.13 firmware 0.6.1 (saver apps: "{0} stopped"); 1.0.12 firmware 0.6.0 (Sessions, the assistant and saver slots, presets, the USB update notice); 1.0.11 "Getting {0}'s Coin Run part" (0.5.12); 1.0.10 the Claude widget (0.5.11). 1.0.9 the first-boot setup (onboard.cpp); 1.0.8 apps update themselves + the dog's calls ("It answers to"); 1.0.7 the Control Center flips the screen (Rotate held); 1.0.1 landscape strings; 1.0.2 PawBoop dogs (live 16:57); 1.0.3 Coin Run map strings; 1.0.4 the dog assistant talks + "Gâu" wake word + the aquarium (live with 0.5.6); 1.0.5 the trial and the locks (lock.h, the dog's 402 lines) (live 11-19); 1.0.6 the pop-up's "Today's news" chip

# ---------------------------------------------------------------- C string literals
ESC = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'", "r": "\r", "0": "\0", "a": "\a"}


def unescape(body):
    out, i, raw = bytearray(), 0, body.encode()
    while i < len(raw):
        c = raw[i:i + 1]
        if c != b"\\":
            out += c
            i += 1
            continue
        n = chr(raw[i + 1])
        if n == "x":
            m = re.match(rb"[0-9a-fA-F]{1,2}", raw[i + 2:])
            out.append(int(m.group(0), 16))
            i += 2 + len(m.group(0))
        elif n in "01234567":
            m = re.match(rb"[0-7]{1,3}", raw[i + 1:])
            out.append(int(m.group(0), 8))
            i += 1 + len(m.group(0))
        else:
            out += ESC.get(n, n).encode()
            i += 2
    return out.decode("utf-8")


LIT = r'"(?:[^"\\\n]|\\.)*"'
LITS = rf'((?:{LIT}\s*)+)'
MARK = re.compile(rf'\b(T_|N_|NP_)\(\s*{LITS}\)')
CTX = re.compile(rf'\b(?:TC_|NC_)\(\s*({LIT})\s*,\s*{LITS}\)')
LIST = re.compile(r'\bList\s+(\w+)\s*=\s*\{\s*"(@[\w.]+)"\s*,\s*(\w+)\s*,')


def lits(s):
    return "".join(unescape(m[1:-1]) for m in re.findall(LIT, s))


def strip_comments(s):
    # Keeps line numbers: comments become spaces (string literals are left alone).
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif c == "'" and i + 2 < n:
            j = i + 1
            while j < n and s[j] != "'":
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif s.startswith("//", i):
            j = s.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif s.startswith("/*", i):
            j = s.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", s[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def sources():
    return sorted(p for p in SRC.rglob("*") if p.suffix in (".cpp", ".h", ".c") and "fonts" not in p.parts
                  and p.name != "art.c")


def note_for(lines, ln):
    """A translator's note: '// i18n: ...' on the line or the line above."""
    if 0 <= ln - 1 < len(lines):  # the same line
        m = re.search(r"//\s*i18n:\s*(.*)$", lines[ln - 1])
        if m:
            return m.group(1).strip()
    if 0 <= ln - 2 < len(lines) and lines[ln - 2].strip().startswith("//"):  # a comment line just above
        m = re.search(r"//\s*i18n:\s*(.*)$", lines[ln - 2])
        if m:
            return m.group(1).strip()
    return None


def extract():
    keys = {}

    def add(key, kind, where, note):
        e = keys.setdefault(key, {"kind": kind, "where": []})
        if where not in e["where"]:
            e["where"].append(where)
        if note and note not in e.get("notes", []):
            e.setdefault("notes", []).append(note)
        if kind != "text":
            e["kind"] = kind

    for p in sources():
        raw = p.read_text(encoding="utf-8")
        s = strip_comments(raw)
        lines = raw.split("\n")
        rel = p.relative_to(ROOT).as_posix()
        for m in MARK.finditer(s):
            ln = s.count("\n", 0, m.start()) + 1
            key = lits(m.group(2))
            add(key, "plural" if m.group(1) == "NP_" else "text", f"{rel}:{ln}", note_for(lines, ln))
        for m in CTX.finditer(s):
            ln = s.count("\n", 0, m.start()) + 1
            ctx = unescape(m.group(1)[1:-1])
            add(ctx + "\x04" + lits(m.group(2)), "text", f"{rel}:{ln}", note_for(lines, ln))
        for m in LIST.finditer(s):
            ln = s.count("\n", 0, m.start()) + 1
            arr = m.group(3)
            am = re.search(rf'\b{arr}\s*\[[^\]]*\]\s*=\s*\{{(.*?)\}};', s, re.S)
            if not am:
                sys.exit(f"{rel}:{ln}: list {m.group(2)}: array {arr} not found in the same file")
            items = [unescape(x[1:-1]) for x in re.findall(LIT, am.group(1))]
            aln = s.count("\n", 0, am.start()) + 1
            add(m.group(2), "list", f"{rel}:{ln}", note_for(lines, aln) or note_for(lines, ln))
            keys[m.group(2)]["en"] = "|".join(items)
            keys[m.group(2)]["count"] = len(items)
    # Settings of a language (not text).
    keys["@plural"] = {"kind": "meta", "where": [], "en": "e",
                       "notes": ["plural rule: 1 (one form), e (one|other), f (one|other, 0 is one), r (Russian)"]}
    keys["@num"] = {"kind": "meta", "where": [], "en": ",.", "notes": ["group separator then decimal separator"]}
    out = {k: keys[k] for k in sorted(keys, key=lambda k: (k.lower(), k))}
    I18N.mkdir(exist_ok=True)
    (I18N / "en.json").write_text(json.dumps(out, ensure_ascii=False, indent=1) + "\n")
    n = sum(1 for v in out.values() if v["kind"] != "meta")
    print(f"i18n/en.json: {n} entries ({sum(1 for v in out.values() if v['kind'] == 'list')} lists, "
          f"{sum(1 for v in out.values() if v['kind'] == 'plural')} plurals)")
    return out


# ---------------------------------------------------------------- checks
PH = re.compile(r"\{(?:\d|n|d|m|M|MM|y|w|W)\}")
DATE_PH = {"{d}", "{m}", "{M}", "{MM}", "{y}", "{w}", "{W}"}
PF = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?(?:l|ll|h|z)?[diouxXsfFeEgGcp%]")


def english(key, e):
    if e["kind"] in ("list", "meta"):
        return e["en"]
    return key.split("\x04", 1)[-1]


def lang_info(tag):
    for t in LANGS:
        if t[0] == tag:
            return t
    sys.exit(f"unknown language {tag}")


_cmaps = {}


def font_chars(font_id):
    """Letters a language may use beyond the built-in fonts: what its font package's font file has (the package holds
    the common set plus every letter of the translation)."""
    if font_id not in _cmaps:
        from fontTools.ttLib import TTFont
        f = FONTS[font_id]
        if f.get("fixed"):
            _cmaps[font_id] = set(f["chars"]()) | set(f.get("extra", [None, None, []])[2])
        else:
            _cmaps[font_id] = {c for c in TTFont(FONTDIR / f["file"], fontNumber=0).getBestCmap()
                               if any(a <= c <= b for a, b in SCRIPTS[font_id])}
    return _cmaps[font_id]


def check_lang(tag, en, tr):
    """(errors, warnings) of a translation."""
    info = lang_info(tag)
    rule = tr.get("@plural", info[3])
    errs, warns = [], []
    letters = BUILTIN | (font_chars(info[5]) if info[5] else set())
    for key, e in en.items():
        if e["kind"] == "meta":
            continue
        if key not in tr:
            warns.append(f"missing: {english(key, e)!r}")
            continue
        t, src = tr[key], english(key, e)
        if not isinstance(t, str):
            errs.append(f"{key!r}: not a string")
            continue
        if "…" in t:
            errs.append(f"{key!r}: has \"…\" (the device never cuts text)")
        if e["kind"] == "list":
            n = len(t.split("|"))
            if key != "@quotes" and n != e["count"]:
                errs.append(f"{key}: {n} items, needs {e['count']}")
            if any(not x.strip() for x in t.split("|")):
                errs.append(f"{key}: an empty item")
            continue
        if e["kind"] == "plural":
            forms = t.split("|")
            if len(forms) != FORMS[rule]:
                errs.append(f"{key!r}: {len(forms)} forms, rule {rule} needs {FORMS[rule]}")
            for f in forms:
                if sorted(PH.findall(f)) != sorted(set(PH.findall(src.split('|')[1]))) and "{n}" in src and "{n}" not in f:
                    errs.append(f"{key!r}: form {f!r} lacks {{n}}")
            continue
        # Date parts may change form ({M} Nov -> {m} 11, {W} -> {w}) and order; the others must all stay.
        def norm(xs):
            return sorted(x for x in xs if x not in DATE_PH)
        if norm(PH.findall(t)) != norm(PH.findall(src)):
            errs.append(f"{key!r}: placeholders {PH.findall(src)} became {PH.findall(t)}")
        if any(x in DATE_PH for x in PH.findall(src)) and {x.lower()[:2] for x in PH.findall(t) if x in DATE_PH} - \
                {x.lower()[:2] for x in PH.findall(src) if x in DATE_PH} - {"{m"}:
            errs.append(f"{key!r}: date parts {PH.findall(src)} became {PH.findall(t)}")
        if PF.findall(t) != PF.findall(src):
            errs.append(f"{key!r}: printf parts {PF.findall(src)} became {PF.findall(t)} (same order needed)")
        if src.count("\n") != t.count("\n") and src.count("\n"):
            warns.append(f"{key!r}: line breaks {src.count(chr(10))} -> {t.count(chr(10))}")
    for key in tr:
        if key.startswith("_"):
            continue
        if key not in en:
            warns.append(f"unused: {key!r}")
        elif isinstance(tr[key], str):
            bad = sorted({c for c in tr[key] if ord(c) not in letters and c not in "\n\x04"})
            if bad:
                errs.append(f"{key!r}: letters the fonts lack: {''.join(bad)} "
                            f"({', '.join(hex(ord(c)) for c in bad)})")
    return errs, warns


def load_tr(tag):
    p = I18N / f"{tag}.json"
    if not p.exists():
        sys.exit(f"{p} not found")
    return json.loads(p.read_text())


def status():
    en = json.loads((I18N / "en.json").read_text())
    for info in LANGS[1:]:
        p = I18N / f"{info[0]}.json"
        if not p.exists():
            print(f"{info[0]:8} no file")
            continue
        errs, warns = check_lang(info[0], en, json.loads(p.read_text()))
        miss = sum(1 for w in warns if w.startswith("missing"))
        unused = sum(1 for w in warns if w.startswith("unused"))
        print(f"{info[0]:8} {len(errs)} errors, {miss} missing, {unused} unused")
        for e in errs[:40]:
            print("   E", e)


# ---------------------------------------------------------------- strings.bin
def fnv(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def strings_bin(entries):
    """entries: {key: bytes value (NUL-terminated)}"""
    items = sorted((fnv(k.encode()), k, v) for k, v in entries.items())
    for a, b in zip(items, items[1:]):
        if a[0] == b[0]:
            sys.exit(f"hash collision: {a[1]!r} and {b[1]!r} (change one English text)")
    pool, offs = bytearray(), []
    for h, k, v in items:
        offs.append(len(pool))
        pool += v
    pool_off = 16 + 8 * len(items)
    head = struct.pack("<4sIII", b"ESL1", len(items), pool_off, 0)
    tab = b"".join(struct.pack("<II", h, o) for (h, _, _), o in zip(items, offs))
    return head + tab + bytes(pool)


def build_lang(tag, out):
    en = json.loads((I18N / "en.json").read_text())
    tr = load_tr(tag)
    info = lang_info(tag)
    errs, warns = check_lang(tag, en, tr)
    if errs:
        sys.exit(f"{tag}: {len(errs)} problems:\n  " + "\n  ".join(errs[:60]))
    entries = {}
    for key, e in en.items():
        if e["kind"] == "meta" or key not in tr:
            continue
        t = tr[key]
        if e["kind"] == "list":
            entries[key] = "\0".join(t.split("|")).encode() + b"\0\0"
        elif t != english(key, e):
            entries[key] = t.encode() + b"\0"
    entries["@plural"] = tr.get("@plural", info[3]).encode() + b"\0"
    entries["@num"] = tr.get("@num", info[4]).encode() + b"\0"
    blob = strings_bin(entries)
    pid = f"dev.espos.lang.{tag}"
    folder = ROOT / "build-pkgs" / "lang" / pid
    folder.mkdir(parents=True, exist_ok=True)
    for f in folder.iterdir():
        f.unlink()
    (folder / "strings.bin").write_bytes(blob)
    man = {"id": pid, "name": info[2], "version": LANG_VERSION, "code": code_of(LANG_VERSION), "kind": "native",
           "engine": "lang", "type": "pack", "min_fw": MIN_FW, "category": "tool", "author": "EspOS",
           "desc": f"{info[2]} for EspOS", "lang": tag, "icon": {"bg": ["#60a5fa", "#2563eb"], "glyph": "globe"},
           "part": "core", "i18n": pack_names(tag)}
    if info[5]:
        man["font"] = info[5]
    (folder / "manifest.json").write_text(json.dumps(man, ensure_ascii=False))
    f = espk.build(folder, Path(out) / f"{pid}-{LANG_VERSION}.espk")
    miss = sum(1 for w in warns if w.startswith("missing"))
    print(f"{tag}: {len(entries)} entries, strings.bin {len(blob)} B, {miss} missing, {f.stat().st_size} B package")
    return f


FONT_NAME = {"en": "{0} font", "vi": "Phông {0}", "zh-Hans": "{0}字体", "es": "Fuente ({0})", "fr": "Police ({0})",
             "de": "Schrift ({0})", "ja": "{0}フォント", "ko": "{0} 글꼴", "pt-BR": "Fonte ({0})", "ru": "Шрифт ({0})",
             "id": "Font {0}"}


def pack_names(lang_tag, font=False):
    """{"en": "Japanese", "vi": "Tiếng Nhật", ...}: a language's (or its font's) name in every language, for the
    package's manifest ("i18n"); the device shows the one of the language shown (Settings, Storage)."""
    english = lang_info(lang_tag)[2]
    out = {}
    for t in LANGS:
        tr = json.loads((I18N / f"{t[0]}.json").read_text()) if t[0] != "en" and (I18N / f"{t[0]}.json").exists() else {}
        n = tr.get(english, english)
        if font:
            if t[0] in ("vi", "es", "fr", "de", "pt-BR", "ru"):
                n = n[:1].lower() + n[1:] if t[0] == "vi" else n
            n = FONT_NAME[t[0]].replace("{0}", n)
        out[t[0]] = {"name": n}
    return out


def code_of(v):
    a, b, c = (list(map(int, v.split("."))) + [0, 0, 0])[:3]
    return a * 10000 + b * 100 + c


# ---------------------------------------------------------------- fonts
def parse_lvgl_c(path):
    """The bitmap, glyph descriptions, code points and metrics of an lv_font_conv C file (one font, no kerning)."""
    s = Path(path).read_text()
    bm = re.search(r"glyph_bitmap\[\]\s*=\s*\{(.*?)\};", s, re.S).group(1)
    bitmap = bytes(int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", re.sub(r"/\*.*?\*/", "", bm, flags=re.S)))
    gd = re.search(r"glyph_dsc\[\]\s*=\s*\{(.*?)\};", s, re.S).group(1)
    dsc = [tuple(int(v) for v in g) for g in re.findall(
        r"\.bitmap_index\s*=\s*(-?\d+),\s*\.adv_w\s*=\s*(-?\d+),\s*\.box_w\s*=\s*(-?\d+),\s*\.box_h\s*=\s*(-?\d+),"
        r"\s*\.ofs_x\s*=\s*(-?\d+),\s*\.ofs_y\s*=\s*(-?\d+)", gd)]
    # Code points in glyph id order, from the cmaps.
    lists = {m.group(1): [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", m.group(2))]
             for m in re.finditer(r"unicode_list_(\d+)\[\]\s*=\s*\{(.*?)\};", s, re.S)}
    cps = []
    for m in re.finditer(r"\.range_start\s*=\s*(\d+),\s*\.range_length\s*=\s*(\d+),\s*\.glyph_id_start\s*=\s*(\d+),"
                         r"\s*\.unicode_list\s*=\s*(\w+),.*?\.list_length\s*=\s*(\d+),\s*\.type\s*=\s*(\w+)", s, re.S):
        start, length, gstart, ul, ll, typ = m.groups()
        start, length, gstart = int(start), int(length), int(gstart)
        if typ.endswith("FORMAT0_TINY"):
            cps += [(gstart + i, start + i) for i in range(length)]
        elif typ.endswith("SPARSE_TINY"):
            arr = lists[re.search(r"(\d+)$", ul).group(1)]
            cps += [(gstart + i, start + o) for i, o in enumerate(arr)]
        else:
            sys.exit(f"{path}: cmap type {typ} not handled")
    lh = int(re.search(r"\.line_height\s*=\s*(\d+)", s).group(1))
    base = int(re.search(r"\.base_line\s*=\s*(-?\d+)", s).group(1))
    upos = int(re.search(r"\.underline_position\s*=\s*(-?\d+)", s).group(1))
    uth = int(re.search(r"\.underline_thickness\s*=\s*(-?\d+)", s).group(1))
    bpp = int(re.search(r"\.bpp\s*=\s*(\d+)", s).group(1))
    return bitmap, dsc, sorted(cps), lh, base, upos, uth, bpp


def esf(path_c, px, bold):
    bitmap, dsc, cps, lh, base, upos, uth, bpp = parse_lvgl_c(path_c)
    # Glyph ids follow the code points' order already (lv_font_conv sorts them); keep that order.
    gids = [g for g, _ in cps]
    if gids != list(range(1, len(cps) + 1)):
        sys.exit(f"{path_c}: glyph ids are not in code point order")
    first = cps[0][1]
    rng = cps[-1][1] - first + 1
    if rng > 0xFFFF:
        sys.exit(f"{path_c}: code points span {rng} (at most 65535)")
    n = len(cps)
    head_len = 40
    off_list = head_len
    off_dsc = (off_list + 2 * n + 3) & ~3
    off_bmp = off_dsc + 8 * (n + 1)
    lst = b"".join(struct.pack("<H", cp - first) for _, cp in cps)
    ds = bytearray(8)  # glyph 0: unused
    for (bi, adv, bw, bh, ox, oy) in dsc[1:]:
        ds += struct.pack("<IBBbb", (bi & 0xFFFFF) | ((adv & 0xFFF) << 20), bw, bh, ox, oy)
    if len(dsc) - 1 != n:
        sys.exit(f"{path_c}: {len(dsc) - 1} glyph descriptions for {n} code points")
    head = struct.pack("<4sBBBBhhbbHIIIIII", b"ESF1", px, 1 if bold else 0, bpp, 0, lh, base, upos, uth, 0, first, n,
                       off_list, off_dsc, off_bmp, rng)
    assert len(head) == head_len, len(head)
    blob = head + lst + b"\0" * (off_dsc - off_list - len(lst)) + bytes(ds) + bitmap
    return blob


def ranges(cps):
    cps = sorted(set(cps))
    out, i = [], 0
    while i < len(cps):
        j = i
        while j + 1 < len(cps) and cps[j + 1] == cps[j] + 1:
            j += 1
        out.append(f"0x{cps[i]:X}" if i == j else f"0x{cps[i]:X}-0x{cps[j]:X}")
        i = j + 1
    return ",".join(out)


def used_chars(tag):
    """Every letter of the language's translation, of the sample apps (apps/*: main.js, manifests) and of the
    package names: the font package has them all."""
    out = set()
    p = I18N / f"{tag}.json"
    if p.exists():
        out |= {ord(c) for v in json.loads(p.read_text()).values() if isinstance(v, str) for c in v}
    for f in list((ROOT / "apps").glob("*/main.js")) + list((ROOT / "apps").glob("*/manifest.json")):
        out |= {ord(c) for c in f.read_text(encoding="utf-8")}
    for t in LANGS:
        out |= {ord(c) for v in pack_names(t[0], True).values() for c in v["name"]}
        out |= {ord(c) for v in pack_names(t[0]).values() for c in v["name"]}
    return out


def build_font(fid, out):
    f = FONTS[fid]
    have_reg = set()
    from fontTools.ttLib import TTFont
    cmap = TTFont(FONTDIR / f["file"], fontNumber=0).getBestCmap()
    if f.get("fixed"):
        chars = set(f["chars"]())
    else:
        chars = (set(f["chars"]()) | used_chars(f["lang"])) - BUILTIN
        chars = {c for c in chars if c in cmap and any(a <= c <= b for a, b in SCRIPTS[fid])}
    extra = f.get("extra")  # a second font file (regular, bold) and its letters
    folder = ROOT / "build-pkgs" / "fonts" / fid
    folder.mkdir(parents=True, exist_ok=True)
    for x in folder.iterdir():
        x.unlink()
    total = 0
    jobs = [(px, False, f["file"]) for px in f["sizes"]]
    if f.get("bold"):
        jobs += [(px, True, f["bold"]) for px in f.get("bold_sizes", f["sizes"])]
    with tempfile.TemporaryDirectory() as tmp:
        for px, bold, file in jobs:
            c = Path(tmp) / f"f{px}{bold}.c"
            more = ["--font", str(FONTDIR / extra[1 if bold else 0]), "-r", ranges(extra[2])] if extra else []
            subprocess.run([str(CONV), "--bpp", "4", "--size", str(px), "--no-compress", "--no-kerning", "--format",
                            "lvgl", "--lv-include", "lvgl.h", "--font", str(FONTDIR / file), "-r", ranges(chars)] + more +
                           ["-o", str(c), "--lv-font-name", "f"], check=True, stdout=subprocess.DEVNULL)
            blob = esf(c, px, bold)
            name = f"{'b' if bold else 'r'}{px}.bin"
            (folder / name).write_bytes(blob)
            total += len(blob)
            have_reg.add(px)
    lic = FONTDIR / ("noto/LICENSE-OFL.txt" if "noto" in f["file"] else "UFL.txt")
    (folder / "LICENSE.txt").write_bytes(lic.read_bytes())
    n_letters = len(chars) + (len(extra[2]) if extra else 0)
    names = pack_names(f["lang"], font=True)
    man = {"id": fid, "name": names["en"]["name"], "version": FONT_VERSION, "code": code_of(FONT_VERSION), "kind": "native",
           "engine": "font", "type": "pack", "min_fw": MIN_FW, "category": "tool", "author": "EspOS",
           "desc": f"{f['en']} for EspOS ({n_letters} letters; "
                   f"{'Noto Sans CJK, SIL Open Font License' if 'noto' in f['file'] else 'Ubuntu Font Licence'})",
           "icon": {"bg": ["#94a3b8", "#475569"], "glyph": "font"}, "part": "core", "lang": f["lang"], "i18n": names}
    (folder / "manifest.json").write_text(json.dumps(man, ensure_ascii=False))
    p = espk.build(folder, Path(out) / f"{fid}-{FONT_VERSION}.espk")
    print(f"{fid}: {n_letters} letters, {len(jobs)} sizes, {total} B of fonts, package {p.stat().st_size} B")
    return p


# ---------------------------------------------------------------- languages.json, lint
def languages():
    out = []
    for tag, name, en_name, *_rest in LANGS:
        font = _rest[2]
        out.append({"tag": tag, "name": name, "english": en_name, "builtin": tag == "en",
                    "pack": None if tag == "en" else f"dev.espos.lang.{tag}", "font_pack": font,
                    "min_fw": None if tag == "en" else MIN_FW})
    (I18N / "languages.json").write_text(json.dumps(out, ensure_ascii=False, indent=1) + "\n")
    print(f"i18n/languages.json: {len(out)} languages")


VI = re.compile(r"[ăâđêôơưĂÂĐÊÔƠƯàáảãạằắẳẵặầấẩẫậèéẻẽẹềếểễệìíỉĩịòóỏõọồốổỗộờớởỡợùúủũụừứửữựỳýỷỹỵ]", re.I)


def lint(files):
    files = [Path(f).resolve() for f in files] or sources()
    n = 0
    for p in files:
        raw = p.read_text(encoding="utf-8")
        s = strip_comments(raw)
        marked = set()
        for m in list(MARK.finditer(s)) + list(CTX.finditer(s)):
            marked.update(range(m.start(), m.end()))
        for m in re.finditer(LIT, s):
            if m.start() in marked:
                continue
            t = unescape(m.group(0)[1:-1])
            line_start = s.rfind("\n", 0, m.start()) + 1
            line = s[line_start:s.find("\n", m.start())]
            if "trace::add" in line or "Serial.print" in line or "#include" in line:
                continue
            if VI.search(t):
                n += 1
                print(f"{p.relative_to(ROOT)}:{s.count(chr(10), 0, m.start()) + 1}: {t!r}")
    print(f"{n} unmarked Vietnamese literals")


def catalog(out):
    """apps/pack.py over OUT with every .espk there (the newest version of each id; dates kept for unchanged ones)."""
    import datetime
    old = {}
    if (Path(out) / "catalog.json").exists():
        old = {e["id"]: e for e in json.loads((Path(out) / "catalog.json").read_text()).get("packages", [])}
    apps = {json.loads((d / "manifest.json").read_text())["id"] for d in (ROOT / "apps").iterdir()
            if (d / "manifest.json").exists()}
    newest = {}
    for f in sorted(Path(out).glob("*.espk")):
        m, _, _ = espk.read(f)
        if m["id"] in apps:
            continue
        cur = newest.get(m["id"])
        if not cur or m.get("code", 0) > cur[0]:
            newest[m["id"]] = (m.get("code", 0), [f])
        elif m.get("code", 0) == cur[0]:
            cur[1].append(f)
    files = [str(f) for _, fs in newest.values() for f in fs]
    subprocess.run([sys.executable, str(ROOT / "apps/pack.py"), str(out), BASE_URL] + files, cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    c = json.loads((Path(out) / "catalog.json").read_text())
    today = datetime.date.today().isoformat()
    for e in c["packages"]:
        o = old.get(e["id"])
        e["date"] = o["date"] if o and o.get("code") == e.get("code") and o.get("date") else today
    errs = espk.check_catalog(c, out)  # (apps/pack.py keeps part choices and add-on fields; checked again here)
    if errs:
        sys.exit("catalog.json refused:\n  " + "\n  ".join(errs))
    (Path(out) / "catalog.json").write_text(json.dumps(c, ensure_ascii=False, indent=1))
    print(f"catalog.json: {len(c['packages'])} packages")


def main(argv):
    out = OUT
    if "--out" in argv:
        i = argv.index("--out")
        out = Path(argv[i + 1])
        argv = argv[:i] + argv[i + 2:]
    if not argv:
        print(__doc__)
        sys.exit(2)
    cmd, args = argv[0], argv[1:]
    if cmd == "extract":
        extract()
    elif cmd == "status":
        status()
    elif cmd == "build":
        tags = [t[0] for t in LANGS[1:] if (I18N / f"{t[0]}.json").exists()] if args == ["all"] else args
        for t in tags:
            build_lang(t, out)
    elif cmd == "font":
        for fid in (list(FONTS) if args == ["all"] else args):
            build_font(fid, out)
    elif cmd == "languages":
        languages()
    elif cmd == "catalog":
        catalog(out)
    elif cmd == "lint":
        lint(args)
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main(sys.argv[1:])
