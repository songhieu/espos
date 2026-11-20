#pragma GCC optimize("Os")  // lists, files, the cache: built small; the pixel loop below keeps -O2
#include "photo.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include <algorithm>
#include <map>

#include "desk.h"
#include "home.h"
#include "jpeg.h"
#include "rom/tjpgd.h"
#include "trace.h"

namespace photo {
namespace {

uint32_t ver = 0;
uint32_t views = 0;  // Home builds, for pictures that change "each time the screen opens"

// The grid the widgets use (widgets.cpp): portrait 4x7 in 304x436, landscape 6x4 in 464x276.
struct Grid {
  int cols, rows, cw, ch;
};
Grid gridFor(bool land) {
  const int aw = land ? 464 : 304, ah = land ? 276 : 436, cols = land ? 6 : 4, rows = land ? 4 : 7;
  return {cols, rows, (aw - (cols - 1) * 6) / cols, (ah - (rows - 1) * 6) / rows};
}

String pOf(const String& opt) {
  // "<mode><every><fit><ink><brightness>|ids" -> fit + 'j' + brightness
  if (opt.length() < 6) return "cj50";
  return String(opt[2] == 'n' ? 'n' : 'c') + "j" + opt.substring(4, 6);
}

std::vector<int> idsOf(const String& opt) {
  std::vector<int> out;
  const int bar = opt.indexOf('|');
  if (bar < 0) return out;
  int start = bar + 1;
  while (start < (int)opt.length()) {
    int comma = opt.indexOf(',', start);
    if (comma < 0) comma = opt.length();
    const int id = opt.substring(start, comma).toInt();
    if (id > 0) out.push_back(id);
    start = comma + 1;
  }
  return out;
}

// ---------------------------------------------------------------- JPEG decoding (ROM TJpgDec)
struct Src {
  const uint8_t* data;
  size_t len, pos;
  uint16_t* out;
  int w, h;
};

__attribute__((optimize("O2"))) UINT input(JDEC* jd, BYTE* buf, UINT n) {
  Src* s = (Src*)jd->device;
  n = min<size_t>(n, s->len - s->pos);
  if (buf) memcpy(buf, s->data + s->pos, n);
  s->pos += n;
  return n;
}

__attribute__((optimize("O2"))) UINT output(JDEC* jd, void* bitmap, JRECT* r) {
  Src* s = (Src*)jd->device;
  const uint8_t* rgb = (const uint8_t*)bitmap;
  for (int y = r->top; y <= r->bottom; y++) {
    for (int x = r->left; x <= r->right; x++, rgb += 3) {
      if (x < s->w && y < s->h) s->out[y * s->w + x] = lv_color_to_u16(lv_color_make(rgb[0], rgb[1], rgb[2]));
    }
  }
  return 1;
}

struct Decoded {
  lv_image_dsc_t dsc;
  uint32_t used;
  bool pinned;  // shown by a Home widget: never evicted while that Home exists
};
std::map<String, Decoded> cache;  // decoded pictures by path (PSRAM), a few kept

uint8_t* readFile(const String& path, size_t& len) {
  len = 0;
  fs::File f = LittleFS.exists(path) ? LittleFS.open(path, "r") : fs::File();
  if (!f) return nullptr;
  len = f.size();
  uint8_t* jpg = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (jpg && f.read(jpg, len) != len) heap_caps_free(jpg), jpg = nullptr;
  f.close();
  return jpg;
}

// cover: the JPEG is another size (a screen copy for a screen of another size, one size each): scaled to cover w x h,
// kept as path + "@".
const lv_image_dsc_t* decode(String key, int w, int h, bool cover = false) {
  const String path = key;
  if (cover) key += '@';
  auto it = cache.find(key);
  if (it != cache.end()) {
    it->second.used = millis();
    return &it->second.dsc;
  }
  size_t len;
  uint8_t* jpg = readFile(path, len);
  const uint32_t t0 = millis();
  uint16_t* px = jpg ? (cover ? jpeg::cover(jpg, len, w, h) : decodeJpg(jpg, len, w, h)) : nullptr;
  heap_caps_free(jpg);
  trace::add("photo %s: %s in %lu ms", path.c_str(), px ? "decoded" : "not decoded", millis() - t0);
  if (!px) return nullptr;
  // Keep at most 6 decoded pictures (a few hundred KB of PSRAM).
  while (cache.size() >= 6) {
    auto oldest = cache.end();
    for (auto c = cache.begin(); c != cache.end(); ++c)
      if (!c->second.pinned && (oldest == cache.end() || c->second.used < oldest->second.used)) oldest = c;
    if (oldest == cache.end()) break;  // all pinned: keep them, grow by one
    heap_caps_free((void*)oldest->second.dsc.data);
    cache.erase(oldest);
  }
  Decoded& d = cache[key];
  d.used = millis();
  d.pinned = false;
  d.dsc = {};
  d.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  d.dsc.header.w = w;
  d.dsc.header.h = h;
  d.dsc.header.stride = w * 2;
  d.dsc.data_size = w * h * 2;
  d.dsc.data = (const uint8_t*)px;
  return &d.dsc;
}

}  // namespace

uint16_t* decodeJpg(const uint8_t* jpg, size_t len, int w, int h) {
  uint16_t* px = (uint16_t*)heap_caps_calloc(w * h, 2, MALLOC_CAP_SPIRAM);
  void* work = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  JDEC jd;
  Src s = {jpg, len, 0, px, w, h};
  const bool ok = px && work && jd_prepare(&jd, input, work, 4096, &s) == JDR_OK && jd_decomp(&jd, output, 0) == JDR_OK;
  heap_caps_free(work);
  if (!ok) heap_caps_free(px), px = nullptr;
  return px;
}

uint8_t* readCopy(int id, int w, int h, size_t& len) {
  return readFile(File{id, String(w) + "x" + String(h), "cj50"}.path(), len);
}

String File::path() const { return "/ph/" + String(id) + "_" + dim + "_" + p + ".jpg"; }

std::vector<File> wanted() {
  std::vector<File> out;
  static const char* KEYS[] = {"s3.p1", "s3.p2", "s3.l1", "s3.l2"};
  for (const char* key : KEYS) {
    const String layout = desk::setting(key, "");
    if (!layout.length()) continue;
    const bool land = key[3] == 'l';
    const Grid g = gridFor(land);
    JsonDocument d;
    if (deserializeJson(d, layout) != DeserializationError::Ok) continue;
    for (JsonArrayConst it : d["i"].as<JsonArrayConst>()) {
      if (strcmp(it[0] | "", "photo")) continue;
      const int w = it[3] | 1, h = it[4] | 1;
      const String opt = it[6] | "";
      const String dim = String(w * g.cw + (w - 1) * 6) + "x" + String(h * g.ch + (h - 1) * 6);
      for (int id : idsOf(opt)) {
        if (out.size() >= 24) break;
        out.push_back({id, dim, pOf(opt)});
      }
    }
  }
  // The saver's pages (s3.saver_pages, docs/saver.md): photo elements at their box, photo widgets at their size,
  // pictured backgrounds at full screen.
  std::vector<File> extra;  // added at the end: no full-screen copies of these
  {
    JsonDocument d;
    const String pages = desk::setting("s3.saver_pages", "");
    if (pages.length() && deserializeJson(d, pages) == DeserializationError::Ok) {
      size_t n = 0;
      for (const char* key : {"p", "l", "np", "nl"}) {
        const bool landPage = key[0] == 'l' || key[1] == 'l';
        JsonArrayConst list = d[key].is<JsonArrayConst>() ? d[key].as<JsonArrayConst>() : JsonArrayConst();
        auto page = [&](JsonObjectConst pg) {
          const int bgId = pg["bg"]["t"] == "photo" ? (int)(pg["bg"]["id"] | 0) : 0;
          if (bgId > 0 && n++ < 16) extra.push_back({bgId, landPage ? "480x320" : "320x480", "cj50"});
          for (JsonObjectConst e : pg["e"].as<JsonArrayConst>()) {
            if (e["t"] == "photo") {  // one picture, or a slideshow of a set or of all (docs/saver.md §2)
              const String dim = String((int)(e["w"] | 1)) + "x" + String((int)(e["h"] | 1));
              std::vector<int> set = (e["all"] | 0) ? idsOf("|" + desk::setting("s3.photos", "")) : std::vector<int>();
              for (JsonVariantConst id : e["ids"].as<JsonArrayConst>())
                if (!(e["all"] | 0) && (id | 0) > 0) set.push_back(id | 0);
              if (set.empty() && (e["id"] | 0) > 0) set.push_back(e["id"] | 0);
              for (int id : set)
                if (n++ < 16) extra.push_back({id, dim, "cj50"});
            } else if (e["t"] == "w" && e["k"] == "photo") {
              const char s = *(e["s"] | "S");
              const String dim = s == 'S' ? "144x138" : s == 'M' ? "300x138" : "300x286";
              const String opt = e["op"] | "";
              for (int id : idsOf(opt))
                if (n++ < 16) extra.push_back({id, dim, pOf(opt)});
            }
          }
        };
        if (!list.isNull()) for (JsonObjectConst pg : list) page(pg);
        else if (d[key].is<JsonObjectConst>()) page(d[key].as<JsonObjectConst>());
      }
    }
  }
  // The device's pictures (s3.photos, picked on the web) and those of old layouts, at full
  // screen in both orientations: the Photos app, photo widgets, the wallpaper and the saver.
  std::vector<int> ids = idsOf("|" + desk::setting("s3.photos", ""));
  for (const File& f : out)
    if (std::find(ids.begin(), ids.end(), f.id) == ids.end()) ids.push_back(f.id);
  if (ids.size() > 8) ids.resize(8);  // LittleFS is 1.2 MB since the PawBoop table
  for (int id : ids)
    for (const char* dim : {"320x480", "480x320"}) out.push_back({id, dim, "cj50"});
  // Home's photo backgrounds (docs/widgets.md §4) at the screen size of each orientation that shows them, even when
  // not in s3.photos: the landscape pages' are s3.home_l's, or the portrait ones while landscape is seeded from them.
  std::vector<int> port, land;
  home::bgPhotos(port, land);
  for (int id : port) extra.push_back({id, "320x480", "cj50"});
  for (int id : land) extra.push_back({id, "480x320", "cj50"});
  for (const File& f : extra)
    if (std::find_if(out.begin(), out.end(), [&](const File& o) { return o.path() == f.path(); }) == out.end())
      out.push_back(f);
  return out;
}

bool have(const File& f) { return LittleFS.exists(f.path()); }

bool save(const File& f, const uint8_t* data, size_t len) {
  if (!LittleFS.exists("/ph")) LittleFS.mkdir("/ph");
  fs::File out = LittleFS.open(f.path(), "w");
  if (!out) return false;
  const bool ok = out.write(data, len) == len;
  out.close();
  if (ok) ver++;
  trace::add("photo saved %s (%u bytes)", f.path().c_str(), (unsigned)len);
  return ok;
}

void prune(const std::vector<File>& keep) {
  fs::File dir = LittleFS.open("/ph");
  if (!dir || !dir.isDirectory()) return;
  std::vector<String> drop;
  for (fs::File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    const String path = String("/ph/") + e.name();
    bool used = false;
    for (const File& k : keep) used |= k.path() == path;
    if (!used) drop.push_back(path);
  }
  for (const String& p : drop) LittleFS.remove(p), trace::add("photo removed %s", p.c_str());
}

uint32_t version() { return ver; }

void unpinAll() {
  for (auto& c : cache) c.second.pinned = false;
}

// The screen copies are 320x480 and 480x320 (the web makes those); a screen of another size (the 2.8" board's 360x480,
// 480x360 turned) takes the copy of its orientation, scaled to cover it.
static const char* screenDim(int w, int h) { return w > h ? "480x320" : "320x480"; }

int screenCount(int w, int h) {
  const char* dim = screenDim(w, h);
  int n = 0;
  for (const File& f : wanted())
    if (f.dim == dim && have(f)) n++;
  return n;
}

const lv_image_dsc_t* screen(uint32_t n, int w, int h, bool pin) {
  const char* dim = screenDim(w, h);
  std::vector<String> here;
  for (const File& f : wanted())
    if (f.dim == dim && have(f)) here.push_back(f.path());
  if (here.empty()) return nullptr;
  const String& p = here[n % here.size()];
  const bool cover = w * h != 320 * 480 || (w != 320 && w != 480);
  const lv_image_dsc_t* d = decode(p, w, h, cover);
  if (d && pin) cache[cover ? p + '@' : p].pinned = true;  // a Home photo widget shows it: the saver slideshow must not evict it
  return d;
}

bool show(lv_obj_t* parent, const String& option, int w, int h) {
  const std::vector<int> ids = idsOf(option);
  if (ids.empty()) return false;
  // Which picture: mode f (the first), s (in turn) or r (random), changing at each view, each
  // hour or each day.
  const char mode = option.length() ? option[0] : 'f', every = option.length() > 1 ? option[1] : 'h';
  const time_t now = time(nullptr);
  const uint32_t slot = every == 'v' ? views++ : every == 'd' ? (now + 7 * 3600) / 86400 : now / 3600;
  size_t start = mode == 'f' ? 0 : mode == 's' ? slot % ids.size() : (slot * 2654435761u >> 7) % ids.size();
  const String dim = String(w) + "x" + String(h), p = pOf(option);
  for (size_t k = 0; k < ids.size(); k++) {  // the chosen one, else the next one that is here
    const File f{ids[(start + k) % ids.size()], dim, p};
    if (!have(f)) continue;
    const lv_image_dsc_t* img = decode(f.path(), w, h);
    if (!img) continue;
    cache[f.path()].pinned = true;
    lv_obj_t* i = lv_image_create(parent);
    lv_image_set_src(i, img);
    lv_obj_set_pos(i, 0, 0);
    return true;
  }
  return false;
}

}  // namespace photo
