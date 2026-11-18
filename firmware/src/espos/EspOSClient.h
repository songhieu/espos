#pragma once

#include <cstddef>
#include <string>

// HTTPS calls to the EspOS API (contract: docs/espos-api.md).
// All calls block; run them from an activity's loop() with WiFi up and the
// reader closed (TLS needs ~40 KB of free heap).
namespace espos::client {

enum class Result {
  Ok,
  Unauthorized,  // 401 unauthorized: unknown or revoked token
  NotLinked,     // 401 not_linked: pairing still pending
  Conflict,      // 409 (pairing code taken)
  LowMemory,
  Network,
  Server,
};

// POST JSON. `auth` adds "Authorization: Device <token>". The response body
// (any status) lands in `out`, capped at `cap` bytes.
Result postJson(const char* path, const std::string& body, std::string& out, bool auth, size_t cap = 32 * 1024);

// GET JSON, same rules.
Result getJson(const char* path, std::string& out, bool auth, size_t cap = 8 * 1024);

// GET JSON authenticated with an explicit device token (pairing: the token is
// not stored until the link is confirmed).
Result getJsonWithToken(const char* path, std::string& out, const std::string& token, size_t cap = 4 * 1024);

// Last HTTP status (0 = no response).
int lastStatus();

// Lowercase hex SHA-256 of `text` (64 chars).
std::string sha256Hex(const std::string& text);

// 32 random bytes as 64 lowercase hex chars (hardware RNG).
std::string randomTokenHex();

}  // namespace espos::client
