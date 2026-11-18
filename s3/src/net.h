#pragma once
#include <Arduino.h>

#include <vector>

// WiFi: saved networks (NVS, most recently used first), background scans, and joining the
// strongest saved network on its own like a phone. Nothing here blocks the UI loop.
namespace net {

enum class State { Off, Idle, Connecting, Connected };

struct Ap {
  String ssid;
  int rssi;
  bool secure;
  bool saved;
};

void begin();
void poll();  // call from loop()

// Scanning (results merged by name, strongest first).
void scan();
bool scanning();
const std::vector<Ap>& results();
uint32_t resultsAt();  // millis() when the last scan finished, 0 = never

// Joining. A user connect saves the network once it gets an address.
void connect(const String& ssid, const String& pass);
void connectSaved(const String& ssid);
void disconnect();
void resumeAuto();  // join the saved networks again now (after a test of another network)
void forget(const String& ssid);
bool isSaved(const String& ssid);
std::vector<String> savedNames();                       // most recently used first
void addSaved(const String& ssid, const String& pass);  // from the web or USB install; joins soon
int savedCount();

State state();
String ssid();       // the network joined or being joined
String ip();
int rssi();
const char* error();  // last failure in Vietnamese, "" when none; cleared by the next connect
int lastReason();     // WiFi reason code of the last refusal (0 = timeout or none)
uint32_t changes();   // bumps on every state change, for screens to refresh

int bars(int rssi);   // 1..4

}  // namespace net
