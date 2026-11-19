#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
#include "devtools.h"
#include "tank/tank.h"

#include <Arduino.h>
#include "esp_memory_utils.h"
#include <algorithm>
#include <WiFi.h>
#ifndef SIM
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
extern const uint8_t x509_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_bundle_end[] asm("_binary_x509_crt_bundle_end");
#endif
#include <lvgl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "desk.h"
#include "ota.h"
#include "sound.h"
#include "net.h"
#include "fonts.h"
#include "lcd.h"
#include "touch.h"
#include "trace.h"
#include "apps.h"
#include "home.h"
#include "js/jsapp.h"
#include "remote.h"
#include "js/jsrt.h"
#include "os.h"
#include "run/run.h"
#include "wake/wake.h"
#ifndef NO_PET
#include "pet/talk.h"
#endif

namespace devtools {
namespace {

// Cable WiFi commands of the web installer (the X4's format), answered from poll():
// CMD:WIFISCAN -> "WIFI <rssi> <ssid>"... "WIFISCAN_END"; CMD:WIFITEST ssid\tpass ->
// WIFITEST_OK | WIFITEST_FAIL status=<1 not found, 4 refused>; CMD:WIFIADD ssid\tpass -> WIFIADD_OK.
uint32_t scanAsked = 0;       // millis() of a pending WIFISCAN, 0 = none
uint32_t testAsked = 0;       // millis() of a pending WIFITEST
uint32_t testChanges = 0;
String testSsid;

void cableWifi() {
  if (scanAsked && !net::scanning() && net::resultsAt() >= scanAsked) {
    for (const net::Ap& a : net::results()) Serial.printf("WIFI %d %s\n", a.rssi, a.ssid.c_str());
    Serial.println("WIFISCAN_END");
    scanAsked = 0;
  }
  if (testAsked && net::changes() != testChanges) {
    testChanges = net::changes();
    if (net::state() == net::State::Connected && net::ssid() == testSsid) {
      Serial.println("WIFITEST_OK");
      testAsked = 0;
    } else if (net::state() == net::State::Idle) {
      const int r = net::lastReason();
      Serial.printf("WIFITEST_FAIL status=%d\n", r == WIFI_REASON_NO_AP_FOUND ? 1 : 4);
      testAsked = 0;
      net::resumeAuto();  // back to the network it had
    }
  }
  if (testAsked && millis() - testAsked > 30000) {
    Serial.println("WIFITEST_FAIL status=4");
    testAsked = 0;
    net::resumeAuto();
  }
}

bool splitTab(const char* in, String& a, String& b) {
  const char* tab = strchr(in, '\t');
  if (!tab) return false;
  a = String(in).substring(0, tab - in);
  b = tab + 1;
  return a.length() > 0;
}

// Provisioning lines carry settings and widget layouts: up to 8 KB (in PSRAM).
constexpr size_t LINE_CAP = 8192;
char* line = nullptr;
size_t len = 0;

void sendBuf(const char* tag, lv_draw_buf_t* b) {
  if (!b) {
    Serial.printf("%s 0 0\n", tag);
    return;
  }
  const int w = b->header.w, h = b->header.h;
  const int bpp = lv_color_format_get_size((lv_color_format_t)b->header.cf);
  Serial.printf("%s %d %d %d\n", tag, w, h, bpp);
  for (int y = 0; y < h; y++) Serial.write(b->data + y * b->header.stride, w * bpp);
  Serial.print("\n");
  lv_draw_buf_destroy(b);
}

// Screen coordinates of the current orientation -> native panel coordinates (inverse of LVGL's
// pointer rotation).
void toNative(int x, int y, int& nx, int& ny) {
  switch (lv_display_get_rotation(nullptr)) {
    case LV_DISPLAY_ROTATION_90: nx = y, ny = LCD_H - 1 - x; break;
    case LV_DISPLAY_ROTATION_180: nx = LCD_W - 1 - x, ny = LCD_H - 1 - y; break;
    case LV_DISPLAY_ROTATION_270: nx = LCD_W - 1 - y, ny = x; break;
    default: nx = x, ny = y;
  }
}

void run(char* cmd) {
  static constexpr char kHello[] = "ESPOS:HELLO ";          // + the host's epoch ms
  static constexpr char kProvision[] = "ESPOS:PROVISION ";  // + the link JSON
  if (!strncmp(cmd, kHello, sizeof(kHello) - 1)) {
    const long long ms = atoll(cmd + sizeof(kHello) - 1);
    if (ms > 1700000000000LL) {
      timeval tv = {(time_t)(ms / 1000), (suseconds_t)((ms % 1000) * 1000)};
      settimeofday(&tv, nullptr);
    }
    Serial.printf("ESPOS:READY {\"fw\":\"%s\",\"kind\":\"s3\",\"board\":\"%s\",\"linked\":%s}\n", FW_VERSION, board::name(),
                  desk::link() == desk::Link::Linked ? "true" : "false");
  } else if (!strncmp(cmd, "SOUND ", 6)) {
    if (!strcmp(cmd + 6, "test")) sound::test();
    else if (!strcmp(cmd + 6, "chime")) sound::chime();
    else sound::click();
  } else if (!strcmp(cmd, "CMD:WIFISCAN")) {
    scanAsked = millis() | 1;
    net::scan();
  } else if (!strncmp(cmd, "CMD:WIFITEST ", 13)) {
    String ssid, pass;
    if (!splitTab(cmd + 13, ssid, pass)) {
      Serial.println("WIFITEST_FAIL status=1");
    } else {
      testSsid = ssid;
      testAsked = millis() | 1;
      net::connect(ssid, pass);  // saved once it gets an address
      testChanges = net::changes();
    }
  } else if (!strncmp(cmd, "CMD:WIFIADD ", 12)) {
    String ssid, pass;
    if (splitTab(cmd + 12, ssid, pass)) {
      net::addSaved(ssid, pass);
      Serial.println("WIFIADD_OK");
    } else {
      Serial.println("WIFIADD_ERR");
    }
  } else if (!strncmp(cmd, kProvision, sizeof(kProvision) - 1)) {
    String err;
    if (desk::provision(cmd + sizeof(kProvision) - 1, err)) Serial.println("ESPOS:OK");
    else Serial.printf("ESPOS:ERR %s\n", err.c_str());
  } else if (!strcmp(cmd, "SHOT")) {
    sendBuf("SHOT", lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565));
    sendBuf("TOP", lv_snapshot_take(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888));
    Serial.println("ENDSHOT");
  } else if (!strncmp(cmd, "TAP ", 4)) {
    int x, y, nx, ny;
    if (sscanf(cmd + 4, "%d %d", &x, &y) == 2) toNative(x, y, nx, ny), touch::inject(nx, ny, nx, ny, 80);
  } else if (!strncmp(cmd, "REMOTE", 6)) {  // REMOTE [1|0]: remote control on/off (Settings › Điều khiển từ xa)
    if (cmd[6] == ' ') remote::setEnabled(atoi(cmd + 7) != 0);
    Serial.printf("REMOTE %s %s pin %s\n", remote::enabled() ? "on" : "off", remote::url().c_str(), remote::pin().c_str());
  } else if (!strncmp(cmd, "TOUCH ", 6)) {  // TOUCH d|u X Y: a finger down (or moved) / lifted, as it happens
    char k;
    int x, y, nx, ny;
    if (sscanf(cmd + 6, "%c %d %d", &k, &x, &y) == 3) toNative(x, y, nx, ny), touch::feed(nx, ny, k == 'd');
  } else if (!strncmp(cmd, "DRAG ", 5)) {
    int x0, y0, x1, y1, ms, a, b, c, d;
    if (sscanf(cmd + 5, "%d %d %d %d %d", &x0, &y0, &x1, &y1, &ms) == 5) {
      toNative(x0, y0, a, b);
      toNative(x1, y1, c, d);
      touch::inject(a, b, c, d, ms);
    }
  } else if (!strncmp(cmd, "BENCH ", 6)) {
    // Render the whole screen 10 times with a variant applied: 0 as is, 1 no gradients,
    // 2 square corners, 3 labels hidden, 4 all of them.
    const int v = atoi(cmd + 6);
    lv_obj_t* scr = lv_screen_active();
    struct Walk {
      static void apply(lv_obj_t* o, int v) {
        if (v == 1 || v == 4) lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_NONE, 0);
        if (v == 2 || v == 4) lv_obj_set_style_radius(o, 0, 0);
        if ((v == 3 || v == 4) && lv_obj_check_type(o, &lv_label_class)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        if (v == 5 && lv_obj_check_type(o, &lv_label_class) &&
            lv_font_get_line_height(lv_obj_get_style_text_font(o, 0)) > 30)
          lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        if (v == 6 && lv_obj_check_type(o, &lv_label_class) &&
            lv_font_get_line_height(lv_obj_get_style_text_font(o, 0)) <= 30)
          lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        if (v == 7 && lv_obj_check_type(o, &lv_image_class)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        if (v == 8 && lv_obj_get_style_bg_opa(o, 0) > 0) lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        if (v == 9 && lv_obj_check_type(o, &lv_image_class) && lv_obj_get_parent(o) == lv_screen_active())
          lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);  // the wallpaper only
        if (v == 10) lv_obj_set_style_border_width(o, 0, 0);
        for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) apply(lv_obj_get_child(o, i), v);
      }
    };
    if (v) Walk::apply(scr, v);
    uint32_t t0 = micros();
    for (int i = 0; i < 10; i++) {
      lv_obj_invalidate(scr);
      lv_refr_now(nullptr);
    }
    Serial.printf("BENCH %d: %lu us per full frame\n", v, (micros() - t0) / 10);
  } else if (!strncmp(cmd, "TEXTBENCH", 9)) {
    // Pure CPU cost of drawing ~400 glyphs into an offscreen buffer, per font.
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, 320, 240);
    lv_obj_t* l = lv_label_create(scr);
    lv_obj_set_width(l, 300);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, "Hôm nay thời tiết Hà Nội mây rải rác, nhiệt độ 19 đến 26 độ, khả năng mưa 20%. "
                         "Họp nhóm tuần lúc 09:00, đón con lúc 14:30, ăn tối nhà ngoại lúc 19:00. "
                         "Giá vàng SJC 154,5 triệu đồng một lượng. Chỉ số VN-Index tăng 0,8%. "
                         "Tết Đinh Mùi còn 83 ngày nữa, chúc cả nhà một tuần làm việc hiệu quả.");
    const lv_font_t* fs[] = {fonts::get(14), &lv_font_montserrat_14, fonts::get(14, true)};
    const char* names[] = {"ubuntu14", "montserrat14", "ubuntu14b"};
    for (int k = 0; k < 3; k++) {
      lv_obj_set_style_text_font(l, fs[k], 0);
      lv_obj_update_layout(scr);
      uint32_t t0 = micros();
      for (int i = 0; i < 5; i++) {
        lv_draw_buf_t* b = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB565);
        lv_draw_buf_destroy(b);
      }
      Serial.printf("TEXTBENCH %s: %lu us per draw (label + 320x240 bg)\n", names[k], (micros() - t0) / 5);
    }
    lv_label_set_text(l, "");
    uint32_t t0 = micros();
    for (int i = 0; i < 5; i++) lv_draw_buf_destroy(lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB565));
    Serial.printf("TEXTBENCH empty: %lu us\n", (micros() - t0) / 5);
    lv_obj_delete(scr);
  } else if (!strcmp(cmd, "PIPEBENCH")) {
    // Same text, drawn through the real band pipeline on a loaded screen.
    lv_obj_t* old = lv_screen_active();
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);
    lv_obj_t* l = lv_label_create(scr);
    lv_obj_set_width(l, 300);
    lv_obj_set_style_text_font(l, fonts::get(14), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_screen_load(scr);
    const char* txt = "Hôm nay thời tiết Hà Nội mây rải rác, nhiệt độ 19 đến 26 độ, khả năng mưa 20%. "
                      "Họp nhóm tuần lúc 09:00, đón con lúc 14:30, ăn tối nhà ngoại lúc 19:00. "
                      "Giá vàng SJC 154,5 triệu đồng một lượng. Chỉ số VN-Index tăng 0,8%. ";
    for (int k = 0; k < 3; k++) {
      lv_label_set_text(l, k == 0 ? "" : txt);
      if (k == 2) lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
      lv_refr_now(nullptr);
      uint32_t t0 = micros();
      for (int i = 0; i < 10; i++) {
        lv_obj_invalidate(scr);
        lv_refr_now(nullptr);
      }
      Serial.printf("PIPEBENCH %s: %lu us per frame\n", k == 0 ? "empty" : k == 1 ? "text wrap" : "text clip",
                    (micros() - t0) / 10);
    }
    lv_screen_load(old);
    lv_obj_delete(scr);
  } else if (!strncmp(cmd, "LBENCH ", 7)) {
    // Frame time with N short labels in our font (f=0) or LVGL's Montserrat (f=1).
    int n = 0, f = 0;
    sscanf(cmd + 7, "%d %d", &n, &f);
    lv_obj_t* old = lv_screen_active();
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);
    for (int i = 0; i < n; i++) {
      lv_obj_t* l = lv_label_create(scr);
      lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_font(l, f ? &lv_font_montserrat_14 : fonts::get(14), 0);
      lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
      lv_label_set_text(l, "Hello 12");
      lv_obj_set_pos(l, (i % 4) * 78, (i / 4) * 24);
    }
    lv_screen_load(scr);
    lv_refr_now(nullptr);
    uint32_t t0 = micros();
    for (int i = 0; i < 10; i++) {
      lv_obj_invalidate(scr);
      lv_refr_now(nullptr);
    }
    Serial.printf("LBENCH n=%d font=%d: %lu us per frame\n", n, f, (micros() - t0) / 10);
    lv_screen_load(old);
    lv_obj_delete(scr);
  } else if (!strcmp(cmd, "RAWBENCH")) {
    Serial.printf("RAW cpu %lu MHz, flash %lu MHz, psram %u KB\n", (unsigned long)getCpuFrequencyMhz(),
                  (unsigned long)(ESP.getFlashChipSpeed() / 1000000), ESP.getPsramSize() / 1024);
    uint8_t* ps = (uint8_t*)heap_caps_malloc(307200, MALLOC_CAP_SPIRAM);
    uint8_t* in = (uint8_t*)heap_caps_malloc(30720, MALLOC_CAP_INTERNAL);
    uint32_t t0 = micros();
    for (int i = 0; i < 10; i++) memset(ps, i, 307200);
    const uint32_t tp = (micros() - t0) / 10;
    t0 = micros();
    for (int i = 0; i < 100; i++) memset(in, i, 30720);
    const uint32_t ti = (micros() - t0) / 10;
    t0 = micros();
    volatile uint32_t acc = 0;
    for (uint32_t i = 0; i < 1000000; i++) acc += i * 3;
    const uint32_t tc = micros() - t0;
    Serial.printf("RAW memset 300KB psram %lu us, 300KB internal %lu us, 1M loop %lu us\n", tp, ti, tc);
    free(ps);
    free(in);
  } else if (!strcmp(cmd, "PANEL")) {
    os::openControl();
  } else if (!strncmp(cmd, "INVLOG ", 7)) {
    lcd::logInvalid(cmd[7] == '1');
  } else if (!strncmp(cmd, "TOUCHLOG ", 9)) {
    trace::setEcho(cmd[9] == '1');
  } else if (!strncmp(cmd, "APP ", 4)) {  // QA: open an app ("APP clock timer")
    char id[52] = "", arg[64] = "";  // package ids may be 48 characters
    sscanf(cmd + 4, "%51s %63s", id, arg);
    apps::open(id, arg[0] ? arg : nullptr);
  } else if (!strncmp(cmd, "JS ", 3)) {  // evaluate JavaScript (QuickJS) and print the result
    const uint32_t t0 = micros();
    const String r = jsrt::eval(cmd + 3);
    Serial.printf("JS %s (%lu us)\n", r.c_str(), (unsigned long)(micros() - t0));
  } else if (!strncmp(cmd, "ROT ", 4)) {  // QA: rotation 0..3 (saved like Settings > Màn hình)
    os::applyRotation(atoi(cmd + 4));
  } else if (!strcmp(cmd, "SAVER")) {  // QA: the screen saver now
    os::showSaver();
  } else if (!strcmp(cmd, "RUNBENCH")) {  // the run game's speed test (docs/run.md): ~35 s of "RUNBENCH ..." lines
    run::bench();
  } else if (!strncmp(cmd, "TANK ", 5)) {  // TANK bench | bench 20 | free planted neon*7 | time 8 | fps 20 | stats (src/tank)
    Serial.println(tank::dev(cmd + 5) ? "TANK ok" : "TANK ?");
  } else if (!strncmp(cmd, "RUN ", 4)) {  // RUN auto | manual | god | warp 180 | end | smooth 0 | helper 0 | stats
    Serial.println(run::dev(cmd + 4) ? "RUN ok" : "RUN ?");
  } else if (!strcmp(cmd, "HOMEBENCH")) {
    home::bench();
  } else if (!strcmp(cmd, "REBUILD")) {
    os::rebuild();
  } else if (!strcmp(cmd, "HOME")) {
    os::goHome();
  } else if (!strncmp(cmd, "NETBENCH ", 9)) {  // QA: download speed, "NETBENCH <https url> [max KB]" (data dropped)
#ifndef SIM
    String arg = cmd + 9, url = arg;
    int maxKb = 1024;
    if (arg.indexOf(' ') > 0) url = arg.substring(0, arg.indexOf(' ')), maxKb = arg.substring(arg.indexOf(' ') + 1).toInt();
    desk::releaseConnection();
    for (int i = 0; i < 40 && desk::connectionOpen(); i++) delay(50);
    NetworkClientSecure tls;
    tls.setCACertBundle(x509_bundle_start, x509_bundle_end - x509_bundle_start);
    HTTPClient h;
    const uint32_t t0 = millis();
    if (!h.begin(tls, url)) return (void)Serial.println("NETBENCH begin failed");
    const int code = h.GET();
    const uint32_t t1 = millis();
    size_t got = 0;
    if (code == 200) {
      static uint8_t buf[4096];
      NetworkClient* st = h.getStreamPtr();
      while (got < (size_t)maxKb * 1024 && (h.connected() || st->available()) && millis() - t1 < 120000) {
        const int n = st->readBytes(buf, sizeof buf);
        if (n > 0) got += n;
        else delay(2);
      }
    }
    const uint32_t t2 = millis();
    h.end();
    Serial.printf("NETBENCH code %d, connect+first byte %lu ms, %u KB in %lu ms = %lu KB/s, heap %u KB internal\n", code,
                  (unsigned long)(t1 - t0), (unsigned)(got / 1024), (unsigned long)(t2 - t1),
                  (unsigned long)(t2 > t1 ? got / (t2 - t1) : 0), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
#endif
  } else if (!strncmp(cmd, "JSSTACK ", 8)) {  // dev: QuickJS's stack size for the next app opened
    int n = 0, u = 0;
    sscanf(cmd + 8, "%d %d", &n, &u);
    jsapp::stackSize(n, u);
  } else if (!strncmp(cmd, "CARDBENCH ", 10)) {  // card read speed into PSRAM / internal RAM, several ways
    const char* path = cmd + 10;
    constexpr uint32_t N = 14000, K = 8;
    uint8_t* ps = (uint8_t*)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM);
    uint8_t* in = (uint8_t*)heap_caps_aligned_alloc(4, 16384, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint8_t* vb = (uint8_t*)heap_caps_aligned_alloc(4, 8192, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint8_t* pb = (uint8_t*)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM);
    static const char* NAMES[] = {"stdio 128 B -> PSRAM", "stdio 4 KB internal -> PSRAM", "stdio 4 KB PSRAM -> PSRAM",
                                  "read() -> PSRAM", "read() -> internal", "read() 4 KB internal chunks -> PSRAM",
                                  "stdio 8 KB internal -> PSRAM", "stdio 16 KB PSRAM -> PSRAM"};
    for (int mode = 0; mode < 8 && ps && in && vb && pb; mode++) {
      const bool posix = mode >= 3 && mode <= 5;
      FILE* f = posix ? nullptr : fopen(path, "rb");
      const int fd = posix ? open(path, O_RDONLY) : -1;
      if (!f && fd < 0) {
        Serial.printf("CARDBENCH cannot open %s\n", path);
        break;
      }
      if (mode == 1) setvbuf(f, (char*)vb, _IOFBF, 4096);
      if (mode == 2) setvbuf(f, (char*)pb, _IOFBF, 4096);
      if (mode == 6) setvbuf(f, (char*)vb, _IOFBF, 8192);
      if (mode == 7) setvbuf(f, (char*)pb, _IOFBF, 16384);
      uint32_t off = 100000 * (mode + 1) + 37, t = 0, mx = 0;
      bool ok = true;
      for (uint32_t k = 0; k < K && ok; k++, off += N) {
        const uint32_t t0 = micros();
        if (mode == 5) {
          for (uint32_t done = 0; ok && done < N;) {
            const uint32_t pos = off + done, st = pos & ~511u, skip = pos - st;
            const uint32_t take = std::min(N - done, 4096 - skip), want = std::min<uint32_t>(4096, (skip + take + 511) & ~511u);
            ok = lseek(fd, st, SEEK_SET) == (off_t)st && read(fd, vb, want) >= (ssize_t)(skip + take);
            if (ok) memcpy(ps + done, vb + skip, take), done += take;
          }
        } else if (posix) {
          ok = lseek(fd, off, SEEK_SET) == (off_t)off && read(fd, mode == 4 ? in : ps, N) == (ssize_t)N;
        } else {
          ok = !fseek(f, off, SEEK_SET) && fread(ps, 1, N, f) == N;
        }
        const uint32_t us = micros() - t0;
        t += us, mx = std::max(mx, us);
      }
      if (f) fclose(f);
      if (fd >= 0) close(fd);
      Serial.printf("CARDBENCH %s: %s, 14 KB avg %.2f ms max %.2f ms\n", NAMES[mode], ok ? "ok" : "FAILED", t / 1000.0f / K,
                    mx / 1000.0f);
    }
    heap_caps_free(ps), heap_caps_free(in), heap_caps_free(vb), heap_caps_free(pb);
  } else if (!strcmp(cmd, "MEM")) {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    // The lowest internal RAM since the last MEM (the heap's local minimum), then since boot.
    static bool watching = false;
    const unsigned since = watching ? heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024 : 0;
    if (watching) heap_caps_monitor_local_minimum_free_size_stop();
    const unsigned boot = ESP.getMinFreeHeap() / 1024;
    watching = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
    if (since) Serial.printf("MEM lowest internal since the last MEM: %u KB\n", since);
    Serial.printf("MEM internal %u KB (largest %u KB, min %u KB), psram %u KB, lvgl used %u KB of %u KB, frag %u%%\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024, boot,
                  ESP.getFreePsram() / 1024, (unsigned)((m.total_size - m.free_size) / 1024), (unsigned)(m.total_size / 1024),
                  m.frag_pct);
  } else if (!strcmp(cmd, "TRACE")) {
    trace::dump();
  } else if (!strcmp(cmd, "SYNC")) {  // QA: sync now (fetches the firmware offer too)
    desk::syncNow();
    Serial.println("SYNC asked");
  } else if (!strcmp(cmd, "OTA")) {  // QA: what "Tải và cài" does (restarts into install mode)
    const desk::FwOffer& o = desk::fwOffer();
    Serial.printf("OTA offer %s (%u bytes)\n", o.version.length() ? o.version.c_str() : "none", (unsigned)o.size);
    if (o.version.length()) ota::start();
  } else if (!strcmp(cmd, "HEAPDIFF")) {  // internal RAM blocks in use that were not there at the last HEAPDIFF
    struct Blk {
      void* p;
      uint32_t n;
    };
    static Blk* prev = nullptr;
    static int prevN = 0;
    constexpr int CAP = 4096;
    struct Walk {
      Blk* b;
      int n;
    } w{(Blk*)heap_caps_malloc(sizeof(Blk) * CAP, MALLOC_CAP_SPIRAM), 0};
    if (w.b) {
      heap_caps_walk(MALLOC_CAP_INTERNAL, [](walker_heap_into_t, walker_block_info_t b, void* u) {
        Walk* w = (Walk*)u;
        if (b.used && w->n < CAP) w->b[w->n++] = {b.ptr, (uint32_t)b.size};
        return true;
      }, &w);
      uint32_t added = 0, total = 0;
      int shown = 0;
      for (int i = 0; i < w.n; i++) {
        total += w.b[i].n;
        bool old = false;
        for (int k = 0; k < prevN && !old; k++) old = prev[k].p == w.b[i].p && prev[k].n == w.b[i].n;
        if (old || !prev) continue;
        added += w.b[i].n;
        if (w.b[i].n >= 192 && shown < 40) {
          shown++;
          const uint8_t* d = (const uint8_t*)w.b[i].p;
          char asc[33];
          for (int k = 0; k < 32; k++) asc[k] = d[k] >= 32 && d[k] < 127 ? d[k] : '.';
          asc[32] = 0;
          Serial.printf("HEAP new %6u B at %p  %02x%02x%02x%02x%02x%02x%02x%02x  %s\n", (unsigned)w.b[i].n, w.b[i].p, d[0], d[1],
                        d[2], d[3], d[4], d[5], d[6], d[7], asc);
        }
      }
      Serial.printf("HEAPDIFF %d blocks, %u B in use; %u B in blocks new since the last HEAPDIFF\n", w.n, (unsigned)total,
                    (unsigned)added);
      if (prev) heap_caps_free(prev);
      prev = w.b;
      prevN = w.n;
    }
  } else if (!strcmp(cmd, "TASKS")) {  // every task: name, state, priority, stack never used, where its stack is
    const UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t* st = (TaskStatus_t*)heap_caps_malloc(sizeof(TaskStatus_t) * (n + 4), MALLOC_CAP_SPIRAM);
    if (st) {
      const UBaseType_t got = uxTaskGetSystemState(st, n + 4, nullptr);
      for (UBaseType_t i = 0; i < got; i++) {
        const bool ps = esp_ptr_external_ram(st[i].pxStackBase);
        Serial.printf("TASK %-16s state %d prio %2u free %5u B stack %s\n", st[i].pcTaskName, (int)st[i].eCurrentState,
                      (unsigned)st[i].uxCurrentPriority, (unsigned)st[i].usStackHighWaterMark, ps ? "psram" : "internal");
      }
      Serial.printf("TASKS %u\n", (unsigned)got);
      heap_caps_free(st);
    }
  } else if (wake::console(cmd)) {  // WAKE, WAKEREC n, WAKEPAUSE 0|1, PKGPUT (src/wake/wake.cpp)
#ifndef NO_PET
  } else if (pet::talk::console(cmd)) {  // ASK <text>, TALK (src/pet/talk.cpp)
#endif
  }
}

}  // namespace

void poll() {
  if (!line) line = (char*)heap_caps_malloc(LINE_CAP, MALLOC_CAP_SPIRAM);
  cableWifi();
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (len) {
        line[len] = 0;
        // A command means the computer is reading the port right now, so replies (screenshots
        // are 300 KB) may wait for it; everything else never waits (see main.cpp).
#if ARDUINO_USB_CDC_ON_BOOT  // the USB port; the UART (QEMU) always waits for room
        Serial.setTxTimeoutMs(100);
#endif
        run(line);
        Serial.flush();
#if ARDUINO_USB_CDC_ON_BOOT
        Serial.setTxTimeoutMs(0);
#endif
        len = 0;
      }
    } else if (len < LINE_CAP - 1) {
      line[len++] = (char)c;
    }
  }
}

}  // namespace devtools
