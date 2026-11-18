#include "FontSync.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_attr.h>
#include <esp_rom_crc.h>
#include <esp_system.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "EspOSStore.h"
#include "FontInstaller.h"
#include "SdCardFontSystem.h"
#include "activities/settings/FontDownloadActivity.h"  // FONT_MANIFEST_URL
#include "network/HttpDownloader.h"

extern SdCardFontSystem sdFontSystem;

namespace espos::fonts {
namespace {

constexpr const char* MANIFEST_TMP = "/.crosspoint/espos_fonts.json";

bool crcOf(const char* path, uint32_t& out) {
  HalFile f;
  if (!Storage.openFileForRead("FONT", path, f)) return false;
  uint8_t buf[256];
  uint32_t crc = 0;
  while (f.available()) {
    const int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    crc = esp_rom_crc32_le(crc, buf, static_cast<uint32_t>(n));
  }
  out = crc;
  return true;
}

struct FileSpec {
  std::string name;
  size_t size = 0;
  uint32_t crc32 = 0;
};

// Survives silent restarts (RTC_NOINIT); reset on any other boot.
RTC_NOINIT_ATTR uint32_t s_magic;
RTC_NOINIT_ATTR uint32_t s_attempted;
constexpr uint32_t MAGIC = 0xF0A7F0A8;

void checkBoot() {
  static bool checked = false;
  if (checked) return;
  checked = true;
  if (esp_reset_reason() != ESP_RST_SW || s_magic != MAGIC) {
    s_magic = MAGIC;
    s_attempted = 0;
  }
}

}  // namespace

bool attempted() {
  checkBoot();
  return s_attempted != 0;
}

void markAttempted() {
  checkBoot();
  s_attempted = 1;
}

std::vector<std::string> installed() {
  std::vector<std::string> out;
  for (const auto& f : sdFontSystem.registry().getFamilies()) out.push_back(f.name);
  return out;
}

bool isInstalled(const std::string& family) {
  for (const auto& f : sdFontSystem.registry().getFamilies()) {
    if (f.name == family) return true;
  }
  return false;
}

bool select(const std::string& family) {
  if (family.empty() || family == BUILTIN) {
    if (SETTINGS.sdFontFamilyName[0] == '\0' && SETTINGS.fontFamily == CrossPointSettings::NOTOSANS) return false;
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
    return true;
  }
  if (!isInstalled(family) || family.size() >= sizeof(SETTINGS.sdFontFamilyName)) return false;
  if (family == SETTINGS.sdFontFamilyName) return false;
  strncpy(SETTINGS.sdFontFamilyName, family.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  return true;
}

std::string missing() {
  const std::string want = ESPOS.get("read.font", BUILTIN);
  if (want.empty() || want == BUILTIN || isInstalled(want)) return {};
  return FontInstaller::isValidFamilyName(want.c_str()) ? want : std::string();
}

bool install(const std::string& family, const std::function<void(size_t, size_t)>& progress, const bool* cancel) {
  if (!FontInstaller::isValidFamilyName(family.c_str())) return false;
  // The catalogue: over TLS, since its CRCs are what the files are checked against.
  // progress(0, 0) lets the caller poll Back while the catalogue arrives.
  const auto poll = [&progress](const size_t, const size_t) {
    if (progress) progress(0, 0);
  };
  if (HttpDownloader::downloadToFile(FONT_MANIFEST_URL, MANIFEST_TMP, poll, cancel) != HttpDownloader::OK) {
    LOG_ERR("FONT", "EspOS: font catalogue download failed");
    return false;
  }
  std::string baseUrl;
  std::vector<FileSpec> files;
  {
    HalFile mf;
    if (!Storage.openFileForRead("FONT", MANIFEST_TMP, mf)) return false;
    JsonDocument filter;
    filter["baseUrl"] = true;
    filter["families"][0]["name"] = true;
    filter["families"][0]["files"][0]["name"] = true;
    filter["families"][0]["files"][0]["size"] = true;
    filter["families"][0]["files"][0]["crc32"] = true;
    JsonDocument doc;
    const auto err = deserializeJson(doc, mf, DeserializationOption::Filter(filter));
    mf.close();
    Storage.remove(MANIFEST_TMP);
    if (err) {
      LOG_ERR("FONT", "EspOS: font catalogue unreadable: %s", err.c_str());
      return false;
    }
    baseUrl = doc["baseUrl"] | "";
    for (JsonObjectConst fam : doc["families"].as<JsonArrayConst>()) {
      if (family != (fam["name"] | "")) continue;
      for (JsonObjectConst f : fam["files"].as<JsonArrayConst>()) {
        FileSpec spec;
        spec.name = f["name"] | "";
        spec.size = f["size"] | 0u;
        spec.crc32 = f["crc32"] | 0u;
        if (FontInstaller::isValidCpfontFilename(spec.name.c_str())) files.push_back(std::move(spec));
      }
    }
  }
  if (baseUrl.empty() || files.empty()) {
    LOG_ERR("FONT", "EspOS: family %s not in the catalogue", family.c_str());
    return false;
  }

  FontInstaller installer(sdFontSystem.registry());
  if (!installer.ensureFamilyDir(family.c_str())) return false;
  size_t total = 0;
  for (const auto& f : files) total += f.size;
  size_t before = 0;
  for (const auto& f : files) {
    if (cancel && *cancel) {
      installer.deleteFamily(family.c_str());
      return false;
    }
    char dest[128];
    FontInstaller::buildFontPath(family.c_str(), f.name.c_str(), dest, sizeof(dest));
    // Plain HTTP after GitHub's redirect keeps a second TLS session off the small heap;
    // the CRC from the TLS-fetched catalogue guards the bytes.
    const auto r = HttpDownloader::downloadToFile(
        baseUrl + f.name, dest,
        [&](const size_t done, const size_t) {
          if (progress) progress(before + done, total);
        },
        cancel, "", "", {}, /*downgradeRedirectsToHttp=*/true);
    uint32_t crc = 0;
    if (r != HttpDownloader::OK || !crcOf(dest, crc) || crc != f.crc32 || !installer.validateCpfontFile(dest)) {
      LOG_ERR("FONT", "EspOS: %s failed (result %d)", f.name.c_str(), static_cast<int>(r));
      installer.deleteFamily(family.c_str());
      return false;
    }
    before += f.size;
  }
  installer.refreshRegistry();
  LOG_INF("FONT", "EspOS: installed font %s (%u files)", family.c_str(), static_cast<unsigned>(files.size()));
  if (ESPOS.get("read.font", BUILTIN) == family && select(family)) SETTINGS.saveToFile();
  return true;
}

}  // namespace espos::fonts
