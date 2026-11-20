// EspOS Bot simulator: src/bot/transport.h over libcurl (link.cpp's task runs as on the board, on a thread). The
// server is tools/bot/fixture_server.py by default (http://127.0.0.1:8098); --api https://api.espos.dev only with an OK
// (live AI and TTS calls).
#include <curl/curl.h>

#include <cstdlib>
#include <cstring>

#include <mutex>
#include <string>

#include "../../src/bot/config.h"
#include "../../src/bot/link.h"
#include "../../src/bot/spk.h"
#include "../../src/bot/store.h"
#include "../../src/bot/transport.h"

namespace bot {
namespace transport {
namespace {

CURL* kept = nullptr;  // one connection kept, as the board's TLS client
std::mutex mu;

struct Ctx {
  std::string body, ctype, assist;
  bool voice = false, headDone = false;
  void (*onHead)(const std::string&) = nullptr;
  volatile bool* cancel = nullptr;
  uint8_t odd = 0;
  bool hasOdd = false;
};

std::string unb64(const std::string& s) {
  static const std::string A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  unsigned acc = 0;
  int bits = 0;
  for (char c : s) {
    const size_t v = A.find(c);
    if (v == std::string::npos) continue;
    acc = acc << 6 | (unsigned)v, bits += 6;
    if (bits >= 8) bits -= 8, o += (char)(acc >> bits);
  }
  return o;
}

size_t onHeader(char* p, size_t sz, size_t n, void* u) {
  Ctx* c = (Ctx*)u;
  std::string l(p, sz * n);
  const size_t colon = l.find(':');
  if (colon != std::string::npos) {
    std::string k = l.substr(0, colon), v = l.substr(colon + 1);
    for (char& ch : k) ch = (char)tolower((unsigned char)ch);
    while (!v.empty() && (v[0] == ' ')) v.erase(0, 1);
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
    if (k == "content-type") c->ctype = v;
    if (k == "x-assist") c->assist = v;
  }
  return sz * n;
}

size_t onBody(char* p, size_t sz, size_t n, void* u) {
  Ctx* c = (Ctx*)u;
  const size_t len = sz * n;
  if (c->onHead && c->ctype.rfind("audio/pcm", 0) == 0) {  // the voice: words first, then the sound to the speaker
    c->voice = true;
    if (!c->headDone) c->headDone = true, c->onHead(unb64(c->assist));
    if (c->cancel && *c->cancel) return 0;  // stops the transfer
    return spk::pushPcm((const uint8_t*)p, (uint32_t)len) ? len : 0;
  }
  c->body.append(p, len);
  return len;
}

// Every request goes to the local fixture server (BOT_SIM_API, default http://127.0.0.1:8098), whatever api a
// provisioning line gave, unless BOT_SIM_LIVE=1 (the real server: live AI and TTS calls, only with an OK).
std::string rebase(const std::string& url) {
  const char* live = getenv("BOT_SIM_LIVE");
  if (live && !strcmp(live, "1")) return url;
  const char* api = getenv("BOT_SIM_API");
  const size_t p = url.find("://");
  const size_t path = p == std::string::npos ? 0 : url.find('/', p + 3);
  return std::string(api ? api : "http://127.0.0.1:8098") + (path == std::string::npos ? "" : url.substr(path));
}

int run(const char* method, const std::string& urlIn, const uint8_t* body, uint32_t len, const char* ctype, Ctx& ctx,
        uint32_t timeoutMs, bool hw, const char* token = nullptr) {
  const std::string url = rebase(urlIn);
  std::lock_guard<std::mutex> l(mu);
  if (!kept) kept = curl_easy_init();
  CURL* c = kept;
  curl_easy_reset(c);
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
  curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, onHeader);
  curl_easy_setopt(c, CURLOPT_HEADERDATA, &ctx);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onBody);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
  struct curl_slist* h = nullptr;
  const std::string tok = token ? token : store::token();
  if (!tok.empty()) h = curl_slist_append(h, ("Authorization: Device " + tok).c_str());
  h = curl_slist_append(h, "User-Agent: EspOS-Bot/" FW_VERSION " (simulator)");
  if (hw) {
    h = curl_slist_append(h, ("X-Espos-Hwid: " + link::hwid()).c_str());
    h = curl_slist_append(h, ("X-Espos-Mac4: " + link::mac4()).c_str());
  }
  if (ctype) h = curl_slist_append(h, (std::string("Content-Type: ") + ctype).c_str());
  h = curl_slist_append(h, "Expect:");
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
  if (!strcmp(method, "POST")) {
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body ? (const char*)body : "");
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)len);
  }
  const CURLcode r = curl_easy_perform(c);
  curl_slist_free_all(h);
  long code = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  if (r == CURLE_OK || (ctx.voice && code == 200)) return (int)code;
  if (r == CURLE_COULDNT_CONNECT || r == CURLE_COULDNT_RESOLVE_HOST) return -1;
  if (r == CURLE_SEND_ERROR) return -2;
  return -3;
}

}  // namespace

void begin() { curl_global_init(CURL_GLOBAL_DEFAULT); }

int request(const char* method, const std::string& url, const uint8_t* body, uint32_t len, const char* ctype,
            std::string& out, uint32_t timeoutMs, bool hw, const char* token) {
  Ctx ctx;
  const int s = run(method, url, body, len, ctype, ctx, timeoutMs, hw, token);
  out = ctx.body;
  return s;
}

int ask(const std::string& url, const uint8_t* wav, uint32_t len, bool& voice, std::string& body,
        void (*onHead)(const std::string& json), volatile bool* cancel) {
  Ctx ctx;
  ctx.onHead = onHead, ctx.cancel = cancel;
  const int s = run("POST", url, wav, len, "audio/wav", ctx, 45000, true);
  voice = ctx.voice;
  if (voice) spk::endStream();
  body = ctx.body;
  return s;
}

bool warm(const std::string&) { return true; }  // curl keeps its own connection
void drop() {}
void dropIfIdle(uint32_t) {}
int ota(const std::string&, uint32_t, const std::string&, volatile int* pct) {
  *pct = -1;
  return -9;  // the simulator never installs firmware
}

}  // namespace transport
}  // namespace bot
