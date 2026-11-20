// Not speed-critical (big copies are memcpy, hashing is in hardware): built for size, the app slot is full.
#pragma GCC optimize("Os")
// Downloads of package parts for the installer, delivered in order.
// Board: one TLS stream is capped by the TCP window (~21 KB/s measured), so three connections fetch 512 KB
// chunks side by side into PSRAM slots; the installer takes the slots in order (details at NetSource). Simulator: the fake network's file for the URL, at SIM_PKG_KBPS (default 1024).
#include "../i18n.h"
#include "internal.h"
#include "trace.h"

#include <atomic>

#ifdef SIM
#include <unistd.h>

#include <string>
extern std::string simDir;
#else
#include <HTTPClient.h>

#include "../desk.h"
#include <NetworkClientSecure.h>

extern const uint8_t x509_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_bundle_end[] asm("_binary_x509_crt_bundle_end");
#endif

namespace pkg {
namespace in {

#ifdef SIM
namespace {

std::atomic<bool> idle{true};
uint32_t t0 = 0;
int rate() {
  const char* r = getenv("SIM_PKG_KBPS");
  return r ? atoi(r) : 1024;
}

class SimSource : public Source {
 public:
  SimSource(const String& path, uint32_t) {
    f_ = fopen(path.c_str(), "rb");
    if (f_) {
      fseek(f_, 0, SEEK_END);
      size_ = ftell(f_);
      fseek(f_, 0, SEEK_SET);
    }
    paceStart();
  }
  ~SimSource() override {
    if (f_) fclose(f_);
  }
  bool ok() const { return f_ != nullptr; }
  bool next(const uint8_t*& p, size_t& n) override {
    if (pos_ >= size_) return n = 0, true;
    const size_t want = min<uint64_t>(sizeof buf_, size_ - pos_);
    pace(pos_ + want);
    if (abortAll || abortJob) return err = T_("stopped"), false;
    n = fread(buf_, 1, want, f_);
    if (!n) return err = T_("cannot read"), false;
    p = buf_;
    pos_ += n;
    return true;
  }
  void release() override {}
  uint64_t got() override { return pos_; }

 private:
  FILE* f_ = nullptr;
  uint64_t size_ = 0, pos_ = 0;
  uint8_t buf_[16384];
};

}  // namespace

void paceStart() { t0 = millis(); }
void pace(uint64_t done) {
  const int r = rate();
  while (!abortAll && !abortJob && r > 0 && (uint64_t)(millis() - t0) * r * 1024 / 1000 < done) {
    idle = true;
    usleep(200);
  }
  idle = false;
}
void paceIdle(bool i) { idle = i; }
void paceWait() {
  for (int n = 0; !idle && !abortAll && n < 30000; n++) usleep(100);
}

Source* openSource(const String& url, uint32_t size) {
  String path = url;
  if (path.startsWith("https://")) path = path.substring(path.indexOf('/', 8));
  path.replace("/", "_");
  SimSource* s = new SimSource(String(simDir.c_str()) + "/net/" + path, size);
  if (!s->ok()) {
    trace::add("sim download %s -> 404 (no %s/net/%s)", url.c_str(), simDir.c_str(), path.c_str());
    delete s;
    return nullptr;
  }
  return s;
}

#else
namespace {

constexpr uint32_t BLOCK = 65536;
// Board: one TLS stream tops out at window / round trip (CONFIG_LWIP_TCP_WND_DEFAULT 5,760 B): ~21 KB/s to the
// storage server measured (PawBoop 7.3 MB in 350 s, 313 s of it waiting for the network). So, like ota.cpp, three
// connections fetch 512 KB chunks side by side (TLS buffers live in PSRAM, desk.cpp). The server closes the
// connection after each ranged answer, so each chunk is one request on a fresh connection (a request per 64 KB
// paid a handshake per block; reusing the closed connection gave "http -5"). The installer takes the 64 KB slots
// in order; a cut chunk resumes with a Range from where it stopped.
// A third connection left the hardware AES without internal RAM on the board ("esp-aes: Failed to allocate
// memory"), its task spun and the task watchdog reset the board: at most two, the second only with room to spare.
constexpr uint32_t CHUNK = 8;                    // blocks per request
constexpr int WORKERS = 2;
constexpr uint32_t ROOM_FOR_TLS = 44 * 1024;     // internal RAM free to open one more connection
constexpr int MAXSLOTS = (WORKERS + 1) * CHUNK;  // 2 MB of PSRAM while installing

class NetSource : public Source {
 public:
  NetSource(const String& url, uint32_t size) : url_(url), size_(size) {
    blocks_ = (size + BLOCK - 1) / BLOCK;
    const int want = min<int>(MAXSLOTS, blocks_);
    for (int i = 0; i < want; i++) {
      uint8_t* b = (uint8_t*)heap_caps_malloc(BLOCK, MALLOC_CAP_SPIRAM);
      if (!b) break;
      slots_[i].buf = b;
      slots_[i].state = 0;
      nslots_ = i + 1;
    }
  }
  ~NetSource() override {
    stop_ = true;
    while (alive_ > 0) delay(20);
    for (int i = 0; i < nslots_; i++) heap_caps_free(slots_[i].buf);
  }
  bool start() {
    if (!nslots_) return err = T_("not enough memory"), false;
    // Fewer slots than three chunks and one spare (little PSRAM left): fewer connections.
    int n = max(1, min<int>(min<int>(WORKERS, (nslots_ - (int)CHUNK) / (int)CHUNK), (blocks_ + CHUNK - 1) / CHUNK));
    t0_ = millis();
    // Decided before the first handshake (a worker's chunk size depends on it): a second connection only when
    // there is internal RAM for two TLS sessions and some to spare (~37 KB each on the board).
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 2 * ROOM_FOR_TLS) n = 1;
    workers_ = n;
    for (int i = 0; i < n; i++) {
      alive_++;
      // Idle priority: a TLS handshake (RSA/ECC in the MPI unit) runs for seconds without blocking; at priority 2
      // two of them back to back kept core 0's idle task out and the task watchdog reset the board. At priority 0
      // they share the core with it (and anything real preempts them).
      if (xTaskCreatePinnedToCoreWithCaps(worker, "pkgdl", 10240, this, tskIDLE_PRIORITY, nullptr, 0,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) !=
          pdPASS) {
        alive_--;
        break;
      }
      if (i + 1 < n) {
        delay(2500);  // one TLS handshake at a time; the first chunk starts at once
        const uint32_t room = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (room < ROOM_FOR_TLS || heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) < 16 * 1024) {
          trace::add("pkg download: one connection only (internal RAM %u KB)", (unsigned)(room / 1024));
          break;
        }
      }
    }
    return alive_ > 0 || (err = T_("cannot open a connection"), false);
  }
  bool next(const uint8_t*& p, size_t& n) override {
    if (consumed_ >= blocks_) return n = 0, true;
    Slot& s = slots_[consumed_ % nslots_];
    const uint32_t t0 = millis();
    while (!(s.state == 2 && s.block == consumed_)) {
      if (failed_) return err = T_("the network dropped midway"), false;
      if (abortJob) return err = T_("stopped"), false;
      if (alive_ <= 0 && !(s.state == 2 && s.block == consumed_)) return err = T_("the network dropped midway"), false;
      if (millis() - t0 > 120000) return err = T_("the network is too slow"), false;
      delay(10);
    }
    p = s.buf;
    n = s.len;
    return true;
  }
  void release() override {
    slots_[consumed_ % nslots_].state = 0;
    consumed_++;
    if (consumed_ == blocks_)
      trace::add("pkg download %u KB in %lu ms (%d slots, %u connections)", (unsigned)(got_ / 1024),
                 (unsigned long)(millis() - t0_), nslots_, (unsigned)handshakes_);
  }
  uint64_t got() override { return got_; }

 private:
  struct Slot {
    uint8_t* buf = nullptr;
    volatile uint32_t block = UINT32_MAX, len = 0;
    volatile int state = 0;  // 0 free, 2 ready
  };

  // Blocks [b0, b1) of the file on one connection, resuming after a cut.
  bool chunk(NetworkClientSecure& tls, HTTPClient& h, uint32_t b0, uint32_t b1) {
    const uint32_t end = min<uint32_t>(size_, b1 * BLOCK);
    uint32_t pos = b0 * BLOCK;
    for (int attempt = 0; pos < end && !stop_ && !failed_ && !abortJob; attempt++) {
      if (attempt >= 8) return false;
      if (attempt) delay(600 * attempt);
      tls.stop();
      if (!h.begin(tls, url_)) continue;
      h.addHeader("Range", "bytes=" + String(pos) + "-" + String(end - 1));
      handshakes_++;
      const int code = h.GET();
      if (!(code == 206 || (code == 200 && pos == 0 && end == size_))) {
        trace::add("pkg download http %d at %u", code, (unsigned)pos);
        h.end();
        continue;
      }
      NetworkClient* st = h.getStreamPtr();
      uint32_t last = millis(), reads = 0;
      int bad = 0;
      while (pos < end && !stop_ && !abortJob) {
        if (pos / BLOCK >= consumed_ + (uint32_t)nslots_) {  // the installer is behind: no free slot yet
          delay(5);
          continue;
        }
        // Let core 0's idle task run now and then: decrypting at full speed starved it and the task watchdog
        // reset the board.
        if ((++reads & 7) == 0) delay(1);
        const uint32_t b = pos / BLOCK, from = b * BLOCK, len = min<uint32_t>(BLOCK, size_ - from);
        Slot& s = slots_[b % nslots_];
        const int avail = st->available();
        if (avail <= 0) {
          if (!h.connected() || millis() - last > 15000) break;
          delay(2);
          continue;
        }
        const int k = st->read(s.buf + (pos - from), min<uint32_t>(avail, from + len - pos));
        if (k > 0) {
          bad = 0;
          pos += k, got_ += k, last = millis();
          if (pos == from + len) {
            s.block = b;
            s.len = len;
            s.state = 2;
          }
        } else {
          if (++bad > 500) break;  // "available" but nothing readable: a broken connection
          delay(2);
        }
      }
      h.end();
      if (pos < end && !stop_ && !abortJob) trace::add("pkg download cut at %u, resuming", (unsigned)pos);
    }
    return pos >= end;
  }

  static void worker(void* arg) {
    NetSource* me = (NetSource*)arg;
    // The desk keeps its own HTTPS connection for minutes: close it first, its TLS state needs the same RAM.
    desk::releaseConnection();
    for (int i = 0; i < 40 && desk::connectionOpen(); i++) delay(50);
    {
      NetworkClientSecure tls;
      tls.setCACertBundle(x509_bundle_start, x509_bundle_end - x509_bundle_start);
      tls.setHandshakeTimeout(20);
      HTTPClient h;
      h.setReuse(false);
      h.setConnectTimeout(10000);
      h.setTimeout(20000);
      while (!me->stop_ && !me->failed_ && !abortJob) {
        // One connection: the whole rest of the file on one stream (no handshake per chunk). Two: 512 KB chunks.
        const uint32_t step = me->workers_ > 1 ? CHUNK : me->blocks_;
        const uint32_t b0 = me->nextChunk_.fetch_add(1) * step;
        if (b0 >= me->blocks_) break;
        const uint32_t b1 = min<uint32_t>(me->blocks_, b0 + step);
        // The chunk's first slot must be free before its request starts (later ones are waited for while reading).
        while (b0 >= me->consumed_ + (uint32_t)me->nslots_ && !me->stop_ && !me->failed_) delay(10);
        if (!me->chunk(tls, h, b0, b1)) {
          me->failed_ = true;
          break;
        }
      }
      h.end();
      tls.stop();
    }
    me->alive_--;
    vTaskDelete(nullptr);
  }

  String url_;
  uint32_t size_, blocks_;
  Slot slots_[MAXSLOTS];
  int nslots_ = 0;
  std::atomic<int> workers_{1};
  uint32_t t0_ = 0;
  std::atomic<uint32_t> consumed_{0}, nextChunk_{0}, handshakes_{0};
  std::atomic<uint32_t> got_{0};
  std::atomic<bool> stop_{false}, failed_{false};
  std::atomic<int> alive_{0};
};

}  // namespace

void paceStart() {}
void pace(uint64_t) {}
void paceIdle(bool) {}
void paceWait() {}

Source* openSource(const String& url, uint32_t size) {
  if (!size || !url.startsWith("https://")) return nullptr;
  NetSource* s = new NetSource(url, size);
  if (!s->start()) {
    trace::add("pkg download: %s", s->err.c_str());
    delete s;
    return nullptr;
  }
  return s;
}
#endif

}  // namespace in
}  // namespace pkg
