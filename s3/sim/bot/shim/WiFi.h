#pragma once
// The simulator has no radio: the bot is "online" through the computer's network (sim/bot/net_sim.cpp).
struct SimWiFi {
  int RSSI() { return -52; }
};
extern SimWiFi WiFi;
