#pragma GCC optimize("Os")  // cold code: built small (the app slot is nearly full)
// net.request for JavaScript apps (app-platform-plan.md lane 1f, manifest-driven.md §1.4). See jsnet.h.
//
// The rules, all checked here before anything goes out:
// - https only; plain http only to a local address (10/8, 172.16/12, 192.168/16, 127/8, 169.254/16, "localhost",
//   "*.local", a name without a dot) with `net:lan`, and a name must then resolve to such an address (checked on the
//   worker right before the request). A local address needs `net:lan` over https too.
// - the host must be named by the manifest: `net:<host>` (or `net:*.<domain>`), `net:*` (any public host), or
//   `net:setting:<key>` (the host of the URL the user set in that setting, `app.<id>.<key>`).
// - redirects are followed only to the same origin (3 at most); anything else comes back to the app as the 3xx.
// - the app's own headers never set Authorization, Cookie, Host, Connection, Content-Length or Transfer-Encoding.
// - secrets (app-slots.md §15.1): the request names one of the manifest's `auth` bindings (`auth: "store"`); its
//   `send_to` must match the URL (same origin, `*` in the path and query) or the request is refused. The OS adds its
//   secret fields' values: `basic` [user, pass], `bearer` key or `header` {Name: key}; after a 401 it tries the
//   binding's `query` form ({param: key}, WooCommerce's fallback for hosts that drop Authorization) once, and keeps
//   using the form that worked (NVS). Values come from the device's secret store (src/secret.h, lane 1e) when the
//   request is queued, live only in the request, and are never traced (the trace has no query string).
// - `url` may start with a url setting, `{shop_url}/wp-json/wc/v3/orders`: the OS fills it in.
// - `save: "cache/<name>"`: a 2xx body (up to 512 KB) goes into the app's files (jsfs.h), the answer is {status, size}.
// - the device's credential goes only with `auth: "espos"` (or a `{"device": true}` binding), only to the calling app's
//   own path {espos}/v1/device/app/<its id>/* (ownPath: no "..", "//" or percent-escapes), never through a redirect, never
//   seen by JS (desktop-protocol.md §9.0). The hardware id is never sent. App requests use their own job on desk's worker.
// The answer: the promise resolves for any HTTP status ({status, ok, headers, body}); it rejects only without one,
// with an Error whose `code` is offline|timeout|dns|tls|too_big|denied|busy and `sent` says whether the request may
// have reached the server (a POST that timed out after it was sent: true; a refused connection: false).
#include "jsnet.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <algorithm>
#include <deque>
#include <utility>
#include <vector>

#include "board.h"
#include "desk.h"
#include "jsapp.h"
#include "jsfs.h"
#include "net.h"
#include "pkg/pkg.h"
#include "trace.h"

#if __has_include("secret.h") && !defined(JSNET_NO_SECRETS)
#include "appset.h"
#include "secret.h"
#define JSNET_SECRETS 1
#endif
#include <Preferences.h>

#ifndef SIM
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#else
#include <arpa/inet.h>
#include <curl/curl.h>
#include <lvgl.h>
#include <netdb.h>

#include <mutex>
#include <thread>
#endif

namespace jsnet {
namespace {

constexpr size_t MAX_TEXT = 256 * 1024;  // an answer's body, in PSRAM
constexpr size_t MAX_SAVE = 512 * 1024;  // a body saved to a file
constexpr size_t MAX_SEND = 32 * 1024;   // a request's body
constexpr size_t MAX_QUEUE = 4;          // waiting, besides the one on the network
constexpr int MAX_HEADERS = 6, MAX_KEEP = 4;

// A manifest `auth` binding (app-slots.md §15.1).
struct Binding {
  String name, kind;                             // basic | bearer | header | device (the device's own credential)
  std::vector<String> keys;                      // secret field keys: basic user + pass, bearer, header
  String header;                                 // kind header: the header's name
  std::vector<std::pair<String, String>> query;  // the fallback: param -> secret key
  std::vector<String> sendTo;
  bool useQuery = false;                         // the query form is the one that worked here (NVS)
};

struct App {
  String id;
  JSContext* ctx = nullptr;
  std::vector<String> hosts, settingHosts;
  bool any = false, lan = false, espos = false;  // espos: net:espos (the device's own server, {espos})
  std::vector<Binding> auth;
  uint32_t gen = 0;  // bumps on every stop(): an answer for a closed app is thrown away
} app;

struct Req {
  String app, method, url, host, path;  // path: for the trace, without the query
  String base, auth, save;              // base: the URL before the OS's auth query; auth: the binding's name
  bool query = false, retried = false;  // the binding's query form; tried once after a 401 already
  size_t max = MAX_TEXT;
  uint16_t port = 443;
  bool https = true, lan = false, json = false;
  uint32_t timeout = 20000;
  String body;
  std::vector<std::pair<String, String>> headers;  // the app's and the OS's
  std::vector<String> keep;
  // the answer (written on the worker)
  int status = 0;
  const char* err = nullptr;
  bool sent = false;
  uint8_t* data = nullptr;
  size_t len = 0;
  std::vector<std::pair<String, String>> got;
  uint32_t ms = 0;
  // the promise (UI thread only)
  JSValue res = JS_UNDEFINED, rej = JS_UNDEFINED;
  uint32_t gen = 0;
  // or the OS's own request for an app (osPost: a pop-up's button), answered here instead of to JS
  OsDone osDone = nullptr;
  void* osCtx = nullptr;
  bool device = false;  // it carries the device's credential: no redirect is ever followed
};

std::deque<Req*> waiting;
Req* cur = nullptr;

// ---------------------------------------------------------------- small helpers
String lower(String s) {
  s.toLowerCase();
  return s;
}

// scheme://host[:port]rest. false for anything odd (user info, spaces, no host, a port out of range).
bool parseUrl(const String& url, bool& https, String& host, uint16_t& port, String& rest) {
  https = url.startsWith("https://");
  if (!https && !url.startsWith("http://")) return false;
  const int a = https ? 8 : 7;
  int e = a;
  while (e < (int)url.length() && url[e] != '/' && url[e] != '?' && url[e] != '#') e++;
  String auth = url.substring(a, e);
  if (!auth.length() || auth.indexOf('@') >= 0) return false;
  for (unsigned i = 0; i < url.length(); i++)
    if ((uint8_t)url[i] <= ' ' || url[i] == '\\') return false;
  port = https ? 443 : 80;
  const int c = auth.lastIndexOf(':');
  if (c >= 0 && auth.indexOf(']') < c) {
    const long p = auth.substring(c + 1).toInt();
    if (p <= 0 || p > 65535) return false;
    port = (uint16_t)p;
    auth = auth.substring(0, c);
  }
  host = lower(auth);
  rest = url.substring(e);
  if (!rest.length() || rest[0] != '/') rest = "/" + rest;
  const int h = rest.indexOf('#');
  if (h >= 0) rest = rest.substring(0, h);
  return host.length() > 0;
}

String originOf(bool https, const String& host, uint16_t port) {
  String o = (https ? "https://" : "http://") + host;
  if (port != (https ? 443 : 80)) o += ":" + String(port);
  return o;
}

bool privateV4(uint32_t a) {  // host order
  return (a >> 24) == 10 || (a >> 24) == 127 || (a >> 20) == 0xAC1 || (a >> 16) == 0xC0A8 || (a >> 16) == 0xA9FE ||
         (a >> 24) == 0;
}

// A literal IPv4 address: its value; -1 otherwise.
int64_t literalV4(const String& h) {
  uint32_t v = 0;
  int parts = 0, n = -1;
  for (unsigned i = 0; i <= h.length(); i++) {
    const char ch = i < h.length() ? h[i] : '.';
    if (ch >= '0' && ch <= '9') {
      n = (n < 0 ? 0 : n) * 10 + (ch - '0');
      if (n > 255) return -1;
    } else if (ch == '.' && n >= 0) {
      v = (v << 8) | n, parts++, n = -1;
    } else {
      return -1;
    }
  }
  return parts == 4 ? (int64_t)v : -1;
}

// Could be on the local network: decided for sure on the worker, from the address the name resolves to.
bool lanName(const String& h) {
  const int64_t v = literalV4(h);
  if (v >= 0) return privateV4((uint32_t)v);
  return h == "localhost" || h.endsWith(".local") || h.indexOf('.') < 0;
}

// {espos} (perm net:espos): the device's own EspOS server, so an app's server part works on a self-hosted server too.
// The device's token is never added (an app authenticates with its own `auth` binding, e.g. the app-scoped
// `espos_token` the server delivers as a package secret: desktop-protocol.md §9.0).
String esposBase() {
#ifdef SIM
  const char* e = getenv("SIM_API_BASE");
  String b = e && *e ? e : "https://api.espos.dev";
#else
  String b = desk::apiBase();
#endif
  while (b.endsWith("/")) b.remove(b.length() - 1);
  return b;
}

String settingOf(const String& key) {
  if (key == "espos") return app.espos ? esposBase() : String();
#ifdef JSNET_SECRETS
  return appset::get(app.id.c_str(), key.c_str());  // with its default, and only when it passes the schema
#else
  return desk::setting(("app." + app.id + "." + key).c_str());
#endif
}

bool esposHost(const String& host) {
  bool s;
  String h, r;
  uint16_t p;
  return app.espos && parseUrl(esposBase(), s, h, p, r) && h == host;
}

// The calling app's own path on the device's own server, {espos}/v1/device/app/<its id>/<path>[?query]: the only URLs
// that may carry the device's credential (desktop-protocol.md §9.0). The path is plain: letters, digits, - _ . /, no
// "..", no "//", no percent-escapes (an encoded dot or slash could climb out), no backslash.
bool ownPath(const String& id, const String& url) {
  if (!id.length()) return false;
  const String pre = esposBase() + "/v1/device/app/" + id + "/";
  if (!url.startsWith(pre)) return false;
  String path = url.substring(pre.length());
  const int q = path.indexOf('?');
  if (q >= 0) path = path.substring(0, q);
  if (!path.length() || path[0] == '/' || path.indexOf("..") >= 0 || path.indexOf("//") >= 0) return false;
  for (unsigned i = 0; i < path.length(); i++)
    if (!isalnum((uint8_t)path[i]) && !strchr("-_./", path[i])) return false;
  return true;
}

// The device's credential on a request to the app's own path ("" or why not).
String deviceAuth(Req& r) {
#ifdef SIM
  const char* e = getenv("SIM_DEVICE_TOKEN");
  const String tok = e && *e ? e : "5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e";  // the sim's own
#else
  const String tok = desk::deviceToken();
#endif
  if (!tok.length()) return "this device is not linked to an account";
  r.headers.erase(std::remove_if(r.headers.begin(), r.headers.end(),
                                 [](const std::pair<String, String>& h) { return lower(h.first) == "authorization"; }),
                  r.headers.end());
  r.headers.push_back({"Authorization", "Device " + tok});
  r.device = true;
  return "";
}

bool hostListed(const String& host) {
  if (app.any || esposHost(host)) return true;
  for (const String& h : app.hosts)
    if (h == host || (h.startsWith("*.") && host.endsWith(h.substring(1)))) return true;
  for (const String& k : app.settingHosts) {
    bool s;
    String h, r;
    uint16_t p;
    if (parseUrl(settingOf(k), s, h, p, r) && h == host) return true;
  }
  return false;
}

// '*' matches any run of characters (the path and query after an origin).
bool glob(const char* p, const char* s) {
  const char *star = nullptr, *mark = nullptr;
  while (*s) {
    if (*p == '*') star = p++, mark = s;
    else if (*p == *s) p++, s++;
    else if (star) p = star + 1, s = ++mark;
    else return false;
  }
  while (*p == '*') p++;
  return !*p;
}

// "{shop_url}/wp-json/*" against a URL: placeholders are the app's settings ({espos}: the device's own server, with
// net:espos); the origins must be the same. `of` fills a placeholder (the open app's settings, or another app's).
bool sendToMatches(const String& pattern, const String& url, String (*of)(const String&) = nullptr) {
  String p;
  for (unsigned i = 0; i < pattern.length(); i++) {
    if (pattern[i] != '{') {
      p += pattern[i];
      continue;
    }
    const int e = pattern.indexOf('}', i);
    if (e < 0) return false;
    String v = (of ? of : settingOf)(pattern.substring(i + 1, e));
    while (v.endsWith("/")) v.remove(v.length() - 1);
    if (!v.length()) return false;  // not set yet: no secret goes anywhere
    p += v;
    i = e;
  }
  if (!p.startsWith("https://") && !p.startsWith("http://")) p = "https://" + p;
  bool ps, us;
  String ph, uh, pr, ur;
  uint16_t pp, up;
  if (!parseUrl(p, ps, ph, pp, pr) || !parseUrl(url, us, uh, up, ur)) return false;
  if (originOf(ps, ph, pp) != originOf(us, uh, up)) return false;
  return glob(pr.c_str(), ur.c_str());
}

String b64(const String& in) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String o;
  const uint8_t* s = (const uint8_t*)in.c_str();
  const size_t n = in.length();
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = s[i] << 16 | (i + 1 < n ? s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
    o += T[v >> 18 & 63];
    o += T[v >> 12 & 63];
    o += i + 1 < n ? T[v >> 6 & 63] : '=';
    o += i + 2 < n ? T[v & 63] : '=';
  }
  return o;
}

String urlenc(const String& s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    const uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      o += (char)c;
    } else {
      char b[4];
      snprintf(b, sizeof b, "%%%02X", c);
      o += b;
    }
  }
  return o;
}

Binding* binding(const String& name) {
  for (Binding& b : app.auth)
    if (b.name == name) return &b;
  return nullptr;
}

String bindingKey(const Binding& b) {  // its NVS key for "the query form worked" (15 characters at most)
  uint32_t h = 2166136261u;
  for (const String* s : {(const String*)&app.id, &b.name})
    for (unsigned i = 0; i < s->length(); i++) h = (h ^ (uint8_t)(*s)[i]) * 16777619u;
  char k[12];
  snprintf(k, sizeof k, "q%08lx", (unsigned long)h);
  return k;
}

// The OS's part: the binding's secrets on the request (r.base is the URL without them). "" or why not.
String addAuth(Req& r, const Binding& b, bool query) {
#ifdef JSNET_SECRETS
  auto val = [&](const String& key, String& v) { return secret::get(r.app.c_str(), key.c_str(), v) && v.length(); };
  r.url = r.base;
  const String own = b.kind == "header" ? lower(b.header) : String("authorization");
  r.headers.erase(std::remove_if(r.headers.begin(), r.headers.end(),
                                 [&](const std::pair<String, String>& h) { return lower(h.first) == own; }),
                  r.headers.end());
  r.query = query;
  if (query) {
    String q;
    for (const auto& kv : b.query) {
      String v;
      if (!val(kv.second, v)) return "the secret " + kv.second + " is not set";
      q += (q.length() ? "&" : "") + urlenc(kv.first) + "=" + urlenc(v);
    }
    r.url += (r.url.indexOf('?') < 0 ? "?" : "&") + q;
    return "";
  }
  String v1, v2;
  if (b.keys.empty() || !val(b.keys[0], v1)) return "the secret " + (b.keys.size() ? b.keys[0] : String("?")) + " is not set";
  if (b.kind == "basic") {
    if (b.keys.size() < 2 || !val(b.keys[1], v2)) return "the secret " + (b.keys.size() > 1 ? b.keys[1] : String("?")) + " is not set";
    r.headers.push_back({"Authorization", "Basic " + b64(v1 + ":" + v2)});
  } else if (b.kind == "bearer") {
    r.headers.push_back({"Authorization", "Bearer " + v1});
  } else {
    r.headers.push_back({b.header, v1});
  }
  return "";
#else
  (void)r, (void)b, (void)query;
  return "secrets are not built in";
#endif
}

JSValue mkErr(JSContext* ctx, const char* code, bool sent, const String& msg) {
  JSValue e = JS_NewError(ctx);
  JS_SetPropertyStr(ctx, e, "message", JS_NewString(ctx, msg.c_str()));
  JS_SetPropertyStr(ctx, e, "code", JS_NewString(ctx, code));
  JS_SetPropertyStr(ctx, e, "sent", JS_NewBool(ctx, sent));
  return e;
}

JSValue mkRes(JSContext* ctx, const Req& r) {
  JSValue o = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, o, "status", JS_NewInt32(ctx, r.status));
  JS_SetPropertyStr(ctx, o, "ok", JS_NewBool(ctx, r.status >= 200 && r.status < 300));
  JSValue h = JS_NewObject(ctx);
  for (const auto& kv : r.got) JS_SetPropertyStr(ctx, h, kv.first.c_str(), JS_NewString(ctx, kv.second.c_str()));
  JS_SetPropertyStr(ctx, o, "headers", h);
  if (r.save.length() && r.status >= 200 && r.status < 300) JS_SetPropertyStr(ctx, o, "size", JS_NewInt64(ctx, r.len));
  else JS_SetPropertyStr(ctx, o, "body", JS_NewStringLen(ctx, r.data ? (const char*)r.data : "", r.len));
  return o;
}

String argStr(JSContext* ctx, JSValueConst v) {
  if (JS_IsUndefined(v) || JS_IsNull(v)) return "";
  const char* s = JS_ToCString(ctx, v);
  String o = s ? s : "";
  JS_FreeCString(ctx, s);
  return o;
}

void pump();

// The answer, on the UI thread: settle the promise (unless the app closed), then the next request.
void finish(Req* r) {
  if (r == cur) cur = nullptr;
  if (!r->err && r->status == 0) r->err = "offline";  // the network went before it ran (desk::appSend)
  if (r->osDone) {  // the OS's own request (osPost)
    trace::add("os for %s: %s %s%s -> %d%s%s", r->app.c_str(), r->method.c_str(), r->host.c_str(), r->path.c_str(),
               r->status, r->err ? " " : "", r->err ? r->err : "");
    String body;
    if (r->data) body.concat((const char*)r->data, r->len);
    r->osDone(r->osCtx, r->err ? 0 : r->status, r->err, body);
    if (r->data) heap_caps_free(r->data);
    for (auto& kv : r->headers) kv.second = String();
    delete r;
    pump();
    return;
  }
  Binding* b = r->auth.length() && r->gen == app.gen ? binding(r->auth) : nullptr;
  if (b && !r->err) {
    if (r->status == 401 && !r->query && !r->retried && b->query.size()) {  // the host dropped Authorization?
      trace::add("app %s: 401 with %s, trying its query form", r->app.c_str(), b->name.c_str());
      r->retried = true;
      if (r->data) heap_caps_free(r->data);
      r->data = nullptr, r->len = 0, r->status = 0, r->sent = false, r->got.clear();
      if (!addAuth(*r, *b, true).length()) {
        waiting.push_front(r);
        pump();
        return;
      }
    } else if (r->status != 401 && r->query != b->useQuery) {  // this form works here: keep it
      b->useQuery = r->query;
      Preferences p;
      p.begin("jsnet", false);
      p.putBool(bindingKey(*b).c_str(), b->useQuery);
      p.end();
    }
  }
  if (!r->err && r->save.length() && r->status >= 200 && r->status < 300 && r->gen == app.gen &&
      !jsfs::writeBytes(r->save.c_str(), r->data, r->len))
    r->err = "too_big";  // over the cache quota, or the file could not be written
  trace::add("app %s: %s %s%s -> %d%s%s, %u B, %lu ms", r->app.c_str(), r->method.c_str(), r->host.c_str(),
             r->path.c_str(), r->status, r->err ? " " : "", r->err ? r->err : "", (unsigned)r->len, (unsigned long)r->ms);
  if (app.ctx && r->gen == app.gen) {
    JSContext* ctx = app.ctx;
    const bool bad = r->err != nullptr;
    JSValue arg = bad ? mkErr(ctx, r->err, r->sent,
                              String("net.request: ") + r->err + (r->sent ? " (the request may have reached the server)" : ""))
                      : mkRes(ctx, *r);
    JSValue fn = JS_DupValue(ctx, bad ? r->rej : r->res);
    JS_FreeValue(ctx, r->res);
    JS_FreeValue(ctx, r->rej);
    const uint32_t gen = app.gen;
    jsapp::invoke(fn, 1, &arg);
    if (gen == app.gen) {  // the app did not close meanwhile (its context is gone then)
      JS_FreeValue(ctx, fn);
      JS_FreeValue(ctx, arg);
    }
  }
  if (r->data) heap_caps_free(r->data);
  for (auto& kv : r->headers) kv.second = String();  // a secret's value: not left in freed memory longer than needed
  delete r;
  pump();
}

// ---------------------------------------------------------------- on the network (board)
#ifndef SIM
// The answer's body into PSRAM, up to MAX_TEXT (HTTPClient::writeToStream decodes chunked bodies).
struct Sink : public Stream {
  uint8_t* buf = nullptr;
  size_t len = 0, cap = 0, max = MAX_TEXT;
  bool over = false;
  size_t write(const uint8_t* b, size_t n) override {
    if (len + n > max) return over = true, 0;
    if (len + n > cap) {
      size_t c = cap ? cap * 2 : 8192;
      while (c < len + n) c *= 2;
      if (c > max) c = max;
      uint8_t* nb = (uint8_t*)heap_caps_realloc(buf, c, MALLOC_CAP_SPIRAM);
      if (!nb) return over = true, 0;
      buf = nb, cap = c;
    }
    memcpy(buf + len, b, n);
    len += n;
    return n;
  }
  size_t write(uint8_t c) override { return write(&c, 1); }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

bool privateIp(const IPAddress& ip) {
  return privateV4((uint32_t)ip[0] << 24 | (uint32_t)ip[1] << 16 | (uint32_t)ip[2] << 8 | ip[3]);
}

// On desk.cpp's network worker, on the kept TLS connection (https) or a plain one of its own (http, LAN only).
void runOnWorker(void* ctx, NetworkClientSecure* tls, HTTPClient* kept) {
  Req& r = *(Req*)ctx;
  const uint32_t t0 = millis();
  IPAddress ip;
  if (!ip.fromString(r.host) && WiFi.hostByName(r.host.c_str(), ip) != 1) {
    r.err = "dns", r.ms = millis() - t0;
    return;
  }
  const bool priv = privateIp(ip);
  if ((priv && !r.lan) || (!r.https && !priv)) {  // a public name that points at the local network, or http outside it
    r.err = "denied", r.ms = millis() - t0;
    return;
  }
  NetworkClient plain;
  HTTPClient local;
  HTTPClient& h = r.https ? *kept : local;
  String url = r.url;
  bool retried = false;
  for (int hop = 0; hop < 4; hop++) {
    const bool reused = r.https && tls->connected();
    if (!(r.https ? h.begin(*tls, url) : h.begin(plain, url))) {
      r.err = "denied";
      break;
    }
    h.setConnectTimeout(r.timeout < 10000 ? r.timeout : 10000);
    h.setTimeout(r.timeout);
    h.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    h.setUserAgent(String("EspOS/") + FW_VERSION);
    std::vector<const char*> names = {"Location", "Content-Type"};
    for (const String& k : r.keep) names.push_back(k.c_str());
    h.collectHeaders(names.data(), names.size());
    for (const auto& kv : r.headers) h.addHeader(kv.first, kv.second);
    const uint32_t s0 = millis();
    const int code = h.sendRequest(r.method.c_str(), (uint8_t*)r.body.c_str(), r.body.length());
    if (code < 0) {
      // A kept connection the server had closed fails at once and nothing reached the server: once more, fresh.
      const bool stale = code == HTTPC_ERROR_SEND_HEADER_FAILED || code == HTTPC_ERROR_SEND_PAYLOAD_FAILED ||
                         code == HTTPC_ERROR_NOT_CONNECTED ||
                         (code == HTTPC_ERROR_CONNECTION_LOST && r.method == "GET" && millis() - s0 < 1500);
      h.end();
      if (r.https) tls->stop();
      if (reused && stale && !retried) {
        retried = true, hop--;
        continue;
      }
      r.sent = code == HTTPC_ERROR_CONNECTION_LOST || code == HTTPC_ERROR_READ_TIMEOUT;
      char e[64] = "";
      const int te = r.https ? tls->lastError(e, sizeof e) : 0;
      r.err = code == HTTPC_ERROR_READ_TIMEOUT                            ? "timeout"
              : code == HTTPC_ERROR_CONNECTION_REFUSED && te <= -0x1000   ? "tls"
              : code == HTTPC_ERROR_CONNECTION_REFUSED && millis() - s0 + 200 >= (r.timeout < 10000 ? r.timeout : 10000)
                  ? "timeout"
                  : "offline";
      break;
    }
    r.sent = true;
    const bool redirect = code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
    if (redirect && hop < 3 && !r.device) {  // the device's credential never follows a redirect
      String loc = h.header("Location");
      if (loc.startsWith("/")) loc = originOf(r.https, r.host, r.port) + loc;
      bool s;
      String lh, lr;
      uint16_t lp;
      if (parseUrl(loc, s, lh, lp, lr) && originOf(s, lh, lp) == originOf(r.https, r.host, r.port)) {
        h.getString();  // drain, the connection serves the next hop
        url = loc;
        if (code == 303 || ((code == 301 || code == 302) && r.method != "GET" && r.method != "HEAD"))
          r.method = "GET", r.body = String();
        continue;
      }
    }
    r.status = code;
    const int size = h.getSize();
    if (size > (int)r.max) {
      r.err = "too_big";
      h.end();
      if (r.https) tls->stop();
      break;
    }
    Sink sink;
    sink.max = r.max;
    if (r.method != "HEAD" && size != 0) {
      const int n = h.writeToStream(&sink);
      if (sink.over) r.err = "too_big";
      else if (n < 0) r.err = "timeout";
    }
    if (r.err) {
      heap_caps_free(sink.buf);
      h.end();
      if (r.https) tls->stop();
      break;
    }
    r.data = sink.buf, r.len = sink.len;
    for (const char* k : names)
      if (h.hasHeader(k)) r.got.push_back({lower(k), h.header(k)});
    h.end();
    break;
  }
  if (!r.https) plain.stop();
  r.ms = millis() - t0;
}

void doneOnUi(void* ctx) { finish((Req*)ctx); }

void start(Req* r) {
  if (!desk::appSend(runOnWorker, doneOnUi, r)) {
    r->err = "offline";
    lv_async_call([](void* p) { finish((Req*)p); }, r);
  }
}

// ---------------------------------------------------------------- on the network (simulator: libcurl on a thread)
#else
std::mutex doneMu;
std::vector<Req*> doneList;
lv_timer_t* doneTimer = nullptr;

struct Body {
  Req* r;
  bool over;
};
size_t onBody(char* p, size_t s, size_t n, void* u) {
  Body& b = *(Body*)u;
  const size_t k = s * n;
  if (b.r->len + k > b.r->max) return b.over = true, 0;
  b.r->data = (uint8_t*)realloc(b.r->data, b.r->len + k + 1);
  memcpy(b.r->data + b.r->len, p, k);
  b.r->len += k;
  return k;
}
size_t onHeader(char* p, size_t s, size_t n, void* u) {
  Req& r = *(Req*)u;
  String line;
  line.concat(p, s * n);
  line.trim();
  const int c = line.indexOf(':');
  if (c > 0) {
    const String k = lower(line.substring(0, c));
    String v = line.substring(c + 1);
    v.trim();
    bool want = k == "location" || k == "content-type";
    for (const String& w : r.keep) want = want || lower(w) == k;
    if (want) r.got.push_back({k, v});
  }
  return s * n;
}

// What went out: the request's head, and how much of its body (sent = it may have reached the server).
struct Out {
  bool head = false;
  size_t body = 0;
};
int onDebug(CURL*, curl_infotype t, char*, size_t n, void* u) {
  Out& o = *(Out*)u;
  if (t == CURLINFO_HEADER_OUT) o.head = true;
  if (t == CURLINFO_DATA_OUT) o.body += n;
  return 0;
}

bool lookup(const String& host, bool& priv) {
  addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_INET;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) || !res) return false;
  priv = privateV4(ntohl(((sockaddr_in*)res->ai_addr)->sin_addr.s_addr));
  freeaddrinfo(res);
  return true;
}

void perform(Req& r) {
  const uint32_t t0 = millis();
  bool priv = false;
  if (!lookup(r.host, priv)) {
    r.err = "dns";
    return;
  }
  if ((priv && !r.lan) || (!r.https && !priv)) {
    r.err = "denied";
    return;
  }
  String url = r.url;
  for (int hop = 0; hop < 4; hop++) {
    r.got.clear();
    free(r.data), r.data = nullptr, r.len = 0;
    CURL* c = curl_easy_init();
    curl_slist* hl = nullptr;
    for (const auto& kv : r.headers) hl = curl_slist_append(hl, (kv.first + ": " + kv.second).c_str());
    hl = curl_slist_append(hl, "Expect:");
    Body body{&r, false};
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, r.method.c_str());
    if (r.method == "HEAD") curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    if (r.body.length() || r.method == "POST" || r.method == "PUT" || r.method == "PATCH") {
      curl_easy_setopt(c, CURLOPT_POSTFIELDS, r.body.c_str());
      curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)r.body.length());
    }
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(c, CURLOPT_USERAGENT, (String("EspOS/") + FW_VERSION + " (sim)").c_str());
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, (long)(r.timeout < 10000 ? r.timeout : 10000));
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)r.timeout);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, onHeader);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &r);
    if (getenv("SIM_NET_CA")) curl_easy_setopt(c, CURLOPT_CAINFO, getenv("SIM_NET_CA"));  // a test server's own CA
    Out out;
    curl_easy_setopt(c, CURLOPT_DEBUGFUNCTION, onDebug);
    curl_easy_setopt(c, CURLOPT_DEBUGDATA, &out);
    curl_easy_setopt(c, CURLOPT_VERBOSE, 1L);  // the debug callback only runs in verbose mode (it prints nothing)
    const CURLcode e = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    const bool wentOut = out.head && out.body >= r.body.length();
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    if (body.over) {
      r.err = "too_big", r.sent = true;
      break;
    }
    if (e != CURLE_OK && !code) {
      r.sent = wentOut;
      r.err = e == CURLE_COULDNT_RESOLVE_HOST                              ? "dns"
              : e == CURLE_OPERATION_TIMEDOUT                              ? "timeout"
              : e == CURLE_SSL_CONNECT_ERROR || e == CURLE_PEER_FAILED_VERIFICATION ? "tls"
                                                                           : "offline";
      break;
    }
    if (e != CURLE_OK) {  // an answer started, then the connection broke or timed out
      r.err = e == CURLE_OPERATION_TIMEDOUT ? "timeout" : "offline", r.sent = true;
      break;
    }
    r.sent = true;
    const bool redirect = code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
    if (redirect && hop < 3 && !r.device) {  // the device's credential never follows a redirect
      String loc;
      for (const auto& kv : r.got)
        if (kv.first == "location") loc = kv.second;
      if (loc.startsWith("/")) loc = originOf(r.https, r.host, r.port) + loc;
      bool s;
      String lh, lr;
      uint16_t lp;
      if (parseUrl(loc, s, lh, lp, lr) && originOf(s, lh, lp) == originOf(r.https, r.host, r.port)) {
        url = loc;
        if (code == 303 || ((code == 301 || code == 302) && r.method != "GET" && r.method != "HEAD"))
          r.method = "GET", r.body = String();
        continue;
      }
    }
    r.status = (int)code;
    break;
  }
  if (r.data) {  // into "PSRAM" like the board (finish frees it with heap_caps_free)
    uint8_t* p = (uint8_t*)heap_caps_malloc(r.len + 1, MALLOC_CAP_SPIRAM);
    if (p) memcpy(p, r.data, r.len);
    free(r.data);
    r.data = p;
  }
  r.ms = millis() - t0;
}

void start(Req* r) {
  static bool once = false;
  if (!once) once = true, curl_global_init(CURL_GLOBAL_DEFAULT);
  if (!doneTimer)
    doneTimer = lv_timer_create([](lv_timer_t*) {
      std::vector<Req*> d;
      {
        std::lock_guard<std::mutex> l(doneMu);
        d.swap(doneList);
      }
      for (Req* r : d) finish(r);
    }, 20, nullptr);
  std::thread([r] {
    perform(*r);
    std::lock_guard<std::mutex> l(doneMu);
    doneList.push_back(r);
  }).detach();
}
#endif

void pump() {
  if (cur || waiting.empty()) return;
  cur = waiting.front();
  waiting.pop_front();
  start(cur);
}

// ---------------------------------------------------------------- the natives
// __net(method, url, body, json, headers, keep, timeout, auth, save, resolve, reject): the prelude's net.request.
JSValue nNet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 11 || !JS_IsFunction(ctx, argv[9]) || !JS_IsFunction(ctx, argv[10]))
    return JS_ThrowTypeError(ctx, "net.request(options)");
  Req* r = new Req;
  auto fail = [&](const char* code, const String& msg) {
    delete r;
    return JS_Throw(ctx, mkErr(ctx, code, false, "net.request: " + msg));
  };
  r->app = app.id;
  r->method = argStr(ctx, argv[0]);
  r->method.toUpperCase();
  if (r->method != "GET" && r->method != "POST" && r->method != "PUT" && r->method != "PATCH" && r->method != "DELETE" &&
      r->method != "HEAD")
    return fail("denied", "method " + r->method + " is not allowed");
  r->url = argStr(ctx, argv[1]);
  if (r->url.startsWith("{")) {  // "{shop_url}/wp-json/...": the address the user set
    const int e = r->url.indexOf('}');
    const String key = e > 1 ? r->url.substring(1, e) : "";
    String v = key.length() ? settingOf(key) : "";
    while (v.endsWith("/")) v.remove(v.length() - 1);
    if (!v.length()) return fail("denied", "the setting " + key + " is not set");
    r->url = v + r->url.substring(e + 1);
  }
  String rest;
  if (r->url.length() > 2048 || !parseUrl(r->url, r->https, r->host, r->port, rest))
    return fail("denied", "not an http(s) address: " + r->url.substring(0, 80));
  r->path = rest.indexOf('?') >= 0 ? rest.substring(0, rest.indexOf('?')) : rest;
  if (r->path.length() > 80) r->path = r->path.substring(0, 80);
  const int h = r->url.indexOf('#');
  if (h >= 0) r->url = r->url.substring(0, h);
  // Where it may go: a local address with net:lan, else a listed host; http only to the local network.
  const bool lanish = lanName(r->host), own = esposHost(r->host);  // own: a self-hosted server may be on the LAN
  r->lan = app.lan || own;
  if (!(lanish ? r->lan || hostListed(r->host) : hostListed(r->host)))
    return fail("denied", r->host + " is not in the app's permissions (net:" + r->host + ")");
  if (!r->https && !(lanish && r->lan)) return fail("denied", "http:// is for the local network (net:lan); use https://");
  r->body = argStr(ctx, argv[2]);
  if (r->body.length() > MAX_SEND) return fail("too_big", "the body is over 32 KB");
  r->json = JS_ToBool(ctx, argv[3]) > 0;
  // The app's headers.
  bool type = false;
  if (JS_IsObject(argv[4])) {
    JSPropertyEnum* props = nullptr;
    uint32_t n = 0;
    if (!JS_GetOwnPropertyNames(ctx, &props, &n, argv[4], JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
      for (uint32_t i = 0; i < n; i++) {
        const char* k = JS_AtomToCString(ctx, props[i].atom);
        JSValue v = JS_GetProperty(ctx, argv[4], props[i].atom);
        const String key = k ? k : "", val = argStr(ctx, v);
        JS_FreeCString(ctx, k);
        JS_FreeValue(ctx, v);
        const String lk = lower(key);
        if (lk == "authorization" || lk == "cookie" || lk == "host" || lk == "connection" || lk == "content-length" ||
            lk == "transfer-encoding" || lk == "expect" || !key.length() || key.length() > 40 || val.length() > 512 ||
            val.indexOf('\n') >= 0 || val.indexOf('\r') >= 0 || key.indexOf(':') >= 0 ||
            (int)r->headers.size() >= MAX_HEADERS) {
          JS_FreePropertyEnum(ctx, props, n);
          return fail("denied", "header " + key + " is not allowed (at most 6, no Authorization or Cookie)");
        }
        if (lk == "content-type") type = true;
        r->headers.push_back({key, val});
      }
      JS_FreePropertyEnum(ctx, props, n);
    }
  }
  if (r->json && !type) r->headers.push_back({"Content-Type", "application/json"});
  if (JS_IsArray(argv[5])) {
    int64_t n = 0;
    JS_GetLength(ctx, argv[5], &n);
    for (int64_t i = 0; i < n && i < MAX_KEEP; i++) {
      JSValue v = JS_GetPropertyUint32(ctx, argv[5], (uint32_t)i);
      const String k = argStr(ctx, v);
      JS_FreeValue(ctx, v);
      if (k.length() && k.length() <= 40) r->keep.push_back(k);
    }
  }
  int32_t t = 0;
  JS_ToInt32(ctx, &t, argv[6]);
  r->timeout = t <= 0 ? 20000 : t < 1000 ? 1000 : t > 60000 ? 60000 : t;
  r->save = argStr(ctx, argv[8]);
  if (r->save.length()) {
    if (!r->save.startsWith("cache/") || !jsfs::path(r->save.c_str()).length())
      return fail("denied", "save goes to cache/<name> (a-z 0-9 . _ -) and needs the cache:<KB> permission");
    r->max = MAX_SAVE;
  }
  if (waiting.size() >= MAX_QUEUE) return fail("busy", "too many requests at once (5)");
  if (net::state() != net::State::Connected) return fail("offline", "no network");
  r->base = r->url;
  r->auth = argStr(ctx, argv[7]);
  Binding* mine = binding(r->auth);
  if (r->auth == "espos" || (mine && mine->kind == "device")) {
    // The app's own server part (net:espos): the device's credential, only to {espos}/v1/device/app/<this app>/*.
    // A manifest binding of that name whose secret is set goes instead (the app-scoped token, a later upgrade).
    if (!app.espos || !ownPath(app.id, r->url))
      return fail("denied", "auth " + r->auth + " is only for this app's own path, {espos}/v1/device/app/" + app.id + "/...");
    if (!(mine && mine->kind != "device" && !addAuth(*r, *mine, false).length())) {
      const String why = deviceAuth(*r);
      if (why.length()) return fail("denied", why);
    }
    r->device = true;
  } else if (r->auth.length()) {
    Binding* b = mine;
    if (!b) return fail("denied", "no auth binding \"" + r->auth + "\" in the manifest");
    bool to = false;
    for (const String& p : b->sendTo) to = to || sendToMatches(p, r->url);
    if (!to) return fail("denied", "auth " + r->auth + " is not for this address (its send_to)");
    const String why = addAuth(*r, *b, b->useQuery && b->query.size());
    if (why.length()) return fail("denied", why);
  }
  r->res = JS_DupValue(ctx, argv[9]);
  r->rej = JS_DupValue(ctx, argv[10]);
  r->gen = app.gen;
  waiting.push_back(r);
  pump();
  return JS_UNDEFINED;
}

JSValue nOnline(JSContext* ctx, JSValueConst, int, JSValueConst*) {
  return JS_NewBool(ctx, net::state() == net::State::Connected);
}

}  // namespace

std::vector<Binding> bindingsOf(const String& id);

void install(JSContext* ctx, const pkg::Package& p) {
  stop();
  app.id = p.id;
  app.ctx = ctx;
  for (const String& s : p.perms) {
    if (!s.startsWith("net:")) continue;
    const String v = s.substring(4);
    if (v == "*") app.any = true;
    else if (v == "lan") app.lan = true;
    else if (v == "espos") app.espos = true;
    else if (v.startsWith("setting:")) app.settingHosts.push_back(v.substring(8));
    else if (v.length()) app.hosts.push_back(lower(v));
  }
  app.auth = bindingsOf(p.id);
  JSValue g = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, g, "__net", JS_NewCFunction(ctx, nNet, "__net", 11));
  JS_SetPropertyStr(ctx, g, "__netOnline", JS_NewCFunction(ctx, nOnline, "__netOnline", 0));
  JS_FreeValue(ctx, g);
}

// The manifest's auth bindings (app-slots.md §15.1) of an installed package.
std::vector<Binding> bindingsOf(const String& id) {
  std::vector<Binding> out;
  const String saved = app.id;
  app.id = id;  // bindingKey() names the NVS key after the app
  const String m = pkg::manifestJson(id.c_str());
  JsonDocument d;
  if (m.length() && deserializeJson(d, m) == DeserializationError::Ok) {
    Preferences pr;
    pr.begin("jsnet", false);  // read-write: a read-only open of a namespace not made yet logs nvs NOT_FOUND each time
    for (JsonPairConst kv : d["auth"].as<JsonObjectConst>()) {
      JsonObjectConst o = kv.value();
      Binding b;
      b.name = kv.key().c_str();
      if (o["basic"].is<JsonArrayConst>()) {
        b.kind = "basic";
        for (JsonVariantConst v : o["basic"].as<JsonArrayConst>()) b.keys.push_back(v | "");
      } else if (o["bearer"].is<const char*>()) {
        b.kind = "bearer", b.keys.push_back(o["bearer"] | "");
      } else if (o["header"].is<JsonObjectConst>()) {
        for (JsonPairConst h : o["header"].as<JsonObjectConst>()) b.kind = "header", b.header = h.key().c_str(), b.keys = {h.value() | ""};
      } else if (o["device"] | false) {
        b.kind = "device";  // the device's own credential, to the app's own path only (ownPath)
      }
      for (JsonPairConst q : o["query"].as<JsonObjectConst>()) b.query.push_back({q.key().c_str(), q.value() | ""});
      for (JsonVariantConst v : o["send_to"].as<JsonArrayConst>()) b.sendTo.push_back(v | "");
      if (!b.sendTo.size() || (b.kind != "device" && b.keys.empty() && b.query.empty())) continue;
      b.useQuery = b.keys.empty() || pr.getBool(bindingKey(b).c_str(), false);
      out.push_back(b);
    }
    pr.end();
  }
  app.id = saved;
  return out;
}

void stop() {
  std::deque<Req*> os;  // the OS's own requests are not the app's: they stay
  for (Req* r : waiting)
    if (r->osDone) os.push_back(r);
  waiting.erase(std::remove_if(waiting.begin(), waiting.end(), [](Req* r) { return r->osDone != nullptr; }), waiting.end());
  if (app.ctx) {
    for (Req* r : waiting) {
      JS_FreeValue(app.ctx, r->res);
      JS_FreeValue(app.ctx, r->rej);
      delete r;
    }
    if (cur && cur->gen == app.gen) {  // on the network now: its answer is dropped in finish()
      JS_FreeValue(app.ctx, cur->res);
      JS_FreeValue(app.ctx, cur->rej);
      cur->res = cur->rej = JS_UNDEFINED;
    }
  }
  waiting = os;
  app.gen++;
  const uint32_t gen = app.gen;
  app = App();
  app.gen = gen;
}

bool osPost(const char* appId, const char* path, const String& json, OsDone done, void* ctx) {
  const pkg::Package* p = pkg::find(appId);
  bool espos = false;
  for (const String& perm : p ? p->perms : std::vector<String>()) espos = espos || perm == "net:espos";
  String ps = path ? path : "";
  bool okPath = ps.length() > 0 && ps.length() <= 40 && ps.indexOf("..") < 0;
  for (unsigned i = 0; okPath && i < ps.length(); i++) okPath = isalnum((uint8_t)ps[i]) || strchr("_-/", ps[i]);
  if (!espos || !okPath || !done || net::state() != net::State::Connected || waiting.size() >= MAX_QUEUE + 4) return false;
  Req* r = new Req;
  r->app = appId, r->method = "POST", r->json = true, r->body = json, r->timeout = 20000;
  r->url = r->base = esposBase() + "/v1/device/app/" + appId + "/" + ps;
  String rest;
  if (!parseUrl(r->url, r->https, r->host, r->port, rest) || r->body.length() > MAX_SEND) return delete r, false;
  r->path = rest, r->lan = true;  // the device's own server (a self-hosted one may be on the LAN)
  r->headers.push_back({"Content-Type", "application/json"});
  static String forApp;  // sendToMatches' placeholders for this app: {espos} and its url settings
  forApp = appId;
  auto of = [](const String& key) -> String {
    if (key == "espos") return esposBase();
#ifdef JSNET_SECRETS
    return appset::get(forApp.c_str(), key.c_str());
#else
    return String();
#endif
  };
  bool authed = false;
  for (Binding& b : bindingsOf(appId)) {  // a binding with its secret set (the later app-scoped token)
    bool to = false;
    for (const String& pat : b.sendTo) to = to || sendToMatches(pat, r->url, of);
    if (!to || b.kind == "device" || addAuth(*r, b, false).length()) continue;
    r->auth = b.name, authed = true;
    break;
  }
  if (!authed && (!ownPath(appId, r->url) || deviceAuth(*r).length())) return delete r, false;
  r->device = true;
  r->osDone = done, r->osCtx = ctx;
  waiting.push_back(r);
  pump();
  return true;
}

}  // namespace jsnet
