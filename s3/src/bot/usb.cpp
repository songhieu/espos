#include "usb.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>

#include <string>

#include "../lic.h"
#include "../trace.h"
#include "app.h"
#include "config.h"
#include "ear.h"
#include "gfx.h"
#include "link.h"
#include "net.h"
#include "oled.h"
#include "spk.h"
#include "store.h"
#include "talk.h"
#include "ui.h"

namespace bot {
namespace usb {
namespace {

std::string field(const char* j, const char* key) {  // a string value in a one-level JSON object
  JsonDocument d;
  if (deserializeJson(d, j)) return "";
  return d[key] | "";
}

std::string tokenSha8() {
  const std::string t = store::token();
  if (t.empty()) return "";
  uint8_t d[32];
  mbedtls_sha256((const uint8_t*)t.data(), t.size(), d, 0);
  char h[9];
  snprintf(h, sizeof h, "%02x%02x%02x%02x", d[0], d[1], d[2], d[3]);
  return h;
}

void info() {
  JsonDocument d;
  d["fw"] = FW_VERSION, d["kind"] = "bot", d["board"] = BOT_BOARD;
  d["mac4"] = link::mac4(), d["hwid"] = link::hwid(), d["token_sha8"] = tokenSha8();
  const std::string c = store::claimCode();
  d["claim_tail"] = c.size() >= 4 ? c.substr(c.size() - 4) : "";
  d["linked"] = link::state() == link::State::Linked;
  JsonArray w = d["wifi"].to<JsonArray>();
  for (const auto& n : store::wifi()) w.add(n.ssid);
  std::string s;
  serializeJson(d, s);
  Serial.printf("ESPOS:INFO %s\n", s.c_str());
}

void shot() {
  static const char* H = "0123456789abcdef";
  std::string s;
  s.reserve(2048);
  for (uint8_t b : gfx::fb) s += H[b >> 4], s += H[b & 15];
  Serial.printf("SHOT %s\n", s.c_str());
}

void state() {
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["ui"] = (int)ui::state();
  d["look"] = ui::look();
  d["link"] = (int)link::state();
  d["online"] = net::online();
  d["portal"] = net::portal();
  d["ssid"] = net::ssid();
  d["time"] = net::timeValid();
  d["talk"] = talk::active();
  d["heap"] = ESP.getFreeHeap();
  d["psram"] = ESP.getFreePsram();
  d["oled_err"] = oled::errors();
  d["lic"] = (int)lic::ai();
  d["ai_left"] = lic::aiLeft();
  d["offer"] = link::offerVersion();
  std::string s;
  serializeJson(d, s);
  Serial.printf("STATE %s\n", s.c_str());
}

void wakeRec(int secs) {  // a recording for the mic's calibration, as the S3's WAKEREC
  if (!ear::captureStart(0, secs * 1000)) return (void)Serial.println("WAKEREC busy");
  const uint32_t t0 = millis();
  while (!ear::captureFull() && millis() - t0 < (uint32_t)secs * 1000 + 500) delay(20);
  uint32_t bytes = 0;
  const uint8_t* w = ear::wav(bytes, 100000);
  Serial.printf("WAKEREC %u\n", (unsigned)bytes);
  for (uint32_t o = 0; o < bytes; o += 4096) Serial.write(w + o, bytes - o < 4096 ? bytes - o : 4096);
  Serial.print("\nENDWAKEREC\n");
  ear::captureFree();
}

void run(char* cmd) {
  if (!strncmp(cmd, "ESPOS:HELLO ", 12)) {
    net::adoptClock(atoll(cmd + 12));
    Serial.printf("ESPOS:READY {\"fw\":\"%s\",\"kind\":\"bot\",\"board\":\"%s\",\"caps\":[\"claim\",\"softap\"],\"linked\":%s}\n",
                  FW_VERSION, BOT_BOARD, link::state() == link::State::Linked ? "true" : "false");
  } else if (!strcmp(cmd, "ESPOS:INFO")) {
    info();
  } else if (!strncmp(cmd, "ESPOS:PROVISION ", 16)) {
    std::string err;
    if (link::provision(cmd + 16, err)) Serial.println("ESPOS:OK");
    else Serial.printf("ESPOS:ERR %s\n", err.c_str());
  } else if (!strncmp(cmd, "ESPOS:CLAIM ", 12)) {
    std::string c = field(cmd + 12, "claim");
    store::setClaim(c, field(cmd + 12, "claim_url"));
    Serial.println(store::claimCode().size() == 8 ? "ESPOS:OK" : "ESPOS:ERR bad claim");
  } else if (!strcmp(cmd, "ESPOS:FACTORY_RESET")) {
    store::factoryReset();
    Serial.println("ESPOS:OK");
    Serial.flush();
    delay(300);
    ESP.restart();
  } else if (!strncmp(cmd, "ESPOS:", 6)) {
    Serial.println("ESPOS:ERR unsupported");
  } else if (!strncmp(cmd, "CMD:WIFIADD ", 12)) {
    char* tab = strchr(cmd + 12, '\t');
    if (!tab) return (void)Serial.println("WIFIADD_ERR");
    *tab = 0;
    store::addWifi(cmd + 12, tab + 1);
    net::reconnect();
    Serial.println("WIFIADD_OK");
  } else if (!strcmp(cmd, "SHOT")) {
    shot();
  } else if (!strcmp(cmd, "STATE")) {
    state();
  } else if (!strncmp(cmd, "UI ", 3)) {
    app::uiTest(cmd + 3);
    Serial.println("OK");
  } else if (!strncmp(cmd, "FAKE ", 5)) {  // FAKE <status> <json>: a turn's answer without the network
    char* sp = strchr(cmd + 5, ' ');
    talk::fake(atoi(cmd + 5), sp ? sp + 1 : "");
    Serial.println("OK");
  } else if (!strcmp(cmd, "MIC")) {
    Serial.println(ear::stats().c_str());
  } else if (!strncmp(cmd, "MICGAIN ", 8)) {
    ear::setGain(atoi(cmd + 8));
    Serial.println(ear::stats().c_str());
  } else if (!strncmp(cmd, "WAKEREC ", 8)) {
    wakeRec(constrain(atoi(cmd + 8), 1, 9));
  } else if (!strcmp(cmd, "WAKELAST")) {  // the sound of the last "Gâu gâu" (as WAKEREC's lines)
    uint32_t bytes = 0;
    const uint8_t* w = ear::lastFire(bytes);
    Serial.printf("WAKEREC %u\n", (unsigned)bytes);
    for (uint32_t o = 0; o < bytes; o += 4096) Serial.write(w + o, bytes - o < 4096 ? bytes - o : 4096);
    Serial.print("\nENDWAKEREC\n");
  } else if (!strncmp(cmd, "SET ", 4)) {
    char* sp = strchr(cmd + 4, ' ');  // "SET key" alone: an empty value (e.g. bot.night off)
    if (sp) *sp = 0;
    store::set(cmd + 4, sp ? sp + 1 : "", true);
    Serial.println("OK");
  } else if (!strcmp(cmd, "SYNC")) {
    link::syncNow();
    Serial.println("OK");
  } else if (!strcmp(cmd, "TRACE")) {
    trace::dump();
  } else if (!strncmp(cmd, "BOOT ", 5)) {
    app::button(!strcmp(cmd + 5, "long"));
    Serial.println("OK");
  } else if (!strcmp(cmd, "WIFIOFF")) {  // tests: the radio off (the mic's noise with and without it)
    net::radioOff();
    Serial.println("OK");
  } else if (!strncmp(cmd, "SPK ", 4)) {  // tests: the amp's I2S on or off
    spk::enable(atoi(cmd + 4) != 0);
    Serial.println("OK");
  } else if (!strncmp(cmd, "NETSIM ", 7)) {  // the simulator's router: down / up / newpass
    Serial.println(net::simulate(cmd + 7) ? "OK" : "ERR simulator only");
  } else if (!strcmp(cmd, "SETTINGS")) {  // the settings as the bot holds them (no secrets in them), and the local time
    JsonDocument d;
    for (auto& kv : store::settings()) d[kv.first] = kv.second;
    std::string s;
    serializeJson(d, s);
    Serial.printf("SETTINGS %s\n", s.c_str());
    Serial.printf("SCREEN %s\n", ui::standbyId());
  } else if (!strcmp(cmd, "PINS")) {  // the buttons as read now (0 = pressed): BOOT 0, Vol+ 40, Vol- 39
    Serial.printf("PINS boot %d volup %d voldown %d\n", digitalRead(pins::BOOT), digitalRead(pins::VOL_UP),
                  digitalRead(pins::VOL_DOWN));
  } else if (!strcmp(cmd, "REBOOT")) {
    ESP.restart();
  } else if (!strcmp(cmd, "HELP")) {
    Serial.println("ESPOS:HELLO|INFO|PROVISION|CLAIM|FACTORY_RESET, SHOT, STATE, UI <state ...>, FAKE <status> <json>, "
                   "MIC, MICGAIN <q8>, WAKEREC <s>, WAKELAST, WIFIOFF, SPK 0|1, SET <k> <v>, SYNC, TRACE, BOOT short|long, PINS, SETTINGS, REBOOT");
  } else if (*cmd) {
    Serial.println("ERR unknown (HELP)");
  }
}

}  // namespace

void loop() {
  static std::string line;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (!line.empty()) run(&line[0]);
      line.clear();
    } else if (line.size() < 8192) {
      line += c;
    }
  }
}

}  // namespace usb
}  // namespace bot
