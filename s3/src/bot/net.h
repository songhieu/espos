#pragma once
// EspOS Bot's WiFi: the saved networks (store.h) tried in turn; with none saved (or none found for a while) its own
// WiFi "EspOS-XXXX" (the MAC's last 4 hex digits; WPA2, 8 random digits per device, shown on the OLED with a QR) with a page that picks a network and the language: a phone
// that joins is shown it by itself (captive portal: every name resolves to 192.168.4.1). The clock from NTP.
#include <stdint.h>

#include <string>

#include "portal.h"

namespace bot {
namespace net {

void begin();
void loop();
bool online();             // connected with an address
bool portal();             // the setup WiFi is on
std::string apName();      // "EspOS-7E38"
std::string ssid();        // the network it is on
void startPortal();        // BOOT held 5 s (after forgetting the WiFi), or nothing saved
// Offline: how long (0 = online; from boot until the first join), the network it lost (or the first saved), the
// saved network that refuses its password ("" = none), full rounds of the saved networks that failed.
uint32_t offlineMs();
std::string lostName();
std::string badPassword();
int failedRounds();
bool simulate(const char* what);  // the simulator: "down" / "up" / "newpass" (the router); false on the board
// The setup page's join (the OLED mirrors it): its state, why it failed, the network, how long ago it failed.
portal::Join joinState();
portal::Why joinWhy();
std::string joinName();
uint32_t joinFailedAgo();
void reconnect();          // the saved list changed (USB, the web's wifi_ops)
std::string mac12();       // the base MAC, 12 lowercase hex (never sent: hwid() is its hash)
bool timeValid();
int64_t nowMs();           // UTC ms, 0 until the clock is known
void adoptClock(int64_t ms);
void radioOff();           // tests: WiFi off until a restart  // the server's or the USB host's time (when NTP has not answered)

}  // namespace net
}  // namespace bot
