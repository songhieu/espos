#pragma once
// EspOS Bot's setup page (its own WiFi "EspOS-XXXX", or http://localhost:8099 in the simulator): pick a network,
// its password and the language. Plain HTML forms, no script (phones' captive-portal windows), in Vietnamese and
// English; the password never comes back in a page. After "Kết nối" the page follows the join by itself
// (/status refreshes every 2 s): connecting, then connected or why not (with the form again).
#include <string>
#include <vector>

namespace bot {
namespace portal {

enum class Join : unsigned char { None, Joining, Ok, Failed };
enum Why : unsigned char { WHY_NONE, WHY_PASSWORD, WHY_NOT_FOUND, WHY_OTHER };

// The form; msg: a line above it (the last try's error).
std::string page(const std::vector<std::string>& networks, const std::string& msg);
// The join's page for its state (Failed: the form again, with why).
std::string status(Join j, const std::string& ssid, Why why, const std::vector<std::string>& networks);
// A "Kết nối": the language is saved; ssid/pass are what to try ("" = none chosen).
void form(const std::string& picked, const std::string& typed, const std::string& pass, const std::string& lang,
          std::string& ssid, std::string& passOut);
std::string whyText(Why why, const std::string& ssid);  // both languages, for the page

}  // namespace portal
}  // namespace bot
