#pragma once
// EspOS Bot's NVS ("bot"): the link to espos.dev (token, api, device), the claim code of a unit for sale, the
// settings as the server knows them plus the ones changed here and not yet synced, and the saved WiFi networks.
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

namespace bot {
namespace store {

void begin();

// The link
std::string token();  // 64 hex, "" = never provisioned
std::string api();
void setLink(const std::string& token, const std::string& api, int devId, const std::string& devName);
int deviceId();
std::string claimCode();  // 8 letters, "" = none (or already claimed)
std::string claimUrl();
void setClaim(const std::string& code, const std::string& url);

// Settings (string values, the server's keys)
const std::map<std::string, std::string>& settings();
std::string setting(const std::string& k);  // the value or the firmware's default (logic::defaults)
void set(const std::string& k, const std::string& v, bool dirty);  // dirty: changed here, goes up on the next sync
std::vector<std::pair<std::string, uint32_t>> dirty();  // key, millis() when changed (0: before this boot)
void settle(uint32_t before);  // the sync that started at `before` took the changes made before it
int64_t rev();
void setRev(int64_t r);
uint32_t version();  // changes with every setting change
bool langChosen();
void setLangChosen();

// WiFi (most recent first, at most 5)
struct Wifi {
  std::string ssid, pass;
};
std::vector<Wifi> wifi();
void addWifi(const std::string& ssid, const std::string& pass);  // first in the list
void forgetWifi(const std::string& ssid);                         // "" = all

// The setup WiFi's WPA2 password: 8 random digits, made once per device and kept (shown on the OLED with its QR).
std::string apPass();

// FACTORY_RESET: the person's things go (settings, WiFi, language), the identity stays (token, claim code).
void factoryReset();

}  // namespace store
}  // namespace bot
