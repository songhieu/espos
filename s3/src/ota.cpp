#pragma GCC optimize("Os")  // cold code: built small (0.5.7 flash diet; the app slot is full)
#include "ota.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>

#include "board.h"
#include "desk.h"
#include "lcd.h"
#include "esp_ota_ops.h"
#include "fonts.h"
#include "i18n.h"
#include "mbedtls/sha256.h"
#include "net.h"
#include "power.h"
#include "touch.h"
#include "trace.h"

extern const uint8_t x509_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_bundle_end[] asm("_binary_x509_crt_bundle_end");

// A new image stays on probation (bootloader rollback) until ota::poll() confirms it.
extern "C" bool verifyRollbackLater() { return true; }

// How an install runs (2026-11-17, after 0.3.0 could not update a board that had run for hours):
// - start() only saves the offer (NVS "ota"/"inst") and restarts. The install runs on the next boot, in
//   install mode: no home screen, apps, sound or remote control, so the internal RAM that TLS and WiFi need
//   is there and in one piece (after hours of use the largest free block was 7 KB and the download task could
//   not even start; three TLS streams at once had taken internal RAM to 0 KB and the board reset).
// - The image is fetched in byte ranges on 1 to 6 TLS streams side by side (as many as the free internal RAM
//   allows), each opened once the one before it is through its handshake. One stream gets ~20 KB/s from here:
//   the TCP window is 5.7 KB and the server is ~0.3 s away.
// - The SHA-256 from the sync answer (which came over TLS) is checked before anything is written.
// - A failure is kept (NVS "err"/"errv": the English text, translated where it is shown), shown in Settings ›
//   Update and reported at the next sync; the board then boots its current version again.
// - Install mode loads no language pack (no package system there): its screen is English.
namespace ota {
namespace {

volatile bool busy = false;
char msg[96] = "";
bool pendingVerify = false;
uint32_t bootMs = 0;

// Install mode.
struct Inst {
  String version, url, sha256, notes;
  uint32_t size = 0;
};
Inst inst;
bool installMode = false, installStarted = false;
uint32_t netWaitMs = 0;

// Progress for the screens.
volatile Stage stage = Stage::Idle;
volatile uint32_t pDone = 0, pTotal = 0, pBps = 0;
volatile int pStreams = 0;
String lastErr, lastErrVersion;

struct Job {
  String version, url, sha256;
  uint32_t size;
};

void setMsg(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
}

String hex(const uint8_t* b, size_t n) {
  static const char* H = "0123456789abcdef";
  String s;
  for (size_t i = 0; i < n; i++) s += H[b[i] >> 4], s += H[b[i] & 15];
  return s;
}

void keepError(const String& version, const char* why, const char* text) {
  Preferences p;
  p.begin("ota", false);
  p.putString("err", text);
  p.putString("errv", version);
  p.putString("errw", why);
  p.end();
  lastErr = text;
  lastErrVersion = version;
}

void fail(const Job& j, const char* why, const char* text) {
  trace::add("ota %s failed: %s", j.version.c_str(), why);
  desk::reportOta(j.version, false, why);
  keepError(j.version, why, text);
  setMsg(T_("Couldn't install: %s"), T_(text));
  stage = Stage::Failed;
  busy = false;
}

// One byte range of the image, fetched on its own connection.
struct Piece {
  String url;
  size_t from, len;
  uint8_t* dst;
  volatile size_t got;
  volatile int state;  // 0 connecting, 2 receiving, 1 done, -1 failed
};

void pieceTask(void* arg) {
  Piece* p = (Piece*)arg;
  for (int attempt = 0; attempt < 8 && p->got < p->len; attempt++) {
    if (attempt) delay(min(400 * attempt, 2000));
    NetworkClientSecure tls;
    tls.setCACertBundle(x509_bundle_start, x509_bundle_end - x509_bundle_start);
    HTTPClient h;
    h.setConnectTimeout(8000);
    h.setTimeout(15000);
    if (!h.begin(tls, p->url)) continue;
    h.addHeader("Range", "bytes=" + String(p->from + p->got) + "-" + String(p->from + p->len - 1));
    const int code = h.GET();
    const size_t before = p->got;
    if (code == 206) {
      p->state = 2;
      // The TLS client's read returns what is decrypted so far (0 when nothing yet): wait for
      // data, give up only after 15 s without any, then reconnect from where it stopped.
      NetworkClient* s = h.getStreamPtr();
      uint32_t last = millis();
      while (p->got < p->len && millis() - last < 15000) {
        const int avail = s->available();
        if (avail <= 0) {
          if (!h.connected()) break;
          delay(2);
          continue;
        }
        const int n = s->read(p->dst + p->got, min<size_t>(avail, p->len - p->got));
        if (n > 0) p->got += n, last = millis();
      }
    }
    if (p->got < p->len)
      trace::add("ota range @%u try %d: http %d, +%u bytes, %u/%u, internal %u KB", (unsigned)p->from, attempt, code,
                 (unsigned)(p->got - before), (unsigned)p->got, (unsigned)p->len,
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    h.end();
  }
  p->state = p->got == p->len ? 1 : -1;
  vTaskSuspend(nullptr);  // deleted by the install task once all pieces are done (its stack is PSRAM)
}

void task(void* arg) {
  Job* j = (Job*)arg;
  desk::releaseConnection();  // the sync's kept TLS connection holds RAM the download needs
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  const size_t total = j->size;
  pTotal = total;
  uint8_t* img = total ? (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM) : nullptr;
  if (!part || !img || total > part->size) {
    fail(*j, "download", total ? N_("not enough memory") : N_("the server did not give the file size"));
  } else {
    // 1. Download into PSRAM: as many streams as the internal RAM allows (each TLS stream keeps ~8 KB of it).
    const unsigned freeKb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    const int N = constrain(((int)freeKb - 28) / 9, 1, 6);
    pStreams = N;
    stage = Stage::Download;
    trace::add("ota: %u KB internal free, %d streams", freeKb, N);
    Piece* pieces = new Piece[N];
    TaskHandle_t handles[6] = {};
    const size_t step = (total + N - 1) / N;
    const uint32_t t0 = millis();
    int started = 0;
    uint32_t lastStart = 0, rateAt = millis();
    size_t rateGot = 0;
    bool running = true, failed = false;
    while (running) {
      // The next stream once the previous one is through its handshake (or after 6 s): one handshake at a time.
      if (started < N && (started == 0 || pieces[started - 1].state != 0 || millis() - lastStart > 6000)) {
        Piece& p = pieces[started];
        p.url = j->url;
        p.from = started * step;
        p.len = min(step, total - p.from);
        p.dst = img + p.from;
        p.got = 0;
        p.state = 0;
        xTaskCreatePinnedToCoreWithCaps(pieceTask, "ota-dl", 10240, &p, 2, &handles[started], 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        started++;
        lastStart = millis();
      }
      delay(200);
      size_t got = 0;
      running = started < N;
      for (int i = 0; i < started; i++) {
        got += pieces[i].got;
        running |= pieces[i].state == 0 || pieces[i].state == 2;
        failed |= pieces[i].state < 0;
      }
      pDone = got;
      if (millis() - rateAt >= 1500) {
        pBps = (got - rateGot) * 1000 / (millis() - rateAt);
        rateGot = got;
        rateAt = millis();
      }
      setMsg(T_("Downloading %d%% · %.1f / %.1f MB"), (int)(got * 100 / total), got / 1048576.0f, total / 1048576.0f);
    }
    size_t got = 0;
    for (int i = 0; i < N; i++) {
      got += pieces[i].got;
      if (!handles[i]) continue;
      // Each piece task suspends itself when it ends; its stack is PSRAM, freed here.
      for (int k = 0; k < 50 && eTaskGetState(handles[i]) != eSuspended; k++) delay(20);
      vTaskDeleteWithCaps(handles[i]);
    }
    delete[] pieces;
    trace::add("ota download %u of %u bytes in %lu ms on %d streams%s", (unsigned)got, (unsigned)total,
               millis() - t0, N, failed ? " (failed)" : "");
    // 2. Check it.
    stage = Stage::Verify;
    setMsg("%s", T_("Checking the file"));
    uint8_t digest[32];
    mbedtls_sha256(img, total, digest, 0);
    esp_ota_handle_t h = 0;
    if (failed) {
      // The byte counts went to the trace above; the kept text is a plain sentence (translated where shown).
      fail(*j, "download", N_("the connection broke off during the download"));
    } else if (j->sha256.length() && !hex(digest, 32).equalsIgnoreCase(j->sha256)) {
      fail(*j, "checksum", N_("the downloaded file is damaged"));
    } else {
      // 3. Write it (erasing the slot first takes a few seconds).
      stage = Stage::Write;
      pDone = 0;
      setMsg("%s", T_("Writing to memory"));
      bool ok = esp_ota_begin(part, total, &h) == ESP_OK;
      for (size_t at = 0; ok && at < total; at += 16384) {
        ok = esp_ota_write(h, img + at, min<size_t>(16384, total - at)) == ESP_OK;
        pDone = min<size_t>(at + 16384, total);
        setMsg(T_("Writing %d%%"), (int)(pDone * 100 / total));
      }
      if (!ok) {
        esp_ota_abort(h);
        fail(*j, "download", N_("could not write to memory"));
      } else if (esp_ota_end(h) != ESP_OK) {
        fail(*j, "image check", N_("the file is not valid firmware"));
      } else if (esp_ota_set_boot_partition(part) != ESP_OK) {
        fail(*j, "image check", N_("could not switch to the new version"));
      } else {
        Preferences p;
        p.begin("ota", false);
        p.putString("try", j->version);
        p.remove("err");
        p.remove("errv");
        p.remove("errw");
        p.end();
        trace::add("ota %s written, restarting", j->version.c_str());
        stage = Stage::Restart;
        setMsg("%s", T_("Done, restarting"));
        heap_caps_free(img);
        delay(1200);
        esp_restart();
      }
    }
  }
  heap_caps_free(img);
  delete j;
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- the install-mode screen (mockups/s3-update.html, 3)
constexpr uint32_t FG = 0x111111, DIM = 0x6b6b70, FAINT = 0x8e8e93, LINE = 0xe5e5ea, BLUE = 0x0a84ff, GREEN = 0x34c759,
                   RED = 0xff3b30;
lv_obj_t *uiPct, *uiBar, *uiLeft, *uiRight, *uiNote, *uiCap;
lv_obj_t* uiStep[5];
lv_obj_t* uiDot[5];

lv_obj_t* text(lv_obj_t* parent, const char* s, int px, bool bold, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, fonts::text(px, bold), 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, s);
  return l;
}

String mb(uint32_t b) {
  char t[16];
  snprintf(t, sizeof t, "%.1f", b / 1048576.0f);
  String s = t;
  s.replace(".", i18n::decSep());
  return s;
}

// The first letter in upper case (UTF-8 aware): the kept errors start in lower case.
String capFirst(const String& s) {
  const uint8_t c = s.length() ? s[0] : 0;
  const int n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
  return i18n::upper(s.substring(0, n)) + s.substring(n);
}

void paintInstall(lv_timer_t*) {
  const Stage st = stage;
  const uint32_t total = pTotal ? pTotal : inst.size;
  const uint32_t done = pDone;
  int pct = 0;
  if (st == Stage::Download && total) pct = done * 100 / total;
  if (st == Stage::Verify) pct = 100;
  if (st == Stage::Write && total) pct = done * 100 / total;
  if (st == Stage::Restart) pct = 100;
  char t[96];
  snprintf(t, sizeof t, "%d%%", pct);
  lv_label_set_text(uiPct, t);
  lv_bar_set_value(uiBar, pct, LV_ANIM_OFF);
  static const char* const caps[] = {N_("Preparing"), N_("Downloading"),        N_("Checking"),
                                     N_("Writing"),   N_("Done, restarting"), N_("Not installed")};
  const int ci = st == Stage::Download ? 1 : st == Stage::Verify ? 2 : st == Stage::Write ? 3 : st == Stage::Restart ? 4
               : st == Stage::Failed ? 5 : 0;
  lv_label_set_text(uiCap, (i18n::upper(T_(caps[ci])) + " " + inst.version).c_str());
  if (st == Stage::Download) {
    lv_label_set_text(uiLeft, (mb(done) + " / " + mb(total) + " MB").c_str());
    const uint32_t bps = pBps;
    if (bps > 0) {
      const uint32_t left = (total - done) / bps;
      if (left >= 60) snprintf(t, sizeof t, T_("%u KB/s · %u min left"), (unsigned)(bps / 1024), (unsigned)(left / 60 + 1));
      else snprintf(t, sizeof t, T_("%u KB/s · %u s left"), (unsigned)(bps / 1024), (unsigned)left);
    } else {
      snprintf(t, sizeof t, "%s", T_("connecting"));
    }
    lv_label_set_text(uiRight, t);
  } else if (st == Stage::Write) {
    lv_label_set_text(uiLeft, (mb(done) + " / " + mb(total) + " MB").c_str());
    lv_label_set_text(uiRight, T_("don't turn it off"));
  } else {
    lv_label_set_text(uiLeft, "");
    lv_label_set_text(uiRight, "");
  }
  // Steps: prepare, download, check, write, restart.
  const int now = st == Stage::Idle ? 0 : st == Stage::Download ? 1 : st == Stage::Verify ? 2 : st == Stage::Write ? 3 : 4;
  for (int i = 0; i < 5; i++) {
    const bool done_ = st != Stage::Failed && i < now;
    const bool cur = st != Stage::Failed && i == now;
    lv_obj_set_style_bg_color(uiDot[i], lv_color_hex(done_ ? GREEN : cur ? BLUE : 0xffffff), 0);
    lv_obj_set_style_border_width(uiDot[i], done_ || cur ? 0 : 2, 0);
    lv_obj_set_style_text_color(uiStep[i], lv_color_hex(done_ || cur ? FG : FAINT), 0);
  }
  if (st == Stage::Download && pStreams > 1) {
    snprintf(t, sizeof t, T_("Download · %d streams"), (int)pStreams);
    lv_label_set_text(uiStep[1], t);
  }
  if (st == Stage::Failed) {
    lv_obj_set_style_text_color(uiCap, lv_color_hex(RED), 0);
    lv_label_set_text(uiNote, i18n::fmt(N_("{0}. The device restarts with the version it has."),
                                        capFirst(T_(lastErr.c_str()))).c_str());
  } else if (!installStarted && net::state() != net::State::Connected) {
    lv_label_set_text(uiNote, T_("Connecting to WiFi. Don't turn the device off; this screen closes by itself when done."));
  } else {
    lv_label_set_text(uiNote, T_("Don't turn the device off. This screen closes by itself when done."));
  }
}

void buildInstallScreen() {
  // The saved way up (no Home here to apply it): a landscape case shows it the right way round.
  static const lv_display_rotation_t ROT[] = {LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180,
                                              LV_DISPLAY_ROTATION_270};
  lcd::setRotation(ROT[desk::settingInt("s3.rot", 0) & 3]);
  const bool land = lv_display_get_horizontal_resolution(nullptr) > lv_display_get_vertical_resolution(nullptr);
  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_t* col = lv_obj_create(scr);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_style_pad_hor(col, 20, 0);
  lv_obj_set_style_pad_top(col, land ? 40 : 56, 0);
  lv_obj_set_flex_flow(col, land ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 6, 0);
  lv_obj_set_style_pad_column(col, 24, 0);
  // Landscape: the progress on the left, the steps on the right, the note under both.
  lv_obj_t *left = col, *right = col;
  if (land) {
    for (lv_obj_t** p : {&left, &right}) {
      *p = lv_obj_create(col);
      lv_obj_remove_style_all(*p);
      lv_obj_set_size(*p, (lv_display_get_horizontal_resolution(nullptr) - 40 - 24) / 2, LV_SIZE_CONTENT);
      lv_obj_set_flex_flow(*p, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_row(*p, 6, 0);
    }
  }
  uiCap = text(left, "", 12, true, FAINT);
  uiPct = text(left, "0%", 34, true, FG);
  uiBar = lv_bar_create(left);
  lv_obj_set_size(uiBar, lv_pct(100), 8);
  lv_bar_set_range(uiBar, 0, 100);
  lv_obj_set_style_bg_color(uiBar, lv_color_hex(LINE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(uiBar, lv_color_hex(BLUE), LV_PART_INDICATOR);
  lv_obj_set_style_radius(uiBar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(uiBar, 4, LV_PART_INDICATOR);
  lv_obj_t* meta = lv_obj_create(left);
  lv_obj_remove_style_all(meta);
  lv_obj_set_size(meta, lv_pct(100), LV_SIZE_CONTENT);
  uiLeft = text(meta, "", 12, false, DIM);
  uiRight = text(meta, "", 12, false, DIM);
  lv_obj_align(uiRight, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_t* gap = lv_obj_create(right);
  lv_obj_remove_style_all(gap);
  lv_obj_set_size(gap, 1, land ? 0 : 12);
  static const char* const names[] = {N_("Prepare"), N_("Download"), N_("Check the file"), N_("Write to memory"),
                                      N_("Restart")};
  for (int i = 0; i < 5; i++) {
    lv_obj_t* row = lv_obj_create(right);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 26);
    uiDot[i] = lv_obj_create(row);
    lv_obj_remove_style_all(uiDot[i]);
    lv_obj_set_size(uiDot[i], 16, 16);
    lv_obj_set_style_radius(uiDot[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(uiDot[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(uiDot[i], lv_color_hex(LINE), 0);
    lv_obj_align(uiDot[i], LV_ALIGN_LEFT_MID, 0, 0);
    uiStep[i] = text(row, T_(names[i]), 14, false, FAINT);
    lv_obj_align(uiStep[i], LV_ALIGN_LEFT_MID, 26, 0);
  }
  uiNote = text(col, "", 12, false, FAINT);
  lv_obj_set_width(uiNote, lv_pct(100));
  lv_label_set_long_mode(uiNote, LV_LABEL_LONG_WRAP);
  lv_timer_create(paintInstall, 300, nullptr);
  paintInstall(nullptr);
}

void startTask(const String& version, const String& url, const String& sha, uint32_t size) {
  busy = true;
  stage = Stage::Idle;
  pDone = 0;
  pTotal = size;
  setMsg(T_("Starting to install %s"), version.c_str());
  trace::add("ota: installing %s (%u bytes)", version.c_str(), size);
  Job* j = new Job{version, url, sha, size};
  if (xTaskCreatePinnedToCore(task, "ota", 8192, j, 2, nullptr, 0) != pdPASS) {
    delete j;
    fail(Job{version, url, sha, size}, "download", N_("not enough memory: turn the device off and on, then try again"));
  }
}

}  // namespace

void begin() {
  bootMs = millis();
  esp_ota_img_states_t st;
  const esp_partition_t* run = esp_ota_get_running_partition();
  pendingVerify = esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  Preferences p;
  p.begin("ota", false);
  const String tried = p.isKey("try") ? p.getString("try", "") : "";
  if (tried.length() && tried != FW_VERSION && !pendingVerify) {
    // The bootloader went back to this image: the new one did not survive its probation.
    desk::reportOta(tried, false, "rollback");
    trace::add("ota %s rolled back", tried.c_str());
    p.remove("try");
    p.putString("err", N_("the new version did not start, so the device went back to this one"));
    p.putString("errv", tried);
  }
  // An install start() asked for: taken now (cleared at once, so a crash during it boots normally next time).
  if (p.isKey("inst")) {
    inst.version = p.getString("inst", "");
    inst.url = p.getString("iurl", "");
    inst.sha256 = p.getString("isha", "");
    inst.size = p.getUInt("isize", 0);
    p.remove("inst");
    installMode = inst.version.length() && inst.url.length() && inst.size;
  }
  if (p.isKey("err")) {
    lastErr = p.getString("err", "");
    lastErrVersion = p.getString("errv", "");
    // Reported once, at the next sync.
    if (!installMode && p.isKey("errw")) {
      desk::reportOta(lastErrVersion, false, p.getString("errw", ""));
      p.remove("errw");
    }
  }
  p.end();
  if (installMode) {
    trace::setEcho(true);  // install mode prints its steps on the serial port (nothing else runs)
    trace::add("ota: install mode for %s", inst.version.c_str());
  }
}

bool installing() { return installMode; }

void beginInstallScreen() { buildInstallScreen(); }

void poll() {
  if (installMode) {
    // Install mode: start once WiFi is up; give up after 90 s without it.
    if (!installStarted) {
      if (net::state() == net::State::Connected) {
        installStarted = true;
        startTask(inst.version, inst.url, inst.sha256, inst.size);
      } else if (millis() - bootMs > 90000) {
        installStarted = true;
        keepError(inst.version, "download", N_("the device could not connect to WiFi"));
        stage = Stage::Failed;
      }
    }
    static uint32_t failedAt = 0;
    if (stage == Stage::Failed) {
      if (!failedAt) failedAt = millis();
      if (millis() - failedAt > 6000) esp_restart();  // back to the normal screens, which show the error
    }
    return;
  }
  // A new image is kept only once it has reached the server again (a sync worked), so a build
  // that boots but cannot get online never strands a board that has no cable: no sync within
  // 15 minutes and the bootloader goes back to the previous image.
  if (pendingVerify && millis() - bootMs > 20000 && !desk::lastSyncMs() && millis() - bootMs > 15 * 60000UL) {
    trace::add("ota: no sync in 15 min, rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
  if (pendingVerify && millis() - bootMs > 20000 && desk::lastSyncMs()) {
    pendingVerify = false;
    esp_ota_mark_app_valid_cancel_rollback();
    desk::reportOta(FW_VERSION, true, "");
    Preferences p;
    p.begin("ota", false);
    p.remove("try");
    p.end();
    trace::add("ota: this image confirmed");
  }
  // An install the web asked for (or "while charging"): when nobody has used the screen for a
  // minute and there is power.
  const desk::FwOffer& o = desk::fwOffer();
  if (!busy && o.install && o.version.length() && net::state() == net::State::Connected &&
      millis() - touch::lastTouchMs() > 60000 && (power::usb() || power::percent() >= 30)) {
    Preferences p;
    p.begin("ota", false);
    const String key = "n" + String(o.version.length() > 12 ? o.version.substring(o.version.length() - 12) : o.version);
    const int tries = p.getInt(key.c_str(), 0);
    if (tries < 3) {
      p.putInt(key.c_str(), tries + 1);
      p.end();
      start();
    } else {
      p.end();
    }
  }
}

void start() {
  const desk::FwOffer& o = desk::fwOffer();
  if (busy || !o.version.length() || !o.url.length() || !o.size) return;
  busy = true;
  Preferences p;
  p.begin("ota", false);
  p.putString("inst", o.version);
  p.putString("iurl", o.url);
  p.putString("isha", o.sha256);
  p.putUInt("isize", o.size);
  p.remove("err");
  p.remove("errv");
  p.remove("errw");
  p.end();
  lastErr = "";
  trace::add("ota: restarting to install %s", o.version.c_str());
  setMsg(T_("Restarting to install %s"), o.version.c_str());
  stage = Stage::Restart;
  delay(700);
  esp_restart();
}

bool running() { return busy; }
const char* status() { return msg; }
Progress progress() { return Progress{stage, pDone, pTotal, pBps, pStreams}; }
const String& lastError() { return lastErr; }
const String& lastErrorVersion() { return lastErrVersion; }
void clearError() {
  lastErr = "";
  lastErrVersion = "";
  Preferences p;
  p.begin("ota", false);
  p.remove("err");
  p.remove("errv");
  p.remove("errw");
  p.end();
}

}  // namespace ota
