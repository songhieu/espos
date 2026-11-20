#pragma GCC optimize("Os")  // cold code: built small (0.5.7 flash diet; the app slot is full)
// Remote control: remote.h, docs/remote.md. Two ways in, both while Settings › Điều khiển từ xa is on:
// - Over the internet, through the EspOS server (the web EspOS's device page): the device calls
//   POST /v1/device/remote (desk::remoteSend, the one kept TLS connection) every few seconds; while the owner's
//   browser watches, back to back with a frame each time the screen changed (160x240, sharp 320x480 once it stands
//   still). The reply brings the gestures to play (tap, long, drag, key), played with touch::inject's timing.
// - In the home network: esp_http_server on port 80 (one task on core 0) serves a page, frames and touches
//   (touch::feedScreen), with the PIN.
// A frame is the panel's mirror (lcd::frame) copied, then JPEG-encoded by Espressif's encoder (lib/esp_new_jpeg),
// all in PSRAM. Home and Back are done by poll() on the LVGL thread.
#include "remote.h"

#include <Preferences.h>

#include <algorithm>
#include <deque>

#include "mbedtls/base64.h"

#include "desk.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_jpeg_enc.h"
#include "lcd.h"
#include "net.h"
#include "os.h"
#include "touch.h"
#include "trace.h"

namespace remote {
namespace {

Preferences prefs;
bool on = false;
String pinStr;
httpd_handle_t server = nullptr;
volatile uint32_t lastWatch = 0;
volatile int pendingKey = 0;  // 1 Home, 2 Back: done by poll()
uint32_t wrong = 0, lockedUntil = 0;
bool mirrored = false;

// A JPEG encoder and its buffers: one for the home network's server task, one for the internet loop (UI thread).
struct Encoder {
  jpeg_enc_handle_t h = nullptr;
  int w = 0, h2 = 0, quality = 70;
  uint8_t *in = nullptr, *out = nullptr;
  int cap = 0;
  // f: w x h RGB565 (stride sw); step 2 halves it. len: the JPEG's bytes in out.
  bool encode(const uint16_t* f, int sw, int ww, int hh, int step, int q, int& len) {
    if (!h || ww != w || hh != h2 || q != quality) {
      close();
      in = (uint8_t*)heap_caps_aligned_alloc(16, ww * hh * 2, MALLOC_CAP_SPIRAM);
      cap = std::max(16 * 1024, ww * hh);  // a JPEG of the screen is well under 1 byte a pixel
      out = (uint8_t*)heap_caps_aligned_alloc(16, cap, MALLOC_CAP_SPIRAM);
      jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
      cfg.width = ww, cfg.height = hh;
      cfg.src_type = JPEG_PIXEL_FORMAT_RGB565_LE;
      cfg.subsampling = JPEG_SUBSAMPLE_420;
      cfg.quality = q;
      cfg.task_enable = false;
      if (!in || !out || jpeg_enc_open(&cfg, &h) != JPEG_ERR_OK) {
        h = nullptr;
        close();
        trace::add("remote: no JPEG encoder %dx%d", ww, hh);
        return false;
      }
      w = ww, h2 = hh, quality = q;
    }
    uint16_t* d = (uint16_t*)in;  // one moment of the screen (the panel keeps changing)
    if (step == 1) memcpy(d, f, ww * hh * 2);
    else
      for (int y = 0; y < hh; y++) {
        const uint16_t* s = f + y * step * sw;
        for (int x = 0; x < ww; x++) d[y * ww + x] = s[x * step];
      }
    return jpeg_enc_process(h, in, ww * hh * 2, out, cap, &len) == JPEG_ERR_OK;
  }
  void close() {
    if (h) jpeg_enc_close(h), h = nullptr;
    heap_caps_free(in), heap_caps_free(out);
    in = out = nullptr, w = h2 = cap = 0;
  }
};
Encoder lanEnc, cloudEnc;

// ---- over the internet
bool cloudWatch = false, cloudBusy = false, sentSharp = false;
uint32_t nextCall = 0, lastSum = 0, stillSince = 0;
int lastAck = 0, sentRot = -1;
std::deque<String> gestures;
uint32_t gestureFree = 0;

const char PAGE[] = R"HTML(<!doctype html><html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>EspOS · Điều khiển từ xa</title>
<style>
body{margin:0;background:#101216;color:#cfd3dc;font:14px -apple-system,"Segoe UI",Roboto,sans-serif;display:flex;
flex-direction:column;align-items:center;min-height:100vh}
.bar{width:100%;max-width:520px;display:flex;justify-content:space-between;padding:12px 16px;box-sizing:border-box;font-size:13px}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#777;margin-right:6px}.on .dot{background:#22a06b}
canvas{border:8px solid #000;border-radius:14px;touch-action:none;max-height:calc(100vh - 140px);max-width:calc(100vw - 32px);
background:#000;cursor:pointer}
.keys{display:flex;gap:8px;margin:12px}
button{border:0;border-radius:10px;padding:9px 14px;background:#262a33;color:#e6e8ee;font:inherit;cursor:pointer}
#pin{display:none;flex-direction:column;align-items:center;gap:12px;margin-top:18vh;padding:0 16px;text-align:center}
#pin input{font:24px ui-monospace,monospace;letter-spacing:.2em;width:8ch;text-align:center;padding:8px;border-radius:10px;border:0}
</style></head><body>
<div class="bar" id="bar"><span><i class="dot"></i><span id="st">Đang nối</span></span><span id="fps"></span></div>
<div id="pin"><div>Nhập mã PIN trên máy<br>(Cài đặt › Điều khiển từ xa)</div><input id="pi" inputmode="numeric" maxlength="6">
<button id="pg">Mở</button></div>
<canvas id="c" width="320" height="480"></canvas>
<div class="keys" id="keys"><button id="bk">‹ Quay lại</button><button id="hm">Home</button><button id="sh">Chụp</button></div>
<script>
const $=i=>document.getElementById(i),c=$('c'),g=c.getContext('2d');
let pin=(location.hash.match(/pin=(\d{6})/)||[])[1]||sessionStorage.getItem('pin')||'';
if(location.hash)history.replaceState(null,'',location.pathname);
let n=0,t0=performance.now(),live=false,q=Promise.resolve();
function ask(m){live=false;$('pin').style.display='flex';c.style.display=$('keys').style.display='none';
 $('st').textContent=m||'Cần mã PIN';$('bar').className='bar';}
$('pg').onclick=()=>{pin=$('pi').value.replace(/\D/g,'');$('pin').style.display='none';c.style.display='';
 $('keys').style.display='flex';loop();};
$('pi').onkeydown=e=>{if(e.key=='Enter')$('pg').onclick();};
async function frame(){
 const r=await fetch('/frame',{headers:{'X-Pin':pin},cache:'no-store'});
 if(r.status==401){ask('Mã PIN chưa đúng');return false;}
 if(r.status==429){ask('Sai mã nhiều lần, chờ một phút');return false;}
 if(!r.ok)throw 0;
 const b=await createImageBitmap(await r.blob());
 if(c.width!=b.width||c.height!=b.height){c.width=b.width;c.height=b.height;}
 g.drawImage(b,0,0);n++;return true;}
async function loop(){if(live)return;live=true;sessionStorage.setItem('pin',pin);
 while(live){try{if(!await frame())break;$('bar').className='bar on';$('st').textContent='Đang xem';}
  catch(e){$('bar').className='bar';$('st').textContent='Mất kết nối, đang thử lại';await new Promise(r=>setTimeout(r,1000));}
  const t=performance.now();if(t-t0>1000){$('fps').textContent=Math.round(n*1000/(t-t0))+' hình/giây';n=0;t0=t;}}}
function post(p,b){q=q.then(()=>fetch(p,{method:'POST',headers:{'X-Pin':pin},body:b}).catch(()=>{}));}
let down=false,last=0;
function pt(e){const r=c.getBoundingClientRect();
 return[Math.round((e.clientX-r.left-8)*c.width/(r.width-16)),Math.round((e.clientY-r.top-8)*c.height/(r.height-16))];}
c.onpointerdown=e=>{c.setPointerCapture(e.pointerId);down=true;const[x,y]=pt(e);post('/touch','d '+x+' '+y);last=performance.now();};
c.onpointermove=e=>{if(!down)return;const t=performance.now();if(t-last<30)return;last=t;const[x,y]=pt(e);post('/touch','d '+x+' '+y);};
c.onpointerup=c.onpointercancel=e=>{if(!down)return;down=false;const[x,y]=pt(e);post('/touch','u '+x+' '+y);};
$('bk').onclick=()=>post('/key','back');$('hm').onclick=()=>post('/key','home');
$('sh').onclick=()=>{const a=document.createElement('a');a.download='espos.png';a.href=c.toDataURL();a.click();};
pin?loop():ask();
</script></body></html>)HTML";

// Every request but the page needs the PIN; ten wrong ones lock the door for a minute.
bool authed(httpd_req_t* r) {
  if (millis() < lockedUntil) {
    httpd_resp_set_status(r, "429 Too Many Requests");
    httpd_resp_sendstr(r, "wait");
    return false;
  }
  char v[12] = "";
  if (httpd_req_get_hdr_value_str(r, "X-Pin", v, sizeof v) == ESP_OK && pinStr.length() == 6 && pinStr == v) {
    wrong = 0;
    return true;
  }
  if (++wrong >= 10) wrong = 0, lockedUntil = millis() + 60000, trace::add("remote: 10 wrong PINs, locked 1 min");
  httpd_resp_set_status(r, "401 Unauthorized");
  httpd_resp_sendstr(r, "PIN");
  return false;
}

esp_err_t onPage(httpd_req_t* r) {
  httpd_resp_set_type(r, "text/html; charset=utf-8");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, PAGE, sizeof PAGE - 1);
}

esp_err_t onFrame(httpd_req_t* r) {
  if (!authed(r)) return ESP_OK;
  lastWatch = millis();
  int w = 0, h = 0, len = 0;
  const uint16_t* f = lcd::frame(w, h);
  for (int k = 0; !f && k < 40; k++) vTaskDelay(pdMS_TO_TICKS(10)), f = lcd::frame(w, h);  // poll() turns it on
  if (!f || !lanEnc.encode(f, w, w, h, 1, 70, len)) {
    httpd_resp_set_status(r, "503 Service Unavailable");
    return httpd_resp_sendstr(r, "busy");
  }
  httpd_resp_set_type(r, "image/jpeg");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, (const char*)lanEnc.out, len);
}

int body(httpd_req_t* r, char* b, int cap) {
  const int n = httpd_req_recv(r, b, std::min<int>(cap - 1, r->content_len));
  b[n > 0 ? n : 0] = 0;
  return n;
}

esp_err_t onTouch(httpd_req_t* r) {
  if (!authed(r)) return ESP_OK;
  char b[32], k = 0;
  int x, y;
  body(r, b, sizeof b);
  if (sscanf(b, "%c %d %d", &k, &x, &y) == 3 && (k == 'd' || k == 'u')) touch::feedScreen(x, y, k == 'd');
  return httpd_resp_sendstr(r, "ok");
}

esp_err_t onKey(httpd_req_t* r) {
  if (!authed(r)) return ESP_OK;
  char b[16];
  body(r, b, sizeof b);
  pendingKey = !strcmp(b, "home") ? 1 : !strcmp(b, "back") ? 2 : 0;
  return httpd_resp_sendstr(r, "ok");
}

void start() {
  if (server) return;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 80;
  cfg.max_open_sockets = 5;
  cfg.lru_purge_enable = true;
  cfg.stack_size = 6144;
  cfg.core_id = 0;
  cfg.recv_wait_timeout = 5;
  cfg.send_wait_timeout = 5;
  if (httpd_start(&server, &cfg) != ESP_OK) {
    server = nullptr;
    trace::add("remote: the server did not start");
    return;
  }
  const httpd_uri_t uris[] = {{"/", HTTP_GET, onPage, nullptr},
                              {"/frame", HTTP_GET, onFrame, nullptr},
                              {"/touch", HTTP_POST, onTouch, nullptr},
                              {"/key", HTTP_POST, onKey, nullptr}};
  for (const httpd_uri_t& u : uris) httpd_register_uri_handler(server, &u);
  trace::add("remote: on, port 80, heap %u KB", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
}

void stop() {
  if (!server) return;
  httpd_stop(server);  // waits for a request in progress
  server = nullptr;
  lanEnc.close();
  trace::add("remote: off");
}

// ---- over the internet
// A cheap sum of the screen (every 4th pixel of every 4th row) to send a frame only when it changed.
uint32_t sumOf(const uint16_t* f, int w, int h) {
  uint32_t s = 2166136261u;
  for (int y = 0; y < h; y += 4)
    for (int x = 0; x < w; x += 4) s = (s ^ f[y * w + x]) * 16777619u;
  return s;
}

void cloudReply(int status, JsonDocument& d) {
  cloudBusy = false;
  if (status != 200) {
    cloudWatch = false;
    nextCall = millis() + (status < 0 ? 3000 : 10000);
    return;
  }
  const bool was = cloudWatch;
  cloudWatch = d["watch"] | false;
  if (cloudWatch && !was) sentSharp = false, lastSum = 0, trace::add("remote: watched over the internet");
  for (JsonObjectConst e : d["events"].as<JsonArrayConst>()) {
    const int id = e["id"] | 0;
    if (id <= lastAck) continue;
    lastAck = id;
    gestures.push_back(e["ev"] | "");
  }
  nextCall = millis() + (cloudWatch ? 30 : 4000);
}

// One gesture at a time, each with its own timing (touch::inject moves the finger over the time given).
void playGestures() {
  if (gestures.empty() || (int32_t)(millis() - gestureFree) < 0) return;
  const String g = gestures.front();
  gestures.pop_front();
  int a, b, c, d, ms, na, nb, nc, nd;
  char key[8] = "";
  if (sscanf(g.c_str(), "tap %d %d", &a, &b) == 2) {
    touch::toNative(a, b, na, nb);
    touch::inject(na, nb, na, nb, 80), gestureFree = millis() + 200;
  } else if (sscanf(g.c_str(), "long %d %d %d", &a, &b, &ms) == 3) {
    touch::toNative(a, b, na, nb);
    touch::inject(na, nb, na, nb, std::min(ms, 3000)), gestureFree = millis() + std::min(ms, 3000) + 150;
  } else if (sscanf(g.c_str(), "drag %d %d %d %d %d", &a, &b, &c, &d, &ms) == 5) {
    touch::toNative(a, b, na, nb);
    touch::toNative(c, d, nc, nd);
    ms = std::max(60, std::min(ms, 2000));
    touch::inject(na, nb, nc, nd, ms), gestureFree = millis() + ms + 150;
  } else if (sscanf(g.c_str(), "key %7s", key) == 1) {
    !strcmp(key, "home") ? os::goHome() : os::back();
    gestureFree = millis() + 300;
  }
}

// The next call to the server: the switch, the gestures done, and a frame when the screen changed (watched only).
// The body is put together by hand: base64 needs no JSON escaping, and a frame stays out of a JsonDocument.
void cloudPoll() {
  playGestures();
  if (!on || cloudBusy || desk::link() != desk::Link::Linked || (int32_t)(millis() - nextCall) < 0) return;
  String body = "{\"on\":true,\"ack\":" + String(lastAck);
  int w = 0, h = 0;
  const uint16_t* f = cloudWatch ? lcd::frame(w, h) : nullptr;
  if (f) {
    const uint32_t s = sumOf(f, w, h);
    const int rot = w > h;
    const bool moving = s != lastSum || rot != sentRot;
    if (moving) stillSince = millis(), sentSharp = false;
    // Moving: a small one (half the pixels each way, 5-8 KB) keeps up with the board's ~25 KB/s up; standing still
    // for 0.6 s: the sharp one, once.
    const bool sharp = !moving && !sentSharp && millis() - stillSince > 600;
    int len = 0;
    if ((moving || sharp) &&
        cloudEnc.encode(f, w, sharp ? w : w / 2, sharp ? h : h / 2, sharp ? 1 : 2, sharp ? 50 : 60, len)) {
      size_t n = 0;
      mbedtls_base64_encode(nullptr, 0, &n, cloudEnc.out, len);
      char* b64 = (char*)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM);
      if (b64 && mbedtls_base64_encode((unsigned char*)b64, n + 1, &n, cloudEnc.out, len) == 0) {
        b64[n] = 0;
        body.reserve(body.length() + n + 48);
        body += ",\"w\":" + String(w) + ",\"h\":" + String(h) + ",\"frame\":\"";  // the screen's own size
        body += b64;
        body += "\"";
        lastSum = s, sentRot = rot;
        if (sharp) sentSharp = true;
      }
      heap_caps_free(b64);
    }
  }
  body += "}";
  if (desk::remoteSend(body, cloudReply)) cloudBusy = true;
  else nextCall = millis() + 300;
}

}  // namespace

void begin() {
  prefs.begin("remote");
  on = prefs.getBool("on", false);
  pinStr = prefs.getString("pin", "");
  if (on && pinStr.length() == 6) start();
  else on = false;
}

void poll() {
  if (const int k = pendingKey) {
    pendingKey = 0;
    k == 1 ? os::goHome() : os::back();
  }
  cloudPoll();
  const bool want = (server && watching()) || (on && cloudWatch);
  if (want != mirrored) mirrored = want, lcd::mirror(want);
}

bool enabled() { return on; }

void setEnabled(bool v) {
  if (v == on) return;
  on = v;
  if (on) {
    char b[8];
    snprintf(b, sizeof b, "%06lu", (unsigned long)(esp_random() % 1000000));
    pinStr = b;
    start();
  } else {
    stop();
    pinStr = "";
    if (mirrored) mirrored = false, lcd::mirror(false);
    cloudWatch = false, gestures.clear(), cloudEnc.close();
    // The server hears it at once (the browser shows it is off).
    if (!cloudBusy && desk::remoteSend("{\"on\":false}", cloudReply)) cloudBusy = true;
  }
  nextCall = 0;
  prefs.putBool("on", on);
  prefs.putString("pin", pinStr);
  if (desk::link() == desk::Link::Linked) desk::syncNow();  // the web learns where the page is (or that it is gone)
}

String pin() { return on ? pinStr : String(); }

String url() {
  if (!on) return String();
#ifdef ESPOS_QEMU
  return "http://127.0.0.1:8080";  // QEMU forwards this computer's port 8080 to the device's port 80 (run.sh)
#else
  const String ip = net::ip();
  return ip.length() ? "http://" + ip : String();
#endif
}

bool watching() { return lastWatch && millis() - lastWatch < 3000; }

}  // namespace remote
