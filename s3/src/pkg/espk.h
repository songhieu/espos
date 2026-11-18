#pragma once
// Streaming reader of one package file (.espk; .espk before 0.5.0, same bytes; docs/ecosystem.md §2,
// tools/espk.py): feed the bytes in order as they arrive; it hands out the manifest and the file table, then
// each file's bytes, and checks each file's CRC32.
#include <Arduino.h>

#include <vector>

namespace pkg {

struct EspkFile {
  String name;
  uint32_t off = 0, size = 0, crc = 0;
};

class EspkReader {
 public:
  struct Sink {
    virtual ~Sink() {}
    // After the header, manifest and table. false stops (set `why`).
    virtual bool layout(const String& manifest, const std::vector<EspkFile>& files, String& why) = 0;
    // Bytes of file `i` at offset `at` (in order).
    virtual bool data(int i, uint32_t at, const uint8_t* p, uint32_t n, String& why) = 0;
    // File `i` is complete and its CRC32 matched.
    virtual bool done(int i, String& why) = 0;
  };
  explicit EspkReader(Sink* s) : sink_(s) {}
  ~EspkReader();
  bool feed(const uint8_t* p, size_t n);  // false: stop, err() says why
  bool finish();                          // all files complete
  const String& err() const { return err_; }
  static bool safeName(const String& n);  // relative, no "..", printable

 private:
  bool parseHead();
  bool fail(const char* why) { return err_ = why, false; }
  Sink* sink_;
  uint8_t* head_ = nullptr;
  uint32_t headCap_ = 0, headLen_ = 0, headEnd_ = 16;
  bool laidOut_ = false;
  std::vector<EspkFile> files_;
  uint64_t pos_ = 0;
  size_t cur_ = 0;
  uint32_t crc_ = 0;
  String err_;
};

}  // namespace pkg
