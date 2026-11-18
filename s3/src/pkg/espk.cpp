// Not speed-critical (big copies are memcpy, hashing is in hardware): built for size, the app slot is full.
#pragma GCC optimize("Os")
#include "espk.h"

#include "../i18n.h"
#include "internal.h"

namespace pkg {

namespace {
uint32_t rd32(const uint8_t* b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
uint16_t rd16(const uint8_t* b) { return b[0] | b[1] << 8; }
constexpr uint32_t MAX_MANIFEST = 32768, MAX_FILES = 512, MAX_FILE = 64u << 20;
}  // namespace

EspkReader::~EspkReader() { heap_caps_free(head_); }

bool EspkReader::safeName(const String& n) {
  if (!n.length() || n.length() > 51 || n[0] == '/' || n.indexOf("..") >= 0 || n.indexOf("//") >= 0 ||
      n.endsWith("/") || n == "manifest.json")
    return false;
  for (unsigned i = 0; i < n.length(); i++) {
    const uint8_t c = n[i];
    if (c < 0x20 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
      return false;
  }
  return true;
}

bool EspkReader::parseHead() {
  const uint32_t mlen = rd32(head_ + 8);
  const char* m = (const char*)head_ + 16;
  String manifest;
  manifest.concat(m, mlen);
  const uint32_t table = 16 + ((mlen + 15) & ~15u);
  uint64_t prevEnd = headEnd_;
  for (size_t i = 0; i < files_.size(); i++) {
    const uint8_t* e = head_ + table + i * 64;
    EspkFile& f = files_[i];
    char nm[53];
    memcpy(nm, e, 52);
    nm[52] = 0;
    f.name = nm;
    f.off = rd32(e + 52), f.size = rd32(e + 56), f.crc = rd32(e + 60);
    if (!safeName(f.name)) return fail(T_("a file name in the package is not valid"));
    for (size_t k = 0; k < i; k++)
      if (files_[k].name == f.name) return fail(T_("the package has two files with the same name"));
    if (f.off & 15 || f.off < prevEnd || f.size > MAX_FILE) return fail(T_("the package's file table is broken"));
    prevEnd = (uint64_t)f.off + f.size;
  }
  laidOut_ = true;
  pos_ = headEnd_;
  cur_ = 0;
  crc_ = 0;
  String why;
  if (!sink_->layout(manifest, files_, why)) return fail(why.length() ? why.c_str() : T_("the package cannot be used"));
  return true;
}

bool EspkReader::feed(const uint8_t* p, size_t n) {
  if (err_.length()) return false;
  // 1. Header, manifest and table, kept whole.
  while (!laidOut_ && n) {
    if (headLen_ + n > headCap_ && headCap_ < headEnd_) {
      const uint32_t cap = max<uint32_t>(headEnd_, 256);
      uint8_t* nb = (uint8_t*)heap_caps_realloc(head_, cap, MALLOC_CAP_SPIRAM);
      if (!nb) return fail(T_("not enough memory"));
      head_ = nb, headCap_ = cap;
    }
    const size_t k = min<size_t>(n, headEnd_ - headLen_);
    memcpy(head_ + headLen_, p, k);
    headLen_ += k, p += k, n -= k;
    if (headLen_ < headEnd_) continue;
    if (headEnd_ == 16) {
      if (memcmp(head_, "ESPK", 4)) return fail(T_("not an .espk package"));  // magic "ESPK": the format's, kept
      if (rd16(head_ + 4) != 1) return fail(T_("a newer package format: update the software"));
      const uint32_t mlen = rd32(head_ + 8), nf = rd32(head_ + 12);
      if (mlen > MAX_MANIFEST || nf > MAX_FILES) return fail(T_("the package header is broken"));
      files_.resize(nf);
      headEnd_ = 16 + ((mlen + 15) & ~15u) + nf * 64;
      if (headEnd_ > 16) continue;
    }
    if (!parseHead()) return false;
  }
  // 2. The files' bytes, in table order; padding between them is skipped.
  while (n) {
    if (cur_ >= files_.size()) {  // trailing bytes: ignored
      pos_ += n;
      return true;
    }
    const EspkFile& f = files_[cur_];
    if (pos_ < f.off) {
      const size_t k = (size_t)min<uint64_t>(n, f.off - pos_);
      pos_ += k, p += k, n -= k;
      continue;
    }
    const uint32_t at = (uint32_t)(pos_ - f.off);
    const size_t k = min<size_t>(n, f.size - at);
    if (k) {
      crc_ = in::crc32(crc_, p, k);
      String why;
      if (!sink_->data((int)cur_, at, p, (uint32_t)k, why)) return fail(why.length() ? why.c_str() : T_("cannot write"));
      pos_ += k, p += k, n -= k;
    }
    if (pos_ == (uint64_t)f.off + f.size) {
      if (crc_ != f.crc) return fail(T_("a downloaded file is wrong (CRC)"));
      String why;
      if (!sink_->done((int)cur_, why)) return fail(why.length() ? why.c_str() : T_("cannot write"));
      cur_++;
      crc_ = 0;
    }
  }
  // Empty files right where we stopped.
  while (laidOut_ && cur_ < files_.size() && files_[cur_].size == 0 && pos_ >= files_[cur_].off) {
    if (files_[cur_].crc != 0) return fail(T_("a downloaded file is wrong (CRC)"));
    String why;
    if (!sink_->done((int)cur_, why)) return fail(why.c_str());
    cur_++;
  }
  return true;
}

bool EspkReader::finish() {
  if (err_.length()) return false;
  if (!laidOut_) return fail(T_("the package is cut short"));
  // Empty files at the very end.
  while (cur_ < files_.size() && files_[cur_].size == 0) {
    if (files_[cur_].crc != 0) return fail(T_("a downloaded file is wrong (CRC)"));
    String why;
    if (!sink_->done((int)cur_, why)) return fail(why.c_str());
    cur_++;
  }
  if (cur_ < files_.size()) return fail(T_("the package is cut short"));
  return true;
}

}  // namespace pkg
