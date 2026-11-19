#!/usr/bin/env python3
"""EspOS packages (.espk), docs/ecosystem.md §2 and docs/packages.md.

  espk.py FOLDER OUT.espk              build: FOLDER has manifest.json and the files (sub-folders kept in names)
  espk.py --check FILE.espk ...        verify: header, manifest, table, every file's CRC32; prints a summary
  espk.py --entry BASE_URL FILE.espk ...
                                       a catalog "packages" entry (JSON) for the parts given (core first)
  espk.py --install-sim SIM_DATA FILE.espk ... [--vol sd|flash]
  (faces, widgets and themes, "runtime": "decl": docs/profiles.md; their layout file is checked too)
                                       put a package straight into a simulator data folder (the card by default;
                                       flash = $SIM_DATA/fs/pkgs, only for kind "js")

Layout (little endian): "ESPK", u16 format=1, u16 flags=0, u32 manifest_len, u32 nfiles, manifest JSON padded
with zeros to 16, nfiles x {char name[52]; u32 off; u32 size; u32 crc32}, then each file at a 16-byte aligned
offset in table order. The magic "ESPK" is the format's, from before the .espk name: every firmware and tool
reads it, so it stays.
"""
import hashlib
import json
import os
import struct
import sys
import zlib
from pathlib import Path

ENGINES = {"js", "pet", "run", "j2me", "lang", "font", "wake", "tank"}  # lang, font: language packs (docs/i18n.md); wake: a wake word's model; tank: Hồ cá cảnh (docs/tank.md)
CATEGORIES = {"game", "tool", "study", "health", "widget"}
PERMS = {"sound", "notify", "net", "bg"}
# Declarative packages (docs/profiles.md): data only, drawn by the firmware.
DECL_TYPES = {"face", "widget", "theme"}
DECL_LEVEL = 1
DECL_MANIFEST_MAX = 8 * 1024  # firmware/src/espos/Pkgs.cpp MAX_MANIFEST
DECL_PERMS = {"data:weather", "data:calendar", "data:reading", "data:news", "data:markets"}
DECL_REFRESH = {"minute", "hour", "data", "day"}
DECL_ITEMS = {"box", "line", "circle", "text", "icon", "image", "bar", "list", "group"}
CAPS = {"screen.color", "screen.gray", "screen.eink", "screen.fast", "input.touch", "input.buttons", "mem.psram",
        "audio.out", "audio.in", "store.sd", "net.wifi", "net.ble", "power.battery", "led.rgb"}
# Languages the S3 shows (src/i18n.cpp LANGS, tools/i18n.py LANGS): the keys of a manifest's "i18n" it reads.
LANG_TAGS = ("en", "vi", "zh-Hans", "es", "fr", "de", "ja", "ko", "pt-BR", "ru", "id")
GLYPHS = {"timer", "alarm", "bell", "list", "drop", "flag", "globe", "sun", "moon", "photo", "edit", "font", "home",
          "search", "speaker", "play", "lap", "pin", "leaf", "power", "grid"}


def pad16(n):
    return (n + 15) & ~15


def fail(msg):
    sys.exit(f"espk: {msg}")


def check_manifest(m, where):
    """Problems with a manifest (a list of strings)."""
    errs = []
    if not isinstance(m.get("id"), str) or not m["id"] or "/" in m["id"] or m["id"].startswith(".") or len(m["id"]) > 48:
        errs.append("id: a reverse-domain name like vn.you.app (no '/', at most 48 characters)")
    part = m.get("part", "core")
    if not isinstance(m.get("version"), str) or not m.get("version"):
        errs.append("version: a string like 1.0.0")
    if not isinstance(m.get("code"), int) or m["code"] < 1:
        errs.append("code: a whole number >= 1, higher for every new version")
    if part == "core":
        errs += check_i18n(m)
    if part == "core" and m.get("type") in DECL_TYPES:
        errs += check_decl_manifest(m)
    elif part == "core":
        for k in ("name", "kind"):
            if not m.get(k):
                errs.append(f"{k}: missing (the core part carries the full manifest)")
        kind = m.get("kind")
        if kind not in ("js", "native"):
            errs.append('kind: "js" or "native"')
        engine = m.get("engine", "js" if kind == "js" else None)
        if engine not in ENGINES:
            errs.append(f"engine: one of {sorted(ENGINES)}")
        if kind == "js" and engine != "js":
            errs.append('kind "js" goes with engine "js"')
        if m.get("category", "tool") not in CATEGORIES:
            errs.append(f"category: one of {sorted(CATEGORIES)}")
        for p in m.get("perms", []):
            if p not in PERMS:
                errs.append(f"perms: unknown {p!r} (known: {sorted(PERMS)})")
        icon = m.get("icon", {})
        if icon.get("glyph", "grid") not in GLYPHS:
            errs.append(f"icon.glyph: one of {sorted(GLYPHS)}")
        for c in icon.get("bg", []):
            if not (isinstance(c, str) and len(c) == 7 and c[0] == "#"):
                errs.append("icon.bg: two colours like #ff6b5a")
        if m.get("type", "app") not in ("app", "pack"):
            errs.append('type: "app" (the default) or "pack" (data for an engine, no icon)')
        ao = m.get("addon_of")
        if ao is not None and (not isinstance(ao, str) or not ao or "/" in ao or ao == m.get("id")):
            errs.append("addon_of: the id of the package this one adds to (e.g. vn.pawboop.pet)")
        # An add-on's shelf on its app's store page ("dog", "map": PawBoop's dogs and run maps), and another add-on
        # it is useless without (a map "needs" the run game): docs/packages.md §4b.
        if m.get("set") is not None and (not ao or m["set"] not in ("dog", "map")):
            errs.append('set: "dog" or "map", on an add-on (addon_of)')
        nd = m.get("needs")
        if nd is not None and (not isinstance(nd, str) or not nd or "/" in nd or nd == m.get("id")):
            errs.append("needs: the id of a package this one cannot work without (e.g. vn.pawboop.run)")
        if "…" in json.dumps(m, ensure_ascii=False):
            errs.append('no "…" in texts (the device never shows it)')
    return [f"{where}: {e}" for e in errs]


def check_i18n(m):
    """Problems with "i18n": {"<language tag>": {"name": ..., "desc": ...}} (docs/i18n.md; src/pkg/pkg.cpp and
    store.cpp read the name and description of the language shown). The SDK's espos check has the same errors
    (M035) and warns about tags the S3 does not show (M036)."""
    t = m.get("i18n")
    if t is None:
        return []
    if not isinstance(t, dict):
        return ['i18n: {"<language tag>": {"name": ..., "desc": ...}} (the name and description in other languages)']
    errs = []
    for tag, v in t.items():
        if not isinstance(v, dict):
            errs.append(f'i18n.{tag}: {{"name": ..., "desc": ...}}')
            continue
        n = v.get("name")
        if n is not None and (not isinstance(n, str) or not n.strip() or len(n) > 40):
            errs.append(f"i18n.{tag}.name: text of 1 to 40 characters")
        d = v.get("desc")
        if d is not None and (not isinstance(d, str) or len(d) > 300):
            errs.append(f"i18n.{tag}.desc: text of at most 300 characters")
    return errs


def check_decl_manifest(m):
    """Problems with the manifest of a face, widget or theme (docs/profiles.md §3)."""
    errs = []
    if not m.get("name"):
        errs.append("name: missing")
    if m.get("runtime") != "decl":
        errs.append('runtime: "decl" for a face, widget or theme')
    api = m.get("api", {})
    if not isinstance(api, dict) or not isinstance(api.get("min", 1), int) or not 1 <= api.get("min", 1) <= DECL_LEVEL:
        errs.append(f"api: {{\"min\": 1..{DECL_LEVEL}, \"target\": n}}")
    req = m.get("requires", [])
    if not isinstance(req, list) or any(r not in CAPS for r in req):
        errs.append(f"requires: a list of {sorted(CAPS)}")
    for sc in m.get("screens", []):
        w, _, h = str(sc).partition("x")
        if not (w.isdigit() and h.isdigit()):
            errs.append(f"screens: sizes like 800x480, not {sc!r}")
    if m.get("refresh", "data") not in DECL_REFRESH:
        errs.append(f"refresh: one of {sorted(DECL_REFRESH)}")
    for p in m.get("perms", []):
        if p not in DECL_PERMS:
            errs.append(f"perms: unknown {p!r} (known: {sorted(DECL_PERMS)})")
    entry = m.get("entry", {})
    if not isinstance(entry, dict) or not isinstance(entry.get(m["type"]), str):
        errs.append(f'entry: {{"{m["type"]}": "{m["type"]}.json"}}')
    if "…" in json.dumps(m, ensure_ascii=False):
        errs.append('no "…" in texts (the device never shows it)')
    size = len(json.dumps(m, ensure_ascii=False, separators=(",", ":")).encode())
    if size > DECL_MANIFEST_MAX:
        errs.append(f"manifest: {size} bytes, at most {DECL_MANIFEST_MAX} (the X4 refuses bigger ones)")
    return errs


def check_decl_file(folder, m, names):
    """Problems with the layout file of a face or widget (level 1, docs/profiles.md §4)."""
    if m.get("type") == "theme":
        return []
    entry = m.get("entry", {}).get(m["type"], "")
    if entry not in names:
        return [f"{entry or 'entry'}: the layout file is missing"]
    raw = (folder / entry).read_bytes()
    if len(raw) > 12 * 1024:
        return [f"{entry}: {len(raw)} bytes, at most 12288 (the X4 reads it in one go)"]
    try:
        doc = json.loads(raw)
    except Exception as e:
        return [f"{entry}: not JSON ({e})"]
    errs = []
    if doc.get("decl") != DECL_LEVEL:
        errs.append(f'{entry}: "decl": {DECL_LEVEL} is missing')
    layouts = doc.get("layouts")
    if not isinstance(layouts, list) or not layouts:
        return errs + [f"{entry}: needs a list of layouts"]
    sizes = []

    def walk(items, where, depth):
        if not isinstance(items, list):
            errs.append(f"{where}: items must be a list")
            return
        for i, it in enumerate(items):
            t = it.get("t") if isinstance(it, dict) else None
            if t not in DECL_ITEMS:
                errs.append(f"{where}[{i}]: t must be one of {sorted(DECL_ITEMS)}")
                continue
            if t == "image" and "{" not in it.get("src", "{") and it.get("src") not in names:
                errs.append(f"{where}[{i}]: picture {it.get('src')!r} is not in the package")
            if "…" in json.dumps(it, ensure_ascii=False):
                errs.append(f'{where}[{i}]: no "…" in texts')
            if t in ("list", "group"):
                if depth >= 3:
                    errs.append(f"{where}[{i}]: lists and groups go 3 deep at most")
                else:
                    walk(it.get("items", []), f"{where}[{i}].items", depth + 1)

    for k, lay in enumerate(layouts):
        size = lay.get("size") if isinstance(lay, dict) else None
        if not (isinstance(size, list) and len(size) == 2 and all(isinstance(v, int) and v > 0 for v in size)):
            errs.append(f"{entry}: layouts[{k}].size must be [w, h]")
            continue
        sizes.append(f"{size[0]}x{size[1]}")
        walk(lay.get("items", []), f"{entry}: layouts[{k}].items", 0)
    if m.get("screens") and sorted(m["screens"]) != sorted(sizes):
        errs.append(f"screens {m['screens']} do not match the layouts {sizes}")
    return errs


def jpeg_size(b):
    """(width, height, baseline) of a JPEG, or None."""
    if b[:2] != b"\xff\xd8":
        return None
    i = 2
    while i + 4 <= len(b):
        if b[i] != 0xFF:
            return None
        mk = b[i + 1]
        if mk in (0xD8, 0x01, 0xFF) or 0xD0 <= mk <= 0xD7:  # standalone markers and fill bytes (as publish_pkg.py)
            i += 1 if mk == 0xFF else 2
            continue
        ln = struct.unpack(">H", b[i + 2:i + 4])[0]
        if 0xC0 <= mk <= 0xCF and mk not in (0xC4, 0xC8, 0xCC):
            if i + 9 > len(b):
                return None
            h, w = struct.unpack(">HH", b[i + 5:i + 9])
            return w, h, mk == 0xC0
        i += 2 + ln
    return None


def build(folder, out):
    folder = Path(folder)
    mf = folder / "manifest.json"
    if not mf.exists():
        fail(f"{mf} not found")
    m = json.loads(mf.read_text())
    errs = check_manifest(m, "manifest.json")
    # hidden files and everything in hidden folders (.git/, .espos/, ...) stay out
    files = sorted(p for p in folder.rglob("*") if p.is_file() and p.name != "manifest.json"
                   and not any(part.startswith(".") for part in p.relative_to(folder).parts))
    names = [p.relative_to(folder).as_posix() for p in files]
    for n in names:
        if len(n.encode()) > 51:
            errs.append(f"{n}: file names (with folders) are at most 51 bytes")
    if m.get("part", "core") == "core" and m.get("type") in DECL_TYPES:
        errs += check_decl_file(folder, m, names)
    if m.get("part", "core") == "core" and m.get("kind") == "js" and "main.js" not in names:
        errs.append('kind "js" needs main.js')
    if "icon.jpg" in names:
        js = jpeg_size((folder / "icon.jpg").read_bytes())
        if not js or not js[2]:
            errs.append("icon.jpg: must be a baseline JPEG")
        elif js[0] != js[1]:
            errs.append(f"icon.jpg: must be square (120x120), is {js[0]}x{js[1]}")
        elif js[0] != 120:
            print(f"espk: note: icon.jpg is {js[0]}x{js[0]}; 120x120 is best", file=sys.stderr)
    if errs:
        fail("\n  ".join(["problems:"] + errs))
    man = json.dumps(m, ensure_ascii=False, separators=(",", ":")).encode()
    head = struct.pack("<4sHHII", b"ESPK", 1, 0, len(man), len(files))
    off = 16 + pad16(len(man)) + 64 * len(files)
    table, blobs = b"", []
    for n, p in zip(names, files):
        data = p.read_bytes()
        off = pad16(off)
        table += struct.pack("<52sIII", n.encode(), off, len(data), zlib.crc32(data) & 0xFFFFFFFF)
        blobs.append((off, data))
        off += len(data)
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "wb") as f:
        f.write(head + man + b"\0" * (pad16(len(man)) - len(man)) + table)
        for o, data in blobs:
            f.write(b"\0" * (o - f.tell()))
            f.write(data)
    print(f"{out.name}: {m['id']} {m.get('version')} part {m.get('part', 'core')}, {len(files)} files, {out.stat().st_size} bytes")
    return out


def read(path):
    """(manifest, [(name, off, size, crc)], bytes), checked; exits on a problem."""
    b = Path(path).read_bytes()
    if len(b) < 16 or b[:4] != b"ESPK":
        fail(f"{path}: not an .espk package file")
    magic, fmt, flags, mlen, nf = struct.unpack_from("<4sHHII", b, 0)
    if fmt != 1:
        fail(f"{path}: format {fmt} (this tool knows 1)")
    if mlen > 32768 or nf > 512:
        fail(f"{path}: header out of range")
    try:
        m = json.loads(b[16:16 + mlen].decode())
    except Exception as e:
        fail(f"{path}: manifest is not JSON ({e})")
    t0 = 16 + pad16(mlen)
    files, prev = [], t0 + 64 * nf
    for i in range(nf):
        raw, off, size, crc = struct.unpack_from("<52sIII", b, t0 + 64 * i)
        name = raw.split(b"\0")[0].decode()
        if off % 16 or off < prev or off + size > len(b):
            fail(f"{path}: {name}: bad offset/size")
        if (zlib.crc32(b[off:off + size]) & 0xFFFFFFFF) != crc:
            fail(f"{path}: {name}: CRC32 does not match")
        if not name or name.startswith("/") or ".." in name or name == "manifest.json":
            fail(f"{path}: bad file name {name!r}")
        files.append((name, off, size, crc))
        prev = off + size
    return m, files, b


def check(paths):
    for p in paths:
        m, files, b = read(p)
        errs = check_manifest(m, Path(p).name)
        if errs:
            fail("\n  ".join(["problems:"] + errs))
        print(f"{Path(p).name}: OK  {m['id']} {m.get('version')} (code {m.get('code')}) part {m.get('part', 'core')}, "
              f"{len(files)} files, {len(b)} bytes, sha256 {hashlib.sha256(b).hexdigest()}")
        for n, off, size, crc in files:
            print(f"    {n:40} {size:10} bytes  crc {crc:08x}")


# A part's choice in the store (docs/packages.md §4): its group, its English label and that label in other languages
# ({"vi": "Nhẹ (10 hình/giây)"}), the default of its group. Kept in the part's manifest (or a builder's
# <id>-<version>-catalog.json, or apps/catalog-text.json "parts"); check_catalog() refuses a catalog that lost them.
PART_KEYS = ("group", "label", "labels", "default", "board")  # board: the part is for that board ("es3c28p"): the device takes it there


def check_catalog(cat, espk_dir=None):
    """Problems with a store catalog (a list of strings): parts that lost their choice (every part of a multi-part
    package must be required or in a group; one default at most a group, a label for each), add-on fields that the
    package's own core manifest has but the entry lost (espk_dir: where the .espk files are)."""
    errs = []
    cores = {}
    if espk_dir:
        for f in Path(espk_dir).glob("*.espk"):
            try:
                m, _, _ = read(f)
            except SystemExit:
                continue
            if m.get("part", "core") == "core":
                cores[(m["id"], m.get("code"))] = m
    for e in cat.get("packages", []):
        pid = e.get("id", "?")
        parts = e.get("parts", [])
        if not parts:
            errs.append(f"{pid}: no parts")
        groups = {}
        for p in parts:
            for k in ("id", "url", "size", "sha256"):
                if k not in p:
                    errs.append(f"{pid} part {p.get('id', '?')}: no {k}")
            if not p.get("req") and not p.get("group"):
                errs.append(f"{pid} part {p.get('id')}: neither required nor in a group (the store would install it always)")
            if p.get("group"):
                groups.setdefault(p["group"], []).append(p)
                if not p.get("label"):
                    errs.append(f"{pid} part {p['id']}: a group's part needs a label")
                if p.get("labels") is not None and not isinstance(p["labels"], dict):
                    errs.append(f"{pid} part {p['id']}: labels must be {{\"<lang>\": \"...\"}}")
        for g, ps in groups.items():
            if sum(1 for p in ps if p.get("default")) > 1:
                errs.append(f"{pid} group {g}: more than one default")
        m = cores.get((pid, e.get("code")))
        if m:
            for k in ("addon_of", "set", "needs", "type"):
                if k in m and e.get(k) != m[k]:
                    errs.append(f"{pid}: {k} is {m[k]!r} in its manifest, {e.get(k)!r} in the catalog")
    return errs


def entry(base, paths, show=False):
    core, parts = None, []
    for p in paths:
        m, files, b = read(p)
        part = m.get("part", "core")
        if part == "core":
            core = m
        e = {"id": part, "url": f"{base.rstrip('/')}/{Path(p).name}", "size": len(b), "sha256": hashlib.sha256(b).hexdigest()}
        if part == "core" or m.get("req"):
            e["req"] = True
        for k in PART_KEYS:
            if k in m:
                e[k] = m[k]
        parts.append(e)
    if not core:
        fail("--entry needs the core part")
    out = {k: core[k] for k in ("id", "name", "version", "code", "kind", "engine", "min_fw", "category", "author", "desc",
                                "icon", "perms", "addon_of", "set", "needs", "type", "runtime", "api", "requires", "screens", "refresh",
                                "i18n", "lang", "font")
           if k in core}
    if core.get("type") not in DECL_TYPES:
        out.setdefault("engine", "js" if core.get("kind") == "js" else "")
    out["parts"] = parts
    if show:
        print(json.dumps(out, ensure_ascii=False, indent=1))
    return out


def install_sim(sim, paths, vol):
    by_id = {}
    for p in paths:
        m, files, b = read(p)
        by_id.setdefault(m["id"], []).append((m, files, b))
    for pid, parts in by_id.items():
        core = next((m for m, _, _ in parts if m.get("part", "core") == "core"), None)
        if not core:
            fail(f"{pid}: the core part is needed")
        if vol == "flash" and core.get("kind") != "js" and core.get("engine") not in ("lang", "font", "wake"):
            fail(f"{pid}: only kind js, languages and fonts go to {sim}/fs/pkgs; native packages: use the card "
                 f"(default) or the store")
        root = Path(sim) / ("sd/espos/pkgs" if vol == "sd" else "fs/pkgs") / pid
        if root.exists():
            for f in sorted(root.rglob("*"), reverse=True):
                f.unlink() if f.is_file() else f.rmdir()
        root.mkdir(parents=True, exist_ok=True)
        allf = []
        for m, files, b in parts:
            for n, off, size, crc in files:
                (root / n).parent.mkdir(parents=True, exist_ok=True)
                (root / n).write_bytes(b[off:off + size])
                allf.append({"n": n, "s": size, "c": crc})
        man = dict(core)
        man.pop("part", None)
        man["parts"] = [m.get("part", "core") for m, _, _ in parts]
        man["files"] = allf
        (root / "manifest.json").write_text(json.dumps(man, ensure_ascii=False))
        print(f"{pid}: {len(allf)} files in {root}")


def main(argv):
    if len(argv) >= 2 and argv[0] == "--check":
        return check(argv[1:])
    if len(argv) >= 3 and argv[0] == "--entry":
        return entry(argv[1], argv[2:], True)
    if len(argv) >= 3 and argv[0] == "--install-sim":
        vol = "sd"
        if "--vol" in argv:
            i = argv.index("--vol")
            vol = argv[i + 1]
            argv = argv[:i] + argv[i + 2:]
        return install_sim(argv[1], argv[2:], vol)
    if len(argv) == 2 and not argv[0].startswith("-"):
        return build(argv[0], argv[1])
    print(__doc__)
    sys.exit(2)


if __name__ == "__main__":
    main(sys.argv[1:])
