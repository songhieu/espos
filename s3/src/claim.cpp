#include "claim.h"
#if !ESPOS_NO_CLAIM

#include <Preferences.h>

#include "apps.h"
#include "board.h"
#include "desk.h"
#include "fonts.h"
#include "i18n.h"
#include "net.h"
#include "os.h"
#include "pkg/pkg.h"
#include "trace.h"
#include "uikit.h"
#include "src/misc/lv_text_private.h"  // lv_text_encoded_next
#ifndef SIM
#include <LittleFS.h>

#include "esp_hmac.h"
#endif

#pragma GCC optimize("Os")  // screens made once: size over speed

namespace claim {
namespace {

using namespace kit;

String code_, url_;
int step = 0;  // 0 the language, 1 the WiFi, 2 the code (kept: a restart goes on where it was)
bool loaded = false;
lv_obj_t* scr = nullptr;  // the step's own screen while it is open
uint32_t calmAt = 0;
bool welcomeDue = false;  // claimed: the welcome shows on Home (after the language's rebuild of the screens)

void load() {
  if (loaded) return;
  loaded = true;
  Preferences p;
  if (!p.begin("claim", true)) return;
  code_ = p.getString("code", ""), url_ = p.getString("url", ""), step = p.getInt("step", 0);
  p.end();
}
void save() {
  Preferences p;
  if (!p.begin("claim", false)) return;
  p.putString("code", code_);
  p.putString("url", url_);
  p.putInt("step", step);
  p.end();
}

void closed(lv_event_t* e) {
  if (lv_event_get_current_target(e) == scr) scr = nullptr;
}
lv_obj_t* screen(const char* title, lv_obj_t** body) {
  scr = os::screen(title, body, os::ROOT);
  lv_obj_add_event_cb(scr, closed, LV_EVENT_DELETE, nullptr);
  return scr;
}

// ---------------------------------------------------------------- 1. the language (the packs Kho máy put in)
void fillLang(lv_obj_t* body) {
  lv_obj_clean(body);
  const Pal& p = pal();
  for (int i = 0; i < i18n::LANG_COUNT; i++) {
    const i18n::Lang* l = &i18n::LANGS[i];
    if (strcmp(l->tag, "en") && !i18n::installed(l->tag)) continue;
    const bool on = !strcmp(l->tag, i18n::wanted());
    lv_obj_t* r = box(body, on ? p.chip : p.cell, 12);
    lv_obj_set_size(r, lv_pct(100), 50);
    lv_obj_set_style_pad_hor(r, 14, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* n = label(r, "", 16, on, on ? p.link : p.fg);
    // In its own language, never translated (anyone finds theirs); its English name while the fonts shown lack its
    // letters (日本語 before the Japanese font pack is in use).
    const lv_font_t* f = lv_obj_get_style_text_font(n, 0);
    bool ok = true;
    for (uint32_t k = 0, cp; ok && (cp = lv_text_encoded_next(l->name, &k));) {
      lv_font_glyph_dsc_t g;
      ok = lv_font_get_glyph_dsc(f, &g, cp, 0);
    }
    lv_label_set_text(n, ok ? l->name : T_(l->english));
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      desk::setSetting("sys.lang", i18n::LANGS[(intptr_t)lv_event_get_user_data(e)].tag);  // switches once closed
      fillLang(lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e)));
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  lv_obj_t* b = button(body, N_("Next"), col::IOS_BLUE, 0xffffff, [](lv_event_t*) {
    step = 1, save();
    os::back();
  });
  lv_obj_set_width(b, lv_pct(100));
  lv_obj_set_style_margin_top(b, 8, 0);
}

// ---------------------------------------------------------------- 3. the code, large, and its QR
void openCode() {
  lv_obj_t* body;
  screen(N_("Activate this device"), &body);
  const Pal& p = pal();
  const bool land = os::landscape();
  const int qs = land ? 150 : 176, w = os::W() - 24 - (land ? qs + 16 : 0);
  lv_obj_t* col = body;
  if (land) {  // words left, the code right
    lv_obj_t* row = stack(body, true, 16);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    col = stack(row, false, 10);
    lv_obj_set_width(col, w);
    qr(row, url_.c_str(), qs);
  }
  lv_obj_t* t = label(col, "", 14, false, p.dim);
  wrapText(t, T_("Go to espos.dev, create an account, choose Add device and enter this code. Or scan the QR code with your phone."), w, 14);
  lv_obj_t* c = label(col, (code_.length() == 8 ? code_.substring(0, 4) + "-" + code_.substring(4) : code_).c_str(), 30, true, p.fg);
  lv_obj_set_style_text_font(c, fonts::text(30, true), 0);  // a size with every letter (the big ones are digits only)
  lv_obj_set_style_text_letter_space(c, 3, 0);
  if (!land) {  // one centred column: the words, the code, its QR
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    qr(col, url_.c_str(), qs);
  }
  lv_obj_t* n = label(col, "", 12, false, p.faint);
  wrapText(n, T_("The code is also printed on the box label. The device opens by itself once you enter it."), w, 12);
  os::open(scr);
}

// ---------------------------------------------------------------- the welcome, once
void welcome() {
  const Pal& p = pal();
  lv_obj_t* c = box(lv_layer_top(), p.cell, 0);
  lv_obj_set_size(c, os::W(), os::H());
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(c, 12, 0);
  const String who = desk::user().indexOf('@') < 0 ? desk::user() : String();  // a name, never an e-mail address
  const int w = os::W() - 48;
  lv_obj_t* t = label(c, "", 24, true, p.fg);
  fitText(t, who.length() ? i18n::fmt(N_("Hello {0}!"), who).c_str() : N_("Hello!"), w, 64, 24, 16, true);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* l = label(c, "", 15, false, p.dim);
  wrapText(l, T_("This device is yours now."), w, 15);
  lv_obj_t* b = button(c, N_("Done"), col::IOS_BLUE, 0xffffff,
                       [](lv_event_t* e) { lv_obj_delete_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e))); });
  lv_obj_set_width(b, w);
  lv_obj_set_style_margin_top(b, 10, 0);
}

// ---------------------------------------------------------------- Kho máy's checks and resets
void info() {  // (written by hand: every value is a plain word, no JSON library needed)
  String m4;
  const String id = desk::hwid(&m4);
  const pkg::VolInfo v = pkg::volume(pkg::SD);
  Serial.printf("ESPOS:INFO {\"fw\":\"%s\",\"board\":\"%s\",\"mac4\":\"%s\",\"hwid\":\"%s\",\"token_sha8\":\"%s\","
                "\"claim_tail\":\"%s\",\"sd\":{\"present\":%s,\"free\":%llu},\"pkgs\":[",
                FW_VERSION, board::name(), m4.c_str(), id.c_str(), desk::tokenSha8().c_str(),
                code_.length() >= 4 ? code_.c_str() + code_.length() - 4 : "", v.present ? "true" : "false",
                (unsigned long long)v.freeKB * 1024);
  bool first = true;
  for (const pkg::Package& p : pkg::list()) Serial.printf("%s\"%s\"", first ? "" : ",", p.id.c_str()), first = false;
  Serial.println("]}");
}

// The string value of "key" in a one-level JSON object ("" when absent): ESPOS:CLAIM's two plain words.
String field(const char* j, const char* key) {
  const String k = String("\"") + key + "\"";
  const char* p = strstr(j, k.c_str());
  if (!p || !(p = strchr(p + k.length(), '"'))) return String();
  const char* e = strchr(p + 1, '"');
  return e ? String(p + 1).substring(0, e - p - 1) : String();
}

// The person's data and WiFi go; the packages and the identity (token, claim code: "link", "claim", the package
// index) stay. A returned unit: ESPOS:FACTORY_RESET, then ESPOS:CLAIM with a new code. Restarts.
void factoryReset() {
#ifndef SIM
  for (const char* ns : {"desk", "wifi", "pet", "run", "lock", "lic", "news", "remote", "wake"}) {
    Preferences p;
    if (p.begin(ns, false)) p.clear(), p.end();
  }
  for (const char* f : {"/settings.json", "/dash.json", "/sys/notif.json", "/tank.sav"}) LittleFS.remove(f);
  if (fs::File dir = LittleFS.open("/ph")) {  // the photos' copies
    std::vector<String> all;
    for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile()) all.push_back(String("/ph/") + f.name());
    dir.close();
    for (const String& f : all) LittleFS.remove(f);
  }
  step = 0, save();  // the language and the WiFi again on the next boot
  Serial.println("ESPOS:OK");
  Serial.flush();
  delay(300);
  ESP.restart();
#else
  Serial.println("ESPOS:OK (simulator: nothing erased)");
#endif
}

// Device attestation (eFuse v2, OFF): HMAC-SHA256 of the challenge with the key in eFuse BLOCK_KEY5 (purpose HMAC_UP).
// Answers only when NVS "claim/attest" is 1, which nothing writes yet; this firmware has no code that burns an eFuse.
void attest(const char* hexChallenge) {
  Preferences p;
  const bool on = p.begin("claim", true) && p.getInt("attest", 0) == 1;
  p.end();
  if (!on) return (void)Serial.println("ESPOS:ERR attestation off");
#ifndef SIM
  uint8_t msg[64], mac[32];
  size_t n = 0;
  for (const char* h = hexChallenge; h[0] && h[1] && n < sizeof msg; h += 2) {
    char b[3] = {h[0], h[1], 0};
    msg[n++] = strtoul(b, nullptr, 16);
  }
  if (esp_hmac_calculate(HMAC_KEY5, msg, n, mac) != ESP_OK) return (void)Serial.println("ESPOS:ERR no key");
  String s;
  for (uint8_t b : mac) s += "0123456789abcdef"[b >> 4], s += "0123456789abcdef"[b & 15];
  Serial.printf("ESPOS:ATTEST {\"hmac\":\"%s\"}\n", s.c_str());
#endif
}

}  // namespace

bool pending() {
  load();
  return code_.length() > 0;
}

void set(const char* code, const char* url) {
  load();
  String c = code ? code : "";
  c.replace("-", "");
  c.toUpperCase();
  if (c.length() != 8) return;
  code_ = c;
  url_ = url && *url ? String(url) : String(ESPOS_WEB "/app#claim=") + c;
  save();  // (the step stays: a new label mid-way goes on where it was; a reset or a fresh unit starts at 0)
  if (scr) os::goHome();  // a new code on the screen: drawn again
  trace::add("claim: code set (..%s)", c.substring(4).c_str());
}

void claimed() {
  load();
  if (!code_.length()) return;
  code_ = url_ = "", step = 0;
  Preferences p;
  if (p.begin("claim", false)) p.remove("code"), p.remove("url"), p.remove("step"), p.end();
  trace::add("claim: claimed");
  if (scr) os::goHome();
  welcomeDue = true;
}

void tick() {
  load();
  if (welcomeDue && os::atHome() && !os::saverOn() && os::idleMs() > 1000) welcomeDue = false, welcome();
  if (!code_.length()) return;
  const bool online = net::state() == net::State::Connected;
  if (step == 1 && online) {  // the WiFi works: the code next (its page closes)
    step = 2, save();
    if (!os::atHome() && !scr) os::goHome();
  }
  if (!os::atHome() || os::saverOn() || os::fingerDown()) return (void)(calmAt = millis());
  if (millis() - calmAt < 800) return;  // at Home a moment first (the steps open one after another)
  calmAt = millis();
  if (step == 0) {
    int n = 0;
    for (int i = 0; i < i18n::LANG_COUNT; i++) n += !strcmp(i18n::LANGS[i].tag, "en") || i18n::installed(i18n::LANGS[i].tag);
    if (n < 2) return (void)(step = 1, save());
    lv_obj_t* body;
    screen(N_("Language"), &body);
    fillLang(body);
    os::open(scr);
  } else if (step == 1) {
    app_settings::open("wifi");  // Settings' own WiFi page (scan, the on-screen keyboard)
  } else {
    openCode();
  }
}

#ifdef SIM
void devStep(int s) { step = s, save(), os::goHome(); }  // the simulator's shots: a step straight away
#endif

bool usb(const char* line) {
  load();
  if (!strcmp(line, "ESPOS:INFO")) return info(), true;
  if (!strcmp(line, "ESPOS:FACTORY_RESET")) return factoryReset(), true;
  if (!strncmp(line, "ESPOS:ATTEST ", 13)) return attest(line + 13), true;
  if (!strncmp(line, "ESPOS:CLAIM ", 12)) {
    String c = field(line + 12, "claim");
    set(c.c_str(), field(line + 12, "claim_url").c_str());
    c.replace("-", "");
    c.toUpperCase();
    Serial.println(c.length() == 8 && code_ == c ? "ESPOS:OK" : "ESPOS:ERR bad claim");
    return true;
  }
  if (!strncmp(line, "ESPOS:FACTORY ", 14))  // the optional network install: this firmware takes a pre-imaged card
    return Serial.println("ESPOS:ERR unsupported"), true;
  return false;
}

}  // namespace claim
#endif  // !ESPOS_NO_CLAIM
