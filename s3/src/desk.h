#pragma once
// EspOS link and sync (docs/espos-api.md): pairing by code, USB provisioning, the periodic
// sync that brings the dashboard and the settings. All network work runs on a worker task; the
// results are applied on the UI loop in poll(), so screens read the state below freely.
#include <Arduino.h>
#include <ArduinoJson.h>

class NetworkClientSecure;
class HTTPClient;

namespace desk {

enum class Link { None, Pairing, Linked };

void begin();
void poll();  // UI loop

Link link();
String deviceName();
String user();

// Pairing: a 6-digit code shown with a QR; the user confirms it on the web.
void startPairing();
void stopPairing();
String pairCode();     // "" until the server took it
String pairUrl();      // what the QR holds
const char* pairStatus();  // short Vietnamese state for the screen
void unlink();

// Remote control over the internet (src/remote.cpp): one POST /v1/device/remote at a time on the kept connection,
// taking turns with the other requests. cb runs on the UI thread with the HTTP status (< 0: no network) and the
// reply. false: one is waiting already, or the device is offline or not linked.
using RemoteCb = void (*)(int status, JsonDocument& reply);
bool remoteSend(const String& body, RemoteCb cb);

// The dog assistant's turn (src/pet/talk.cpp): a request that writes and reads the kept TLS connection itself (a WAV
// up, the voice streamed down as it comes). `fn` runs on the network worker with the connection to the API host
// (connected; nullptr when it could not connect), the host name and the device token; it returns false when it left
// the connection unusable (then it is closed). `done` runs on the UI loop afterwards. It goes before every other
// request. false: one is waiting already, or the device is offline or not linked.
using RawFn = bool (*)(void* ctx, NetworkClientSecure* tls, const char* host, const char* token);
using RawDone = void (*)(void* ctx);
bool rawSend(RawFn fn, RawDone done, void* ctx);

// A JavaScript app's request (src/js/jsnet.cpp, app-platform-plan.md lane 1f): `run` executes on the network worker, in
// turn with the sync, given the kept TLS connection and its HTTPClient (which switches to the app's host by itself);
// nothing of the device's (token, hardware id) is added. `done` runs on the UI loop afterwards, also when the network
// went away before it ran (`run` not called then). Works linked or not. An app request goes before the sync, but a sync
// that is due takes its turn between two of them; the ping waits while an app talked to its server in the last 90 s
// (each switch of host costs a TLS handshake, ~3 s on the board). false: one is waiting already, or no network.
using AppRun = void (*)(void* ctx, NetworkClientSecure* tls, HTTPClient* http);
bool appSend(AppRun run, RawDone done, void* ctx);

// Sync.
void syncNow();
bool syncing();
int64_t lastSyncMs();   // wall clock of the last good sync, 0 = never
const char* lastError();  // "" when the last sync worked
uint32_t changes();     // bumps on link/sync state changes

// The last dashboard (kept on flash, so it shows offline and right after boot).
JsonObjectConst dash();
uint32_t dashVersion();

// Settings: the merged set from the server; device changes go up on the next sync.
String setting(const char* key, const char* def = "");
int settingInt(const char* key, int def);
void setSetting(const char* key, const String& value);
// Marks a setting changed here (it goes up with the next sync) without changing it: a preset's value that the
// provisioning left in place (preset.h).
void touchSetting(const char* key);
// Forgets every setting whose key starts with `prefix` here only (not sent up): an uninstalled package's "app.<id>."
// values, which the server drops itself at the next sync (appset.h).
void dropSettings(const char* prefix);
uint32_t settingsVersion();
#if ESPOS_ONBOARD
bool hadSettings();  // /settings.json was there at boot (a device in use; the first-boot setup's rule, onboard.h)
#endif

// USB provisioning (ESPOS:PROVISION <json>): false with a reason on bad input.
bool provision(const char* json, String& err);

// Firmware offered by the server (version "" when none).
struct FwOffer {
  String version, url, sha256, notes, date;
  uint32_t size = 0;
  bool install = false;
  String usb;  // a newer version too big for this partition table's app slot: USB only (docs/partitions.md)
};
const FwOffer& fwOffer();
void reportOta(const String& version, bool ok, const String& why);

// The board for the trial and Kho máy (admin-users' contract): SHA-256 of "espos-hw:" + the base MAC as 12 lowercase
// hex, and the MAC's last 4 hex digits in mac4. The MAC itself never leaves the device.
String hwid(String* mac4 = nullptr);
String tokenSha8();  // the first 8 hex digits of SHA-256 of the device token ("" when none): Kho máy compares it

// Wall clock now (ms since epoch), 0 when not set yet.
int64_t nowMs();
// The EspOS server this device talks to ("https://api.espos.dev", or a self-hosted one): JS apps' {espos} (jsnet.cpp).
const String& apiBase();
// OS only (src/js/jsnet.cpp): the device's credential, for a JS app's requests to its own path on this server
// ({espos}/v1/device/app/<its id>/*, manifest auth "espos"); "" when not linked. Never to JS, never traced.
const String& deviceToken();

// Apps: GETs on demand (news lists, articles, pictures), one at a time and ahead of the
// periodic sync. `pathOrUrl` is an API path ("/v1/device/news?sec=") sent with the device
// token, or a full https URL (signed storage) sent without it; redirects are followed without
// the token. `bytes` asks for the raw body (pictures, <= 600 KB) instead of parsed JSON. The
// callback runs on the UI loop; `doc` and `data` are freed after it returns.
using FetchCb = void (*)(void* ctx, int status, JsonDocument* doc, const uint8_t* data, size_t len);
void fetch(const String& pathOrUrl, bool bytes, FetchCb cb, void* ctx, bool urgent = false);
// The kept HTTPS connection (one TLS session, reused for minutes): a package download asks for it to be closed
// first so its own connection has the internal RAM; connectionOpen() turns false once the worker closed it.
void releaseConnection();
bool connectionOpen();
void cancelFetches(void* ctx);  // drop the pending ones for ctx (its screen closed)

}  // namespace desk
