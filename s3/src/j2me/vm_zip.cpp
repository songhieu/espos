// JARs (the zip central directory; entries are inflated by vm_inflate.cpp) and folders: where classes and resources
// come from.
#if defined(ESP_PLATFORM) && !defined(J2ME_NO_OS)
#pragma GCC optimize("Os")  // cold code: small on the board (flash is short); the interpreter stays -O2
#endif
#include <cstdio>

#include "vm_internal.h"

namespace j2me {

void Vm::addJar(const uint8_t* zip, size_t len, bool copy) {
  Source s;
  s.len = len;
  if (copy) {
    uint8_t* b = static_cast<uint8_t*>(vmMalloc(len ? len : 1));
    if (!b) {
      log("j2me: no memory for a JAR of " + std::to_string(len) + " bytes");
      return;
    }
    std::memcpy(b, zip, len);
    s.zip = b;
    s.owned = true;
  } else {
    s.zip = zip;
  }
  const uint8_t* z = s.zip;
  // the end of central directory record: the last 22 bytes, or before a comment of up to 64 KB
  long eocd = -1;
  for (long i = (long)len - 22; i >= 0 && i >= (long)len - 22 - 65535; i--)
    if (le4(z + i) == 0x06054b50u) {
      eocd = i;
      break;
    }
  if (eocd < 0) {
    log("j2me: not a JAR (no central directory)");
    if (s.owned) vmFree(const_cast<uint8_t*>(s.zip));
    return;
  }
  uint32_t n = le2(z + eocd + 10), cdOff = le4(z + eocd + 16);
  size_t p = cdOff;
  for (uint32_t k = 0; k < n; k++) {
    if (p + 46 > len || le4(z + p) != 0x02014b50u) {
      log("j2me: JAR central directory is damaged");
      break;
    }
    Entry e;
    e.method = le2(z + p + 10);
    e.csize = le4(z + p + 20);
    e.usize = le4(z + p + 24);
    uint16_t nl = le2(z + p + 28), xl = le2(z + p + 30), cl = le2(z + p + 32);
    e.offset = le4(z + p + 42);
    if (p + 46 + nl > len) break;
    PStr name(reinterpret_cast<const char*>(z + p + 46), nl);
    if (nl && name[nl - 1] != '/') s.entries[name] = e;
    p += 46 + nl + xl + cl;
  }
  sources.push_back(std::move(s));
}

void Vm::addDir(const std::string& path) {
  Source s;
  s.dir = path;
  while (s.dir.size() > 1 && s.dir.back() == '/') s.dir.pop_back();
  sources.push_back(std::move(s));
}

bool Vm::readEntry(Source& s, const Entry& e, uint8_t* (*sink)(void*, size_t), void* ctx) {
  const uint8_t* z = s.zip;
  if ((size_t)e.offset + 30 > s.len || le4(z + e.offset) != 0x04034b50u) return false;
  size_t data = (size_t)e.offset + 30 + le2(z + e.offset + 26) + le2(z + e.offset + 28);
  if (data + e.csize > s.len) return false;
  if (e.method != 0 && e.method != 8) {
    log("j2me: JAR entry uses compression method " + std::to_string(e.method));
    return false;
  }
  uint8_t* out = sink(ctx, e.usize);
  if (!out) return false;
  if (e.method == 0) {
    if (e.csize != e.usize) return false;
    if (e.usize) std::memcpy(out, z + data, e.usize);
    return true;
  }
  return inflateRaw(z + data, e.csize, out, e.usize);
}

bool Vm::readResource(const std::string& name0, uint8_t* (*sink)(void*, size_t), void* ctx) {
  std::string name = name0;
  while (!name.empty() && name[0] == '/') name.erase(0, 1);
  PStr key(name.c_str(), name.size());
  for (Source& s : sources) {
    if (!s.dir.empty()) {
      std::string path = s.dir + "/" + name;
      FILE* f = std::fopen(path.c_str(), "rb");
      if (!f) continue;
      bool ok = false;
      if (std::fseek(f, 0, SEEK_END) == 0) {
        long n = std::ftell(f);
        if (n >= 0 && std::fseek(f, 0, SEEK_SET) == 0) {
          uint8_t* out = sink(ctx, (size_t)n);
          ok = out && std::fread(out, 1, (size_t)n, f) == (size_t)n;
        }
      }
      std::fclose(f);
      if (ok) return true;
      continue;
    }
    auto it = s.entries.find(key);
    if (it != s.entries.end()) return readEntry(s, it->second, sink, ctx);
  }
  return false;
}

bool Vm::resource(const std::string& name, std::vector<uint8_t>& out) {
  return readResource(
      name,
      [](void* c, size_t n) -> uint8_t* {
        auto* v = static_cast<std::vector<uint8_t>*>(c);
        v->resize(n);
        return n ? v->data() : reinterpret_cast<uint8_t*>(v);
      },
      &out);
}

bool Vm::resource(const std::string& name, PVec<uint8_t>& out) {
  return readResource(
      name,
      [](void* c, size_t n) -> uint8_t* {
        auto* v = static_cast<PVec<uint8_t>*>(c);
        v->resize(n);
        return n ? v->data() : reinterpret_cast<uint8_t*>(v);
      },
      &out);
}

}  // namespace j2me
