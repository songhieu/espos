#include "transport.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

#include "config.h"
#include "link.h"
#include "spk.h"
#include "store.h"

extern const uint8_t x509_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace bot {
namespace transport {
namespace {

NetworkClientSecure* tls = nullptr;
uint32_t lastUse = 0;  // when the kept connection last carried a request

std::string hostOf(const std::string& url) {
  size_t a = url.find("://");
  a = a == std::string::npos ? 0 : a + 3;
  const size_t b = url.find('/', a);
  return url.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

int errOf(int code) {
  return code == HTTPC_ERROR_CONNECTION_REFUSED ? -1 : code == HTTPC_ERROR_SEND_PAYLOAD_FAILED ? -2 : -3;
}

void headers(HTTPClient& h, bool hw, const char* token = nullptr) {
  const std::string t = token ? token : store::token();
  if (!t.empty()) h.addHeader("Authorization", ("Device " + t).c_str());
  h.addHeader("User-Agent", "EspOS-Bot/" FW_VERSION);
  if (hw) h.addHeader("X-Espos-Hwid", link::hwid().c_str()), h.addHeader("X-Espos-Mac4", link::mac4().c_str());
}

// The voice's bytes into the speaker's ring; a cancelled turn stops taking them (the stream then ends).
struct SpkStream : public Stream {
  volatile bool* cancel;
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t n) override { return !*cancel && spk::pushPcm(b, n) ? n : 0; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

std::string unb64(const String& s) {
  size_t n = 0;
  std::string out(s.length() * 3 / 4 + 4, '\0');
  if (mbedtls_base64_decode((unsigned char*)&out[0], out.size(), &n, (const unsigned char*)s.c_str(), s.length())) return "";
  out.resize(n);
  return out;
}

}  // namespace

void begin() {
  tls = new NetworkClientSecure();
  tls->setCACertBundle(x509_bundle_start, x509_bundle_end - x509_bundle_start);
  tls->setHandshakeTimeout(15);
}

int request(const char* method, const std::string& url, const uint8_t* body, uint32_t len, const char* ctype,
            std::string& out, uint32_t timeoutMs, bool hw, const char* token) {
  HTTPClient h;
  h.setReuse(true);
  h.setTimeout(timeoutMs);
  h.setConnectTimeout(10000);
  if (!h.begin(*tls, url.c_str())) return -1;
  headers(h, hw, token);
  if (ctype) h.addHeader("Content-Type", ctype);
  const int code = !strcmp(method, "POST") ? h.POST((uint8_t*)body, len) : h.GET();
  if (code > 0) {
    out = h.getString().c_str();
    lastUse = millis();
  } else {
    tls->stop();  // a broken connection is never kept
  }
  h.end();
  return code > 0 ? code : errOf(code);
}

int ask(const std::string& url, const uint8_t* wav, uint32_t len, bool& voice, std::string& body,
        void (*onHead)(const std::string& json), volatile bool* cancel) {
  voice = false;
  HTTPClient h;
  h.setReuse(true);
  h.setTimeout(45000);
  h.setConnectTimeout(10000);
  if (!h.begin(*tls, url.c_str())) return -1;
  headers(h, true);  // the hardware headers: the AI trial is counted per board
  h.addHeader("Content-Type", "audio/wav");
  const char* keys[] = {"Content-Type", "X-Assist"};
  h.collectHeaders(keys, 2);
  const int code = h.POST((uint8_t*)wav, len);
  if (code <= 0) {
    tls->stop();
    h.end();
    return errOf(code);
  }
  lastUse = millis();
  if (code == 200 && h.header("Content-Type").startsWith("audio/pcm")) {
    voice = true;
    onHead(unb64(h.header("X-Assist")));
    SpkStream out;
    out.cancel = cancel;
    const int n = h.writeToStream(&out);
    spk::endStream();
    if (n < 0 || *cancel) tls->stop();  // cut short: the rest of it is never read on this connection
    h.end();
    return 200;
  }
  body = h.getString().c_str();
  h.end();
  return code;
}

bool warm(const std::string& url) {
  tls->stop();
  const bool ok = tls->connect(hostOf(url).c_str(), 443) == 1;
  if (ok) lastUse = millis();
  return ok;
}

void drop() { tls->stop(); }
void dropIfIdle(uint32_t ms) {
  if (tls->connected() && millis() - lastUse > ms) tls->stop();
}

int ota(const std::string& url, uint32_t size, const std::string& sha, volatile int* pct) {
  tls->stop();
  *pct = 0;
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  HTTPClient h;
  NetworkClientSecure c;
  c.setCACertBundle(x509_bundle_start, x509_bundle_end - x509_bundle_start);
  h.setTimeout(20000);
  if (!part || !h.begin(c, url.c_str())) return -1;
  const int code = h.GET();
  if (code != 200) return h.end(), code > 0 ? code : -3;
  const int total = h.getSize() > 0 ? h.getSize() : (int)size;
  esp_ota_handle_t oh = 0;
  if (total <= 0 || (uint32_t)total > part->size || esp_ota_begin(part, total, &oh) != ESP_OK) return h.end(), -4;
  mbedtls_sha256_context sc;
  mbedtls_sha256_init(&sc);
  mbedtls_sha256_starts(&sc, 0);
  NetworkClient* s = h.getStreamPtr();
  static uint8_t buf[4096];
  int got = 0;
  uint32_t quiet = millis();
  bool ok = true;
  while (got < total && ok) {
    const int a = s->available();
    if (a <= 0) {
      if (!s->connected() || millis() - quiet > 20000) ok = false;
      delay(5);
      continue;
    }
    const int n = s->read(buf, a < (int)sizeof buf ? a : (int)sizeof buf);
    if (n <= 0) continue;
    quiet = millis();
    mbedtls_sha256_update(&sc, buf, n);
    ok = esp_ota_write(oh, buf, n) == ESP_OK;
    got += n;
    *pct = (int)((int64_t)got * 100 / total);
  }
  h.end();
  uint8_t d[32];
  mbedtls_sha256_finish(&sc, d);
  mbedtls_sha256_free(&sc);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", d[i]);
  if (!ok || got != total) return esp_ota_abort(oh), -5;
  if (!sha.empty() && sha != hex) return esp_ota_abort(oh), -6;
  if (esp_ota_end(oh) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) return -7;
  return 200;
}

}  // namespace transport
}  // namespace bot
