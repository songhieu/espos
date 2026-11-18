// The natives of com.espos.j2me.Native (midp.h). Every pixel call checks the arrays and rectangles Java passes
// before drawing: a game cannot write outside its own arrays whatever it sends.
#include "midp.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "jvm.h"
#include "midp_draw.h"

#include <dirent.h>

namespace j2me {

// ---------------------------------------------------------------- host
void MidpHost::post(int type, int a, int b) {
  std::lock_guard<std::mutex> l(mu);
  if (events.size() >= 128) events.pop_front();  // a stalled game: the oldest go
  events.push_back({type, a, b});
}

uint32_t gameBit(int code) {
  switch (code) {
    case -1: case 50: return 1u << 1;   // UP
    case -2: case 56: return 1u << 6;   // DOWN
    case -3: case 52: return 1u << 2;   // LEFT
    case -4: case 54: return 1u << 5;   // RIGHT
    case -5: case 53: return 1u << 8;   // FIRE
    case 49: return 1u << 9;            // GAME_A
    case 51: return 1u << 10;           // GAME_B
    case 55: return 1u << 11;           // GAME_C
    case 57: return 1u << 12;           // GAME_D
  }
  return 0;
}

void MidpHost::key(int code, bool down) {
  std::lock_guard<std::mutex> l(mu);
  const uint32_t bit = gameBit(code);
  if (down) held |= bit, latched |= bit;
  else held &= ~bit;
  if (events.size() >= 128) events.pop_front();
  events.push_back({down ? 1 : 2, code, 0});
}

int MidpHost::poll(int* ev) {
  std::lock_guard<std::mutex> l(mu);
  if (events.empty()) return 0;
  const Ev e = events.front();
  events.pop_front();
  ev[0] = e.a, ev[1] = e.b;
  return e.type;
}

uint32_t MidpHost::keyStates() {
  std::lock_guard<std::mutex> l(mu);
  const uint32_t s = held | latched;  // a key tapped between two reads still counts once
  latched = 0;
  return s;
}

namespace {

MidpHost* H = nullptr;

// ---- argument helpers
struct Px {
  uint32_t* p = nullptr;
  int len = 0;
};
Px ints(Object* a) {
  if (!a) return {};
  return {reinterpret_cast<uint32_t*>(a->elems<int32_t>()), a->length};
}

// A target from (array, w, h) and a clip (cx, cy, cw, ch), or false when they do not fit together.
bool target(Vm& vm, Thread& t, Slot* a, int clipAt, draw::Target& out) {
  Px d = ints(a[0].a);
  const int w = a[1].i, h = a[2].i;
  if (!d.p) return vm.throwNew(t, "java/lang/NullPointerException"), false;
  if (w <= 0 || h <= 0 || (int64_t)w * h > d.len) return vm.throwNew(t, "java/lang/IllegalArgumentException", "target"), false;
  int cx = a[clipAt].i, cy = a[clipAt + 1].i, cw = a[clipAt + 2].i, ch = a[clipAt + 3].i;
  const int x2 = std::min<int>(w, cx + std::max<int>(0, cw)), y2 = std::min<int>(h, cy + std::max<int>(0, ch));
  cx = std::max<int>(0, cx), cy = std::max<int>(0, cy);
  out = {d.p, w, h, cx, cy, std::max<int>(0, x2 - cx), std::max<int>(0, y2 - cy)};
  return true;
}

// java.lang.String's characters (value[offset .. offset + count))
Field *fValue = nullptr, *fOffset = nullptr, *fCount = nullptr;
bool chars(Vm& vm, Object* s, const char16_t*& p, int& n) {
  if (!s) return false;
  if (!fValue) {
    Class* c = vm.loadClass("java/lang/String");
    fValue = vm.findField(c, "value", "[C");
    fOffset = vm.findField(c, "offset", "I");
    fCount = vm.findField(c, "count", "I");
    if (!fValue || !fOffset || !fCount) return false;
  }
  Object* v = s->slots()[fValue->slot].a;
  const int off = s->slots()[fOffset->slot].i, cnt = s->slots()[fCount->slot].i;
  if (!v || off < 0 || cnt < 0 || off + cnt > v->length) return false;
  p = reinterpret_cast<const char16_t*>(v->elems<uint16_t>()) + off;
  n = cnt;
  return true;
}

// a substring (off, len) of a String argument, checked
bool sub(Vm& vm, Thread& t, Object* s, int off, int len, const char16_t*& p, int& n) {
  const char16_t* all;
  int total;
  if (!chars(vm, s, all, total)) return vm.throwNew(t, "java/lang/NullPointerException"), false;
  if (off < 0 || len < 0 || off + len > total) return vm.throwNew(t, "java/lang/StringIndexOutOfBoundsException"), false;
  p = all + off, n = len;
  return true;
}

// ---- drawing
void nFill(Vm& vm, Thread& t, Slot* a, Slot&) {
  draw::Target g;
  if (target(vm, t, a, 8, g)) draw::fill(g, a[3].i, a[4].i, a[5].i, a[6].i, (uint32_t)a[7].i);
}
void nLine(Vm& vm, Thread& t, Slot* a, Slot&) {
  draw::Target g;
  if (target(vm, t, a, 9, g)) draw::line(g, a[3].i, a[4].i, a[5].i, a[6].i, (uint32_t)a[7].i, a[8].i != 0);
}
void nArc(Vm& vm, Thread& t, Slot* a, Slot&) {
  draw::Target g;
  if (target(vm, t, a, 11, g))
    draw::arc(g, a[3].i, a[4].i, a[5].i, a[6].i, a[7].i, a[8].i, (uint32_t)a[9].i, a[10].i != 0);
}
void nTriangle(Vm& vm, Thread& t, Slot* a, Slot&) {
  draw::Target g;
  if (target(vm, t, a, 10, g)) draw::triangle(g, a[3].i, a[4].i, a[5].i, a[6].i, a[7].i, a[8].i, (uint32_t)a[9].i);
}
void nRoundRect(Vm& vm, Thread& t, Slot* a, Slot&) {
  draw::Target g;
  if (target(vm, t, a, 11, g))
    draw::roundRect(g, a[3].i, a[4].i, a[5].i, a[6].i, a[7].i, a[8].i, (uint32_t)a[9].i, a[10].i != 0);
}
// blit(d, dw, dh, s, sw, sh, sx, sy, w, h, transform, dx, dy, alpha, cx, cy, cw, ch)
void nBlit(Vm& vm, Thread& t, Slot* a, Slot&) {
  draw::Target g;
  if (!target(vm, t, a, 14, g)) return;
  Px s = ints(a[3].a);
  const int sw = a[4].i, sh = a[5].i;
  if (!s.p) return vm.throwNew(t, "java/lang/NullPointerException");
  if (sw <= 0 || sh <= 0 || (int64_t)sw * sh > s.len) return vm.throwNew(t, "java/lang/IllegalArgumentException", "source");
  const int sx = a[6].i, sy = a[7].i, w = a[8].i, h = a[9].i, tr = a[10].i;
  if (sx < 0 || sy < 0 || w < 0 || h < 0 || sx + w > sw || sy + h > sh || tr < 0 || tr > 7)
    return vm.throwNew(t, "java/lang/IllegalArgumentException", "region");
  draw::blit(g, s.p, sw, sh, sx, sy, w, h, tr, a[11].i, a[12].i, a[13].i != 0);
}
// rgb(d, dw, dh, src, offset, scanlength, x, y, w, h, alpha, cx, cy, cw, ch)
void nRgb(Vm& vm, Thread& t, Slot* a, Slot&) {
  draw::Target g;
  if (!target(vm, t, a, 11, g)) return;
  Px s = ints(a[3].a);
  if (!s.p) return vm.throwNew(t, "java/lang/NullPointerException");
  const int off = a[4].i, scan = a[5].i, w = a[8].i, h = a[9].i;
  if (w <= 0 || h <= 0) return;
  // MIDP: the corners of the area must be inside the array
  const int64_t first = off, last = (int64_t)off + (int64_t)(h - 1) * scan + (w - 1);
  if (first < 0 || last < 0 || first >= s.len || last >= s.len || std::abs(scan) < w)
    return vm.throwNew(t, "java/lang/ArrayIndexOutOfBoundsException");
  draw::rgb(g, s.p, s.len, off, scan, a[6].i, a[7].i, w, h, a[10].i != 0);
}
// text(d, dw, dh, s, off, len, font, x, y, rgb, cx, cy, cw, ch) -> width
void nText(Vm& vm, Thread& t, Slot* a, Slot& ret) {
  ret.i = 0;
  draw::Target g;
  if (!target(vm, t, a, 10, g)) return;
  const char16_t* p;
  int n;
  if (!sub(vm, t, a[3].a, a[4].i, a[5].i, p, n)) return;
  ret.i = draw::text(g, p, n, a[6].i, a[7].i, a[8].i, (uint32_t)a[9].i);
}
void nTextWidth(Vm& vm, Thread& t, Slot* a, Slot& ret) {
  const char16_t* p;
  int n;
  ret.i = 0;
  if (sub(vm, t, a[0].a, a[1].i, a[2].i, p, n)) ret.i = draw::textWidth(p, n, a[3].i);
}
void nFontHeight(Vm&, Thread&, Slot* a, Slot& ret) {
  const draw::Font* f = draw::font(a[0].i);
  ret.i = f ? f->lineH : 14;
}
void nFontBaseline(Vm&, Thread&, Slot* a, Slot& ret) {
  const draw::Font* f = draw::font(a[0].i);
  ret.i = f ? f->baseline : 11;
}

// ---- images
void nDecodeImage(Vm& vm, Thread& t, Slot* a, Slot& ret) {
  ret.a = nullptr;
  Object* b = a[0].a;
  if (!b) return vm.throwNew(t, "java/lang/NullPointerException");
  const int off = a[1].i, len = a[2].i;
  if (off < 0 || len < 0 || off + len > b->length) return vm.throwNew(t, "java/lang/ArrayIndexOutOfBoundsException");
  const uint8_t* p = reinterpret_cast<const uint8_t*>(b->elems<int8_t>()) + off;
  int w = 0, h = 0;
  PVec<uint32_t> px;
  if (!draw::decodePng(p, (size_t)len, w, h, px)) return;  // null: not an image (Java throws)
  Object* r = vm.newArray('I', nullptr, 2 + w * h);
  if (!r) return;  // OutOfMemoryError pending
  int32_t* e = r->elems<int32_t>();
  e[0] = w, e[1] = h;
  std::memcpy(e + 2, px.data(), px.size() * 4);
  ret.a = r;
}

// ---- screen and events
void nScreenW(Vm&, Thread&, Slot*, Slot& ret) { ret.i = H->screenW; }
void nScreenH(Vm&, Thread&, Slot*, Slot& ret) { ret.i = H->screenH; }
void nFlush(Vm& vm, Thread& t, Slot* a, Slot&) {
  Px s = ints(a[0].a);
  if (!s.p || s.len < H->screenW * H->screenH) return vm.throwNew(t, "java/lang/IllegalArgumentException", "screen");
  int x = std::max<int>(0, a[1].i), y = std::max<int>(0, a[2].i);
  const int x2 = std::min<int>(H->screenW, a[1].i + a[3].i), y2 = std::min<int>(H->screenH, a[2].i + a[4].i);
  if (x < x2 && y < y2 && H->flush) H->flush(s.p, x, y, x2 - x, y2 - y);
}
void nPoll(Vm& vm, Thread& t, Slot* a, Slot& ret) {
  Object* ev = a[0].a;
  if (!ev || ev->length < 2) return vm.throwNew(t, "java/lang/IllegalArgumentException");
  int e[2];
  ret.i = H->poll(e);
  if (ret.i) ev->elems<int32_t>()[0] = e[0], ev->elems<int32_t>()[1] = e[1];
}
void nKeyStates(Vm&, Thread&, Slot*, Slot& ret) { ret.i = (int32_t)H->keyStates(); }
void nSoftLabels(Vm& vm, Thread&, Slot* a, Slot&) {
  if (H->softLabels) H->softLabels(vm.str(a[0].a), vm.str(a[1].a));
}
void nScreenHint(Vm&, Thread&, Slot* a, Slot&) {
  if (H->screenHint) H->screenHint(a[0].i, a[1].i);
}
void nVibrate(Vm&, Thread&, Slot* a, Slot&) {
  if (H->vibrate) H->vibrate(std::max<int>(0, std::min<int>(a[0].i, 5000)));
}
void nTone(Vm&, Thread&, Slot* a, Slot&) {
  if (H->tone) H->tone(a[0].i, std::max<int>(0, std::min<int>(a[1].i, 10000)), std::max<int>(0, std::min<int>(a[2].i, 100)));
}
void nAppProperty(Vm& vm, Thread&, Slot* a, Slot& ret) {
  ret.a = nullptr;
  auto it = H->props.find(vm.str(a[0].a));
  if (it != H->props.end()) ret.a = vm.newString(it->second);
}
void nExit(Vm&, Thread&, Slot*, Slot&) {
  if (H->exit) H->exit();
}

// ---- record stores: one file a store in rmsDir, named by the store's name in hex (any characters are safe)
std::string rmsPath(Vm& vm, Object* name) {
  const std::string n = vm.str(name);
  if (n.empty() || n.size() > 32 || H->rmsDir.empty()) return "";
  static const char* HX = "0123456789abcdef";
  std::string p = H->rmsDir;
  for (unsigned char c : n) p += HX[c >> 4], p += HX[c & 15];
  return p + ".rms";
}
void mkdirs(const std::string& dir) {
  for (size_t i = 1; i < dir.size(); i++)
    if (dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
}
// File work goes through the host (H->fileJob): on the board the VM's task has its stack in PSRAM and must not touch
// flash (the cache is off meanwhile), so the UI thread does it while the VM waits. Java arrays are made here.
bool fileJob(const std::function<bool()>& job) { return H->fileJob ? H->fileJob(job) : job(); }

void nRmsLoad(Vm& vm, Thread&, Slot* a, Slot& ret) {
  ret.a = nullptr;
  const std::string p = rmsPath(vm, a[0].a);
  if (p.empty()) return;
  PVec<uint8_t> data;
  const bool ok = fileJob([&]() {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool good = n >= 0 && n <= 512 * 1024;
    if (good) {
      data.resize((size_t)n);
      good = fread(data.data(), 1, (size_t)n, f) == (size_t)n;
    }
    fclose(f);
    return good;
  });
  if (!ok) return;
  Object* b = vm.newArray('B', nullptr, (int32_t)data.size());
  if (b) std::memcpy(b->elems<int8_t>(), data.data(), data.size()), ret.a = b;
}
void nRmsSave(Vm& vm, Thread&, Slot* a, Slot& ret) {
  ret.i = 0;
  const std::string p = rmsPath(vm, a[0].a);
  Object* b = a[1].a;
  if (p.empty() || !b || b->length > 512 * 1024) return;
  const int8_t* bytes = b->elems<int8_t>();  // the VM waits: no collection moves or frees it meanwhile
  const size_t n = (size_t)b->length;
  ret.i = fileJob([&]() {
    mkdirs(H->rmsDir);
    const std::string tmp = p + ".new";  // written whole, then put in place: a power cut keeps the old one
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(bytes, 1, n, f) == n;
    fclose(f);
    if (!ok) {
      remove(tmp.c_str());
      return false;
    }
    remove(p.c_str());
    return rename(tmp.c_str(), p.c_str()) == 0;
  });
}
void nRmsDelete(Vm& vm, Thread&, Slot* a, Slot& ret) {
  const std::string p = rmsPath(vm, a[0].a);
  ret.i = !p.empty() && fileJob([&]() { return remove(p.c_str()) == 0; });
}
void nRmsList(Vm& vm, Thread&, Slot*, Slot& ret) {
  ret.a = nullptr;
  std::vector<std::string> names;
  fileJob([&]() {
    if (DIR* d = H->rmsDir.empty() ? nullptr : opendir(H->rmsDir.c_str())) {
      while (dirent* e = readdir(d)) {
        std::string f = e->d_name;
        if (f.size() < 5 || f.compare(f.size() - 4, 4, ".rms") || (f.size() - 4) % 2) continue;
        std::string nm;
        for (size_t i = 0; i + 1 < f.size() - 4; i += 2) nm += (char)strtol(f.substr(i, 2).c_str(), nullptr, 16);
        names.push_back(nm);
      }
      closedir(d);
    }
    return true;
  });
  if (names.empty()) return;  // MIDP: null when there are none
  Class* sc = vm.loadClass("java/lang/String");
  Object* arr = vm.newArray('L', sc, (int32_t)names.size());
  if (!arr) return;
  Vm::Scope scope(vm);
  for (size_t i = 0; i < names.size(); i++) arr->elems<Object*>()[i] = vm.newString(names[i]);
  ret.a = arr;
}

}  // namespace

void registerMidp(Vm& vm, MidpHost& host) {
  H = &host;
  fValue = fOffset = fCount = nullptr;
  const std::string N = "com/espos/j2me/Native.";
  struct {
    const char* sig;
    Native fn;
  } table[] = {
      {"fill([IIIIIIIIIIII)V", nFill},
      {"line([IIIIIIIIZIIII)V", nLine},
      {"arc([IIIIIIIIIIZIIII)V", nArc},
      {"triangle([IIIIIIIIIIIIII)V", nTriangle},
      {"roundRect([IIIIIIIIIIZIIII)V", nRoundRect},
      {"blit([III[IIIIIIIIIIZIIII)V", nBlit},
      {"rgb([III[IIIIIIIZIIII)V", nRgb},
      {"text([IIILjava/lang/String;IIIIIIIIII)I", nText},
      {"textWidth(Ljava/lang/String;III)I", nTextWidth},
      {"fontHeight(I)I", nFontHeight},
      {"fontBaseline(I)I", nFontBaseline},
      {"decodeImage([BII)[I", nDecodeImage},
      {"screenWidth()I", nScreenW},
      {"screenHeight()I", nScreenH},
      {"flush([IIIII)V", nFlush},
      {"poll([I)I", nPoll},
      {"keyStates()I", nKeyStates},
      {"softLabels(Ljava/lang/String;Ljava/lang/String;)V", nSoftLabels},
      {"vibrate(I)V", nVibrate},
      {"screenHint(II)V", nScreenHint},
      {"tone(III)V", nTone},
      {"appProperty(Ljava/lang/String;)Ljava/lang/String;", nAppProperty},
      {"exit()V", nExit},
      {"rmsLoad(Ljava/lang/String;)[B", nRmsLoad},
      {"rmsSave(Ljava/lang/String;[B)Z", nRmsSave},
      {"rmsDelete(Ljava/lang/String;)Z", nRmsDelete},
      {"rmsList()[Ljava/lang/String;", nRmsList},
  };
  for (auto& e : table) vm.registerNative(N + e.sig, e.fn);
}

}  // namespace j2me
