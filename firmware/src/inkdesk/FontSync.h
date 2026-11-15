#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// Reading fonts picked on the InkDesk web (setting read.font). "notosans" is the built-in
// font; any other value is a family from the CrossPoint SD font catalogue (pre-rendered
// .cpfont files for e-ink), downloaded into /fonts/<Family>/ where the firmware reads them.
namespace inkdesk::fonts {

constexpr const char* BUILTIN = "notosans";

// Families installed on the SD card (names, as the registry knows them).
std::vector<std::string> installed();
bool isInstalled(const std::string& family);

// Makes `family` the reader font when it is the built-in one or already installed.
// Returns true when the setting changed. A family that is not installed yet is left for
// install() and selected afterwards.
bool select(const std::string& family);

// The family read.font asks for but the card does not have yet ("" when none).
std::string missing();

// The catalogue comes from GitHub over TLS and needs a larger contiguous block than the
// InkDesk calls: right after a sync the largest block was 47 KB and the download hung; on a
// fresh boot it is about 86 KB. Below this, reboot into the font screen first.
constexpr size_t MIN_BLOCK = 70000;

// One automatic attempt per power-up: Home does not start the download again after an
// attempt (or a cancel) until the next boot that is not a silent restart.
bool attempted();
void markAttempted();

// Downloads one family (manifest over TLS, files checked by CRC32), then selects it.
// `progress(done, total)` reports bytes across the family. Returns true on success.
bool install(const std::string& family, const std::function<void(size_t, size_t)>& progress = nullptr,
             const bool* cancel = nullptr);

}  // namespace inkdesk::fonts
