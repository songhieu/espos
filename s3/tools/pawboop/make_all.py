"""PawBoop's packages for the store, from the game designer's hand-off 3 (handoff_all/: ten dogs, eight maps, the voices,
the dog assistant). Everything is copied unchanged and checked against the hand-off's SHA-256 lists.

  vn.pawboop.pet  2.0.0   the pet game: its screen pieces (pawboop_ui.pbu, hand-off 1) and the bubble fonts
                          (Lexend, the page's font, with every letter of every dog's lines), icon.jpg
  vn.pawboop.run  2.0.0   Coin Run (add-on of the pet): core/run_things.pbs, core/run_ui.pbu, run.json (8 maps)
  vn.pawboop.dog.<dog>    one dog (add-on of the pet, "set": "dog"): pet.pbs (its 29 pet clips), run.pbs (its run
                          clips), voice.pba (176 lines), assist.pba (the assistant's 42 lines + the 73-clip number
                          kit), icon.jpg (the phone game's portrait). The chosen dog is the pet, the runner and the
                          assistant's voice (setting s3.pet_dog).
  vn.pawboop.map.<map>    one map of the run (add-on of the pet, "set": "map", "needs": vn.pawboop.run): its road at
                          20 pictures a second and the gate to the next map of the ring (used when that map is in too)

  python3 tools/pawboop/make_all.py [--out DIR] [--catalog]
                                                        [--sim DIR [--dogs a,b|all] [--maps a,b|all]]
    --out      where the .espk files go (default build-pkgs/espos/s3, beside the other packages; not uploaded)
    --catalog  build-pkgs/espos/s3/catalog.json again afterwards (tools/i18n.py catalog)
    --sim      install the pet, the run and the dogs/maps given on the simulator's memory card (tools/espk.py)
"""
import argparse
import hashlib
import io
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import espk  # noqa: E402
import i18n as I18N  # noqa: E402  (ESF1 font files, the catalog)

HAND = Path("handoff/esp32")
SRC = HAND / "handoff_all"
# The screen pieces re-packed by the lead for ten dogs (handoff_ui2/NOTE.md): hand-off 1's without the 41 say_*
# bubbles and without "Biscuit" in screen / splash, plus name_<dog> and wake_<dog>.
UI_PACK = HAND / "handoff_ui2/pawboop_ui.pbu"
UI_SHA = "db94d6e6bcedfb88b5db21b8d4ce719a5d1f96e7356e3c0a1e58ebd21abd229d"
PORTRAITS = Path("handoff/art")  # the phone game's dog portraits
LEXEND = Path("handoff/fonts")
PET_ICON = ROOT / "build-pkgs/espos/s3/vn.pawboop.pet-1.0.0.jpg"

MIN_FW = 501  # firmware 0.5.1: dogs, maps, the assistant (src/pet/dogs.cpp, assist.cpp)
PET_MIN_FW = 502  # 0.5.2: the pet's fonts are format 1 (kerning); 0.5.1's src/fonts.cpp refuses format 1
PET_V, RUN_V, PIECE_V = "2.0.2", "2.0.0", "1.0.0"  # pet 2.0.1: the re-packed screen pieces; 2.0.2: kerned fonts


def code_of(v):
    a, b, c = (list(map(int, v.split("."))) + [0, 0, 0])[:3]
    return a * 10000 + b * 100 + c


DOG_EN = {  # the store's English line for each character (dogs.json "character" is the Vietnamese)
    "bao": "The grumpy uncle at the end of the lane: a glare, a grunt, then a wink.",
    "momo": "The chill one, with a whole philosophy of being lazy.",
    "boop": "The hyperactive kid who shouts everything.",
    "leaf": "The sweetheart: all sugar, all hearts.",
    "volt": "The self-made star, always playing to the crowd.",
    "byte": "The call-centre robot: polite, by the script.",
    "shiba": "The cool street guy of few words.",
    "corgi": "The southern princess.",
    "husky": "The drama queen, straight out of a Vietnamese opera.",
    "golden": "The fired-up coach.",
}
MAP_VI = {"bazaar": "Ả Rập", "construction": "Công trường", "carnival": "Rạp xiếc", "city": "Thành phố",
          "lantern": "Phố đèn lồng", "park": "Công viên", "snow": "Làng tuyết", "beach": "Biển"}  # sim/run.html's names
BG = {"top": (0xFF, 0xF3, 0xD6), "bottom": (0xFF, 0xC4, 0x8C)}


def sha(b):
    return hashlib.sha256(b).hexdigest()


def checked(base: Path, manifest: dict):
    """{relative path: bytes}, every file checked against the hand-off's list."""
    out = {}
    for f in manifest["files"]:
        b = (base / f["path"]).read_bytes()
        if len(b) != f["bytes"] or sha(b) != f["sha256"]:
            sys.exit(f"{base / f['path']}: size or SHA-256 differs from the hand-off's manifest.json")
        out[f["path"]] = b
    return out


def jpeg(im: Image.Image) -> bytes:
    b = io.BytesIO()
    im.convert("RGB").save(b, "JPEG", quality=90, progressive=False, optimize=True, subsampling=0)
    return b.getvalue()


def gradient(n, top, bottom):
    im = Image.new("RGBA", (n, n))
    d = ImageDraw.Draw(im)
    for y in range(n):
        t = y / (n - 1)
        d.line([(0, y), (n, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,))
    return im


def dog_icon(dog):
    """120x120: the phone game's round portrait on the pet's warm square."""
    n = 480
    bg = gradient(n, BG["top"], BG["bottom"])
    p = Image.open(PORTRAITS / f"portrait_{dog}.png").convert("RGBA")
    s = int(n * 0.92)
    p.thumbnail((s, s), Image.LANCZOS)
    bg.alpha_composite(p, ((n - p.size[0]) // 2, (n - p.size[1]) // 2))
    return jpeg(bg.resize((120, 120), Image.LANCZOS))


def map_icon(pbv: bytes):
    """120x120: the middle of the map's first road picture."""
    import struct
    _, w, h, _, nclips, nframes = struct.unpack_from("<4sHHHHI", pbv, 0)
    offs = struct.unpack_from("<%dI" % (nframes + 1), pbv, 16 + 24 * nclips)
    im = Image.open(io.BytesIO(pbv[offs[0]:offs[1]])).convert("RGB")
    s = min(w, h)
    im = im.crop(((w - s) // 2, h - s, (w + s) // 2, h)).resize((120, 120), Image.LANCZOS)
    return jpeg(im)


def build(out: Path, man: dict, files: list) -> Path:
    """files: [(name, bytes)] -> out/<id>-<version>.espk (one part: core)"""
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        (t / "manifest.json").write_text(json.dumps(dict(man, part="core"), ensure_ascii=False))
        for name, b in files:
            (t / name).parent.mkdir(parents=True, exist_ok=True)
            (t / name).write_bytes(b)
        name = f"{man['id']}-{man['version']}{'-core' if man['id'] in ('vn.pawboop.pet', 'vn.pawboop.run') else ''}.espk"
        return espk.build(t, out / name)


# ---------------------------------------------------------------- the bubble fonts (Lexend, SIL OFL 1.1)
VI_LETTERS = ("aàáảãạăằắẳẵặâầấẩẫậbcdđeèéẻẽẹêềếểễệfghiìíỉĩịjklmnoòóỏõọôồốổỗộơờớởỡợpqrstuùúủũụưừứửữựvwxyỳýỷỹỵz")
FONTS = [("b12", "Lexend-Bold.ttf", 12, True), ("b14", "Lexend-Bold.ttf", 14, True),
         ("s11", "Lexend-SemiBold.ttf", 11, False)]


def font_files(dogs: dict):
    chars = set(range(0x20, 0x7F)) | {ord(c) for c in VI_LETTERS + VI_LETTERS.upper()} | {ord(c) for c in "…·“”‘’–—°«»"}
    for d in dogs["dogs"].values():
        chars |= {ord(c) for c in d["name"]}
        for ls in d["lines"].values():
            for l in ls:
                chars |= {ord(c) for c in l["text"]}
        for ls in d["assistant"]["lines"].values():
            for l in ls:
                chars |= {ord(c) for c in l["text"]}
    for w in dogs["assistant"]["number_kit"]["words"].values():
        chars |= {ord(c) for c in w + w.lower()}
    from fontTools.ttLib import TTFont
    out = []
    with tempfile.TemporaryDirectory() as tmp:
        for tag, ttf, px, bold in FONTS:
            cmap = TTFont(LEXEND / ttf).getBestCmap()
            have = sorted(c for c in chars if c in cmap)
            missing = sorted(c for c in chars if c not in cmap and c > 0x20)
            if missing:
                print(f"font {tag}: not in Lexend: {''.join(map(chr, missing))}")
            c = Path(tmp) / f"{tag}.c"
            subprocess.run([str(I18N.CONV), "--bpp", "4", "--size", str(px), "--no-compress", "--format",
                            "lvgl", "--lv-include", "lvgl.h", "--font", str(LEXEND / ttf), "-r", I18N.ranges(have),
                            "-o", str(c), "--lv-font-name", "f"], check=True, stdout=subprocess.DEVNULL)
            out.append((f"font/{tag}.bin", esf_kerned(c, px, bold)))
    return out


def esf_kerned(path_c, px, bold):
    """The font file of tools/i18n.py esf() (format 0) as format 1: with Lexend's kerning (LVGL's kerning classes),
    so the bubble's words are spaced and measured like the page's canvas (src/fonts.cpp reads it)."""
    import re
    import struct
    blob = bytearray(I18N.esf(path_c, px, bold))
    src = Path(path_c).read_text()

    def arr(name):
        m = re.search(name + r"\[\]\s*=\s*\{(.*?)\};", src, re.S)
        body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
        return [int(v) for v in re.findall(r"-?\d+", body)]
    left, right, vals = arr("kern_left_class_mapping"), arr("kern_right_class_mapping"), arr("kern_class_values")
    lc = int(re.search(r"\.left_class_cnt\s*=\s*(\d+)", src).group(1))
    rc = int(re.search(r"\.right_class_cnt\s*=\s*(\d+)", src).group(1))
    scale = int(re.search(r"\.kern_scale\s*=\s*(\d+)", src).group(1))
    n = struct.unpack_from("<I", blob, 20)[0]
    assert len(left) == len(right) == n + 1 and len(vals) == lc * rc, (len(left), n, len(vals), lc, rc)
    # Format 1: 4 more bytes after the 40-byte head (the kerning block's offset): every offset moves by 4 (aligned).
    head, rest = blob[:40], blob[40:]
    off_list, off_dsc, off_bmp = struct.unpack_from("<III", head, 24)
    shift = 4 if (off_dsc + 4) % 4 == 0 else 4
    struct.pack_into("<III", head, 24, off_list + shift, off_dsc + shift, off_bmp + shift)
    head[7] = 1  # format
    body = bytes(head) + b"\0" * 4 + bytes(rest)
    ko = (len(body) + 3) & ~3
    kern = struct.pack("<HHHH", lc, rc, scale, 0) + bytes(left) + bytes(right) + bytes(v & 0xFF for v in vals)
    out = bytearray(body + b"\0" * (ko - len(body)) + kern)
    struct.pack_into("<I", out, 40, ko)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=ROOT / "build-pkgs/espos/s3")
    ap.add_argument("--catalog", action="store_true")
    ap.add_argument("--sim", type=Path)
    ap.add_argument("--dogs", default="all")
    ap.add_argument("--maps", default="all")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    pet = checked(SRC / "pawboop_pet", json.loads((SRC / "pawboop_pet/manifest.json").read_text()))
    run = checked(SRC / "pawboop_run", json.loads((SRC / "pawboop_run/manifest.json").read_text()))
    dogs = json.loads(pet["dogs.json"])
    rj = json.loads(run["run.json"])
    ui = UI_PACK.read_bytes()
    if sha(ui) != UI_SHA:
        sys.exit(f"{UI_PACK}: not the re-packed pawboop_ui.pbu (handoff_ui2)")
    built = {}

    # ---- the pet game
    man = {"id": "vn.pawboop.pet", "name": "PawBoop", "version": PET_V, "code": code_of(PET_V), "kind": "native",
           "engine": "pet", "min_fw": PET_MIN_FW, "category": "game", "author": "PawBoop",
           "desc": "Raise a dog: feed it, bathe it, play ball. Ten dogs, each with its own character and Vietnamese voice.",
           "icon": {"bg": ["#ffcf7a", "#e8892c"], "glyph": "play"}, "perms": ["sound", "notify"],
           "i18n": {"vi": {"name": "PawBoop", "desc": "Nuôi chó: cho ăn, tắm, chơi bóng. Mười con chó, mỗi con một tính cách, nói tiếng Việt."}}}
    icon = PET_ICON.read_bytes()
    built["vn.pawboop.pet"] = [build(a.out, man, [("pawboop_ui.pbu", ui), ("icon.jpg", icon)] + font_files(dogs))]
    (a.out / f"vn.pawboop.pet-{PET_V}.jpg").write_bytes(icon)

    # ---- the run game (its roads come with the maps)
    sys.path.insert(0, str(ROOT / "tools/pawboop_run"))
    import make_pkgs as RUN  # noqa: E402  (its icon: Biscuit mid-leap on the run's orange)
    with tempfile.NamedTemporaryFile(suffix=".pbs") as t:
        t.write(run["dogs/run_corgi.pbs"])
        t.flush()
        ricon = RUN.icon(Path(t.name))
    man = {"id": "vn.pawboop.run", "name": "Coin Run", "addon_of": "vn.pawboop.pet", "version": RUN_V,
           "code": code_of(RUN_V), "kind": "native", "engine": "run", "min_fw": MIN_FW, "category": "game",
           "author": "PawBoop", "desc": "Your dog runs through eight worlds, picking up coins for its food. Add the maps below.",
           "icon": {"bg": ["#ffb347", "#ff7a00"], "glyph": "play"}, "perms": ["sound", "notify"],
           "i18n": {"vi": {"name": "Chạy lấy xu", "desc": "Chó của bạn chạy qua tám thế giới, lấy xu mua đồ ăn. Thêm các màn ở dưới."}}}
    files = [("core/run_things.pbs", run["core/run_things.pbs"]), ("core/run_ui.pbu", run["core/run_ui.pbu"]),
             ("run.json", run["run.json"]), ("icon.jpg", ricon)]
    built["vn.pawboop.run"] = [build(a.out, man, files)]
    (a.out / f"vn.pawboop.run-{RUN_V}.jpg").write_bytes(ricon)

    # ---- the dogs
    for k in dogs["order"]:
        d = dogs["dogs"][k]
        ic = dog_icon(k)
        man = {"id": f"vn.pawboop.dog.{k}", "name": d["name"], "addon_of": "vn.pawboop.pet", "set": "dog",
               "version": PIECE_V, "code": code_of(PIECE_V), "kind": "native", "engine": "pet", "min_fw": MIN_FW,
               "category": "game", "author": "PawBoop", "desc": DOG_EN[k],
               "icon": {"bg": ["#ffcf7a", "#e8892c"], "glyph": "play"},
               "i18n": {"vi": {"name": d["name"], "desc": f"{d['character']}. {d['reaction']}"}}}
        files = [("pet.pbs", pet[d["pet_pack"]]), ("run.pbs", run[rj["dogs"][k]["file"]]),
                 ("voice.pba", pet[d["voice"]["pack"]]), ("assist.pba", pet[d["assistant"]["pack"]]), ("icon.jpg", ic)]
        built[man["id"]] = [build(a.out, man, files)]
        (a.out / f"{man['id']}-{PIECE_V}.jpg").write_bytes(ic)

    # ---- the maps (a map = its road + the gate to the next map of the ring)
    order = rj["order"]
    for i, m in enumerate(order):
        nx = order[(i + 1) % len(order)]
        road = run[f"roads20/{rj['roads'][m]['file']}"]
        link = run[f"roads20/{rj['roads'][m + '>' + nx]['file']}"]
        ic = map_icon(road)
        en, vi = rj["maps"][m]["name"], MAP_VI[m]
        man = {"id": f"vn.pawboop.map.{m}", "name": en, "addon_of": "vn.pawboop.pet", "set": "map",
               "needs": "vn.pawboop.run", "version": PIECE_V, "code": code_of(PIECE_V), "kind": "native",
               "engine": "run", "min_fw": MIN_FW, "category": "game", "author": "PawBoop",
               "desc": f"A map of Coin Run, with the gate to {rj['maps'][nx]['name']}.",
               "icon": {"bg": ["#ffb347", "#ff7a00"], "glyph": "play"},
               "i18n": {"vi": {"name": vi, "desc": f"Một màn của Chạy lấy xu, kèm cổng sang {MAP_VI[nx]}."}}}
        files = [(rj["roads"][m]["file"], road), (rj["roads"][m + ">" + nx]["file"], link), ("icon.jpg", ic)]
        built[man["id"]] = [build(a.out, man, files)]
        (a.out / f"{man['id']}-{PIECE_V}.jpg").write_bytes(ic)

    total = 0
    for pid, ps in built.items():
        for p in ps:
            n = p.stat().st_size
            total += n
            print(f"{p.name:44} {n:>11,} B")
    print(f"{'all':44} {total:>11,} B")

    if a.catalog:
        I18N.catalog(a.out)
    if a.sim:
        want = ["vn.pawboop.pet", "vn.pawboop.run"]
        want += [f"vn.pawboop.dog.{k}" for k in (dogs["order"] if a.dogs == "all" else [x for x in a.dogs.split(",") if x])]
        want += [f"vn.pawboop.map.{m}" for m in (order if a.maps == "all" else [x for x in a.maps.split(",") if x])]
        paths = [str(p) for w in want for p in built[w]]
        subprocess.run([sys.executable, str(ROOT / "tools/espk.py"), "--install-sim", str(a.sim)] + paths, check=True)


if __name__ == "__main__":
    main()
