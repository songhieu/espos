#include "InkDeskClient.h"

#include <HalMemory.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstring>

#include "InkDeskStore.h"
#include "network/HttpDownloader.h"

namespace inkdesk::client {
namespace {

constexpr uint32_t HTTP_TIMEOUT_MS = 20000;
int g_lastStatus = 0;

bool enoughHeapForTls() {
  const auto heap = HalMemory::getDefaultHeap();
  if (heap.freeBytes < HttpDownloader::MIN_TLS_FREE_HEAP || heap.largestBlockBytes < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR("INKD", "Low heap for TLS: %zu free, %zu max block", heap.freeBytes, heap.largestBlockBytes);
    return false;
  }
  return true;
}

Result classify(const int status, const std::string& body) {
  if (status <= 0) return Result::Network;
  if (status >= 200 && status < 300) return Result::Ok;
  if (status == 401 || status == 403) {
    return body.find("not_linked") != std::string::npos ? Result::NotLinked : Result::Unauthorized;
  }
  if (status == 409) return Result::Conflict;
  return Result::Server;
}

Result request(const char* method, const char* path, const std::string* body, std::string& out,
               const std::string* token, const size_t cap) {
  out.clear();
  g_lastStatus = 0;
  if (!enoughHeapForTls()) return Result::LowMemory;
  if (INKDESK.apiBase().empty()) {
    LOG_ERR("INKD", "No server address (install over USB, or build with INKDESK_DEFAULT_API)");
    return Result::Network;
  }

  const std::string url = INKDESK.apiBase() + path;
  freeink::SecureHttpClient http;
  http.setInsecure();
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setUserAgent(std::string("InkDesk-X4/") + FW_VERSION);
  if (!http.begin(url)) {
    LOG_ERR("INKD", "Bad URL: %s", url.c_str());
    return Result::Network;
  }
  http.addHeader("Accept", "application/json");
  if (token) http.addHeader("Authorization", "Device " + *token);
  if (body) http.addHeader("Content-Type", "application/json");

  bool truncated = false;
  const auto sink = [&out, &truncated, cap](const uint8_t* data, const size_t len) {
    const size_t room = cap > out.size() ? cap - out.size() : 0;
    if (len > room) truncated = true;
    out.append(reinterpret_cast<const char*>(data), len < room ? len : room);
    return true;
  };
  const uint8_t* payload = body ? reinterpret_cast<const uint8_t*>(body->data()) : nullptr;
  const size_t payloadLen = body ? body->size() : 0;
  const int status = http.sendRequest(method, payload, payloadLen, sink);
  http.end();
  g_lastStatus = status > 0 ? status : 0;
  if (truncated) LOG_ERR("INKD", "%s %s: response truncated at %zu bytes", method, path, cap);
  LOG_DBG("INKD", "%s %s -> %d (%zu bytes)", method, path, status, out.size());
  const Result r = classify(status, out);
  if (r == Result::Ok && truncated) return Result::Server;
  return r;
}

}  // namespace

Result postJson(const char* path, const std::string& body, std::string& out, const bool auth, const size_t cap) {
  return request("POST", path, &body, out, auth ? &INKDESK.token : nullptr, cap);
}

Result getJson(const char* path, std::string& out, const bool auth, const size_t cap) {
  return request("GET", path, nullptr, out, auth ? &INKDESK.token : nullptr, cap);
}

Result getJsonWithToken(const char* path, std::string& out, const std::string& token, const size_t cap) {
  return request("GET", path, nullptr, out, &token, cap);
}

int lastStatus() { return g_lastStatus; }

std::string sha256Hex(const std::string& text) {
  uint8_t digest[32];
  mbedtls_sha256(reinterpret_cast<const unsigned char*>(text.data()), text.size(), digest, 0);
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return std::string(hex, 64);
}

std::string randomTokenHex() {
  uint8_t bytes[32];
  esp_fill_random(bytes, sizeof(bytes));
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", bytes[i]);
  return std::string(hex, 64);
}

}  // namespace inkdesk::client
