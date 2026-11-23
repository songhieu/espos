#pragma GCC optimize("Os")  // cold code: built small (the app slot is nearly full)
// See lanform.h.
#include "lanform.h"

#include <Arduino.h>
#include <lvgl.h>

#include <atomic>
#include <vector>

#include "appset.h"
#include "i18n.h"
#include "net.h"
#include "os.h"
#include "trace.h"
#include "uikit.h"

#ifndef SIM
#include <esp_http_server.h>
#include "esp_random.h"
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>
#endif

namespace lanform {
namespace {

using namespace kit;

constexpr int PORT = 8080;
constexpr uint32_t OPEN_MS = 10 * 60000;

struct Form {
  String id, pin;
  std::vector<appset::Field> fields;  // url / string fields and device secrets
  lv_obj_t* scr = nullptr;
  lv_obj_t* status = nullptr;
  int wrong = 0;
  uint32_t opened = 0;
  bool done = false;
  // A post waiting for the UI loop (the server's task hands it over and waits for the answer).
  std::vector<std::pair<String, String>> posted;
  std::atomic<int> state{0};  // 0 idle, 1 posted (UI loop applies), 2 answered
  String answer;
} F;

#ifndef SIM
httpd_handle_t server = nullptr;
#else
int listenFd = -1;
std::atomic<bool> simRun{false};
#endif

String html(const String& s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

String urldecode(const String& s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.length()) o += (char)strtol(s.substring(i + 1, i + 3).c_str(), nullptr, 16), i += 2;
    else o += s[i];
  }
  return o;
}

String page(const String& note) {
  String f;
  for (const appset::Field& x : F.fields) {
    const bool secret = x.type == "secret";
    f += "<label>" + html(x.label) + "</label><input name=\"" + html(x.key) + "\" autocomplete=off spellcheck=false" +
         (secret ? String(" placeholder=\"") + html(T_("unchanged when empty")) + "\"" : " value=\"" + html(appset::get(F.id.c_str(), x.key.c_str())) + "\"") + ">";
  }
  return String("<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width\"><title>EspOS</title>"
                "<style>body{font:16px system-ui,sans-serif;margin:20px auto;max-width:460px;padding:0 16px}"
                "label{display:block;margin:14px 0 4px;color:#555}input{width:100%;font:15px ui-monospace,monospace;"
                "padding:9px;box-sizing:border-box;border:1px solid #bbb;border-radius:8px}button{margin-top:18px;padding:11px 20px;"
                "font-size:16px;border:0;border-radius:10px;background:#0a84ff;color:#fff}p{color:#333}.n{color:#c2410c}</style><h2>") +
         html(F.id) + "</h2>" + (note.length() ? "<p class=n>" + html(note) + "</p>" : String()) + "<p>" +
         html(T_("Paste the values here. They go straight to the device on your network.")) + "</p><form method=post action=/save>" + f +
         "<label>PIN</label><input name=pin inputmode=numeric autocomplete=off required><button>" + html(T_("Save to the device")) +
         "</button></form><script>if(location.hash)document.forms[0].pin.value=location.hash.slice(1)</script>";
}

// A post, on the server's task: parsed here, applied by the UI loop (appset is UI-loop only), answered with its result.
String onPost(const String& body) {
  if (F.done) return page(T_("This page was used already. Open it again on the device."));
  std::vector<std::pair<String, String>> kv;
  String pin;
  for (int a = 0; a < (int)body.length();) {
    int e = body.indexOf('&', a);
    if (e < 0) e = body.length();
    const String part = body.substring(a, e);
    const int q = part.indexOf('=');
    if (q > 0) {
      const String k = urldecode(part.substring(0, q)), v = urldecode(part.substring(q + 1));
      if (k == "pin") pin = v;
      else kv.push_back({k, v});
    }
    a = e + 1;
  }
  // The PIN, compared in constant time; 5 wrong ones end the page.
  uint8_t diff = pin.length() != F.pin.length();
  for (unsigned i = 0; i < F.pin.length() && i < pin.length(); i++) diff |= pin[i] ^ F.pin[i];
  if (diff) {
    if (++F.wrong >= 5) F.done = true;
    return page(F.done ? T_("Too many wrong PINs. Open the page again on the device.") : T_("Wrong PIN."));
  }
  F.posted = kv;
  F.state = 1;
  for (int i = 0; i < 500 && F.state != 2; i++) {  // the UI loop applies it (5 s at most)
#ifndef SIM
    delay(10);
#else
    usleep(10000);  // the simulator's delay() does not wait
#endif
  }
  const String a = F.state == 2 ? F.answer : String(T_("The device is busy. Try again."));
  F.state = 0;
  return page(a);
}

// UI loop: apply a post (values and secrets through appset's checks), show the result on the screen.
void apply(lv_timer_t* t) {
  if (!F.scr || !lv_obj_is_valid(F.scr)) return;
  if (F.state == 1) {
    String bad;
    int n = 0;
    // Addresses and texts first, secrets after: a new address clears the secrets bound to it (appset), so keys typed
    // with it must land after it, whatever order the browser sent the fields in.
    for (int pass = 0; pass < 2; pass++)
    for (auto& kv : F.posted) {
      const appset::Field* f = nullptr;
      for (const appset::Field& x : F.fields)
        if (x.key == kv.first) f = &x;
      if (!f || (f->type == "secret") != (pass == 1)) continue;
      if (f->type == "secret" && !kv.second.length()) continue;  // an empty secret field: unchanged
      String why;
      String v = kv.second;
      v.trim();
      const bool ok = f->type == "secret" ? appset::setSecret(F.id.c_str(), f->key.c_str(), v, why)
                                          : appset::set(F.id.c_str(), f->key.c_str(), v, why);
      if (ok) n++;
      else bad += (bad.length() ? "; " : "") + f->label + ": " + why;
      kv.second = String();  // a secret's value: not left in memory longer than needed
    }
    F.posted.clear();
    trace::add("lanform: %s, %d fields saved%s", F.id.c_str(), n, bad.length() ? ", some refused" : "");
    F.answer = bad.length() ? bad : i18n::fmt(N_("Saved on the device ({0}). You can close this page."), String(n).c_str());
    if (!bad.length()) F.done = true;
    if (F.status) setText(F.status, F.answer.c_str());
    F.state = 2;
  }
  if (F.done || millis() - F.opened > OPEN_MS) {
    lv_timer_set_period(t, 60000);  // nothing more to do; the screen stays until it is closed
  }
}

#ifndef SIM
esp_err_t onGet(httpd_req_t* r) {
  httpd_resp_set_type(r, "text/html; charset=utf-8");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  const String p = page(F.done ? String(T_("This page was used already. Open it again on the device.")) : String());
  return httpd_resp_send(r, p.c_str(), p.length());
}

esp_err_t onSave(httpd_req_t* r) {
  char buf[2048];
  const int n = r->content_len < sizeof buf ? httpd_req_recv(r, buf, r->content_len) : -1;
  String body;
  if (n > 0) body.concat(buf, n);
  memset(buf, 0, sizeof buf);
  const String p = n > 0 ? onPost(body) : page(T_("Too long."));
  httpd_resp_set_type(r, "text/html; charset=utf-8");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, p.c_str(), p.length());
}

void startServer() {
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.server_port = PORT;
  c.ctrl_port = 32770;  // remote.cpp's server has the default one
  c.max_uri_handlers = 2;
  c.stack_size = 6144;
  if (httpd_start(&server, &c) != ESP_OK) return (void)(server = nullptr);
  httpd_uri_t g = {"/", HTTP_GET, onGet, nullptr}, s = {"/save", HTTP_POST, onSave, nullptr};
  httpd_register_uri_handler(server, &g);
  httpd_register_uri_handler(server, &s);
}

void stopServer() {
  if (server) httpd_stop(server), server = nullptr;
}
#else
// The simulator: the same page on 127.0.0.1:8080 from a small thread (tests post to it with curl).
void serve() {
  while (simRun) {
    sockaddr_in a{};
    socklen_t al = sizeof a;
    const int c = accept(listenFd, (sockaddr*)&a, &al);
    if (c < 0) continue;
    String req;
    char b[2048];
    int n;
    while ((n = recv(c, b, sizeof b, 0)) > 0) {
      req.concat(b, n);
      const int he = req.indexOf("\r\n\r\n");
      if (he < 0) continue;
      const int cl = req.indexOf("Content-Length: ");
      const int len = cl > 0 ? req.substring(cl + 16).toInt() : 0;
      if ((int)req.length() >= he + 4 + len) break;
    }
    const bool post = req.startsWith("POST /save");
    const String body = req.substring(req.indexOf("\r\n\r\n") + 4);
    const String p = post ? onPost(body) : page(F.done ? String(T_("This page was used already. Open it again on the device.")) : String());
    const String out = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
                       String(p.length()) + "\r\n\r\n" + p;
    send(c, out.c_str(), out.length(), 0);
    close(c);
  }
}

void startServer() {
  listenFd = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  const int port = getenv("SIM_LANFORM_PORT") ? atoi(getenv("SIM_LANFORM_PORT")) : PORT;  // tests: a free port
  a.sin_family = AF_INET, a.sin_port = htons(port), a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listenFd, (sockaddr*)&a, sizeof a) || listen(listenFd, 2)) return (void)(close(listenFd), listenFd = -1);
  simRun = true;
  std::thread(serve).detach();
}

void stopServer() {
  simRun = false;
  if (listenFd >= 0) shutdown(listenFd, SHUT_RDWR), close(listenFd), listenFd = -1;
}
#endif

}  // namespace

void open(const char* appId) {
  if (F.scr && lv_obj_is_valid(F.scr)) return;
  F.id = appId, F.wrong = 0, F.done = false, F.state = 0, F.opened = millis();
  F.fields.clear();
  for (const appset::Field& f : appset::fields(appId))
    if (f.type == "url" || f.type == "string" || (f.type == "secret" && f.where == "device")) F.fields.push_back(f);
  char pin[8];
#ifndef SIM
  snprintf(pin, sizeof pin, "%06lu", (unsigned long)(esp_random() % 1000000));
#else
  snprintf(pin, sizeof pin, "%06lu", (unsigned long)(arc4random() % 1000000));
#endif
  F.pin = pin;
#ifdef SIM
  if (getenv("SIM_LANFORM_PIN")) F.pin = getenv("SIM_LANFORM_PIN");  // tests (tools/nettest/run.sh lanform) post with it
#endif
  const String ip = net::ip();
  const String url = "http://" + (ip.length() ? ip : String("192.168.4.1")) + ":" + PORT;
  lv_obj_t* body;
  F.scr = os::screen(T_("From a computer"), &body);
  hint(body, T_("On a computer or phone on this Wi-Fi, scan the code or open the address, then type the PIN."));
  lv_obj_t* row = clear(body);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(row, 12, 0);
  qr(row, (url + "/#" + F.pin).c_str(), 140);
  lv_obj_t* col = clear(row);
  lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  label(col, url.c_str(), 15, true, pal().fg);
  label(col, T_("PIN"), 13, false, pal().dim);
  label(col, (F.pin.substring(0, 3) + " " + F.pin.substring(3)).c_str(), 24, true, pal().fg);
  F.status = label(body, "", 15, true, pal().fg);
  lv_obj_set_width(F.status, lv_pct(100));  // the answer wraps (a refused field says why)
  lv_label_set_long_mode(F.status, LV_LABEL_LONG_WRAP);
  hint(body, T_("The page closes after one save or in 10 minutes. Nothing goes through espos.dev."));
  startServer();
  lv_timer_t* t = lv_timer_create(apply, 50, nullptr);
  lv_obj_add_event_cb(F.scr, [](lv_event_t* e) {
    lv_timer_delete((lv_timer_t*)lv_event_get_user_data(e));
    stopServer();
    F.scr = F.status = nullptr;
    F.pin = String();
    F.posted.clear();
  }, LV_EVENT_DELETE, t);
  trace::add("lanform: open for %s on %s", appId, url.c_str());
  os::open(F.scr);
}

}  // namespace lanform
