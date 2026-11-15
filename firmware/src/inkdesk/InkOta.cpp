#include "InkOta.h"

#include <Logging.h>
#include <ResumableFetch.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstring>

#include "InkDeskStore.h"
#include "network/FirmwareFlasher.h"
#include "network/WifiPowerSaveGuard.h"

// Arduino confirms a new OTA image as soon as it starts; InkDesk confirms it later, once the
// image has shown it works (confirmRunning), so a broken build rolls back on its own.
extern "C" bool verifyRollbackLater() { return true; }

namespace inkdesk::ota {
namespace {

constexpr int DOWNLOAD_ROUNDS = 6;
constexpr int HTTP_TIMEOUT_MS = 30000;

void report(const std::string& version, const bool ok, const char* why) {
  inkdesk::OtaReport r;
  r.version = version;
  r.ok = ok;
  r.why = why ? why : "";
  r.atMs = InkDeskStore::clockValid() ? InkDeskStore::nowMs() : 0;
  if (INKDESK.otaReports.size() >= 4) INKDESK.otaReports.erase(INKDESK.otaReports.begin());
  INKDESK.otaReports.push_back(std::move(r));
}

Result fail(const Result result, const char* why) {
  LOG_ERR("OTA", "InkDesk update failed: %s", why);
  report(INKDESK.fwOffer.version, false, why);
  // The web asks again if it wants another try: no automatic retry loop.
  INKDESK.fwOffer.install = false;
  INKDESK.saveToFile();
  return result;
}

void toHex(const uint8_t* in, const size_t n, char* out) {
  for (size_t i = 0; i < n; i++) snprintf(out + i * 2, 3, "%02x", in[i]);
}

}  // namespace

int versionCode(const std::string& version) {
  const size_t dash = version.find_last_of('-');
  const char* v = version.c_str() + (dash == std::string::npos ? 0 : dash + 1);
  int major = 0, minor = 0, patch = 0;
  if (sscanf(v, "%d.%d.%d", &major, &minor, &patch) < 2) return 0;
  return major * 10000 + minor * 100 + patch;
}

std::string shortVersion(const std::string& version) {
  const size_t dash = version.find_last_of('-');
  return dash == std::string::npos ? version : version.substr(dash + 1);
}

bool offerAvailable() {
  const auto& o = INKDESK.fwOffer;
  return !o.version.empty() && !o.url.empty() && versionCode(o.version) > versionCode(FW_VERSION);
}

Result install(const std::function<void(size_t, size_t)>& progress, const std::function<bool()>& connect) {
  if (!offerAvailable()) return Result::NoOffer;
  const inkdesk::FwOffer offer = INKDESK.fwOffer;
  const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
  if (!slot) return fail(Result::Flash, "no slot");
  if (offer.size > slot->size) return fail(Result::WrongImage, "too big");

  esp_ota_handle_t handle = 0;
  // A known size erases only what the image needs instead of the whole 6.4 MB slot.
  if (esp_ota_begin(slot, offer.size > 0 ? offer.size : OTA_SIZE_UNKNOWN, &handle) != ESP_OK) {
    return fail(Result::Flash, "begin");
  }
  LOG_INF("OTA", "InkDesk update %s -> slot %s (%u bytes)", offer.version.c_str(), slot->label,
          static_cast<unsigned>(offer.size));
  if (!connect()) {
    esp_ota_abort(handle);
    return fail(Result::Network, "wifi");
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  size_t done = 0;
  bool flashOk = true;
  bool wrongChip = false;
  uint8_t header[14];
  size_t headerLen = 0;
  freeink::FetchSink sink;
  sink.write = [&](const uint8_t* data, const size_t len) {
    // Bytes 12-13 of the image header name the chip: refuse an image for another MCU.
    if (headerLen < sizeof(header)) {
      const size_t take = std::min(len, sizeof(header) - headerLen);
      memcpy(header + headerLen, data, take);
      headerLen += take;
      if (headerLen == sizeof(header)) {
        uint16_t imageChip;
        memcpy(&imageChip, header + 12, sizeof(imageChip));
        const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
        if (deviceChip != 0xFFFF && imageChip != deviceChip) {
          wrongChip = true;
          return false;
        }
      }
    }
    mbedtls_sha256_update(&sha, data, len);
    if (esp_ota_write(handle, data, len) != ESP_OK) {
      flashOk = false;
      return false;
    }
    done += len;
    if (progress) progress(done, offer.size);
    return true;
  };
  // What is in the slot cannot be taken back: a server that ignores Range fails the install.
  sink.rewind = [] { return false; };

  // fetchResumable already resumes a cut body a few times; these rounds also outlast a WiFi
  // drop (its own retries fail at once without WiFi), rejoining before each one.
  bool complete = false;
  int status = 0;
  for (int round = 0; round < DOWNLOAD_ROUNDS && !complete; round++) {
    if (round > 0) {
      LOG_INF("OTA", "Resuming at %u of %u bytes (round %d)", static_cast<unsigned>(done),
              static_cast<unsigned>(offer.size), round + 1);
      delay(1500);
      if (WiFi.status() != WL_CONNECTED && !connect()) continue;
    }
    WifiPowerSaveGuard psGuard;
    freeink::FetchOptions options;
    options.startOffset = done;
    const freeink::FetchResult r = freeink::fetchResumable(
        offer.url, options,
        [](freeink::SecureHttpClient& http, bool) {
          http.setTimeout(HTTP_TIMEOUT_MS);
          http.setInsecure();  // the SHA-256 from the sync (over TLS) checks the image
          http.setUserAgent(std::string("InkDesk-X4/") + FW_VERSION);
        },
        sink);
    status = r.status;
    complete = r.complete;
    if (r.stopped || (r.status >= 400 && r.status < 500)) break;  // retrying will not help
  }
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);

  if (wrongChip || !complete || !flashOk) {
    esp_ota_abort(handle);
    if (wrongChip) return fail(Result::WrongImage, "wrong chip");
    if (!flashOk) return fail(Result::Flash, "write");
    // "download: HTTP <status>, <bytes>/<size> B" tells where it stopped (status -1: no answer).
    char why[64];
    snprintf(why, sizeof(why), "download: HTTP %d, %u/%u B", status, static_cast<unsigned>(done),
             static_cast<unsigned>(offer.size));
    return fail(Result::Network, why);
  }
  char hex[65];
  toHex(digest, sizeof(digest), hex);
  if ((offer.size > 0 && done != offer.size) || strcasecmp(hex, offer.sha256.c_str()) != 0) {
    esp_ota_abort(handle);
    LOG_ERR("OTA", "Got %u bytes, sha %s", static_cast<unsigned>(done), hex);
    return fail(Result::Hash, "checksum");
  }
  if (esp_ota_end(handle) != ESP_OK) return fail(Result::WrongImage, "image check");
  if (esp_ota_set_boot_partition(slot) != ESP_OK) return fail(Result::Flash, "select");

  INKDESK.otaTarget = offer.version;
  INKDESK.fwOffer.install = false;
  INKDESK.saveToFile();
  LOG_INF("OTA", "InkDesk update %s written, restarting", offer.version.c_str());
  return Result::Ok;
}

void checkAfterBoot() {
  if (INKDESK.otaTarget.empty()) return;
  const std::string target = INKDESK.otaTarget;
  INKDESK.otaTarget.clear();
  if (target == FW_VERSION) {
    report(target, true, "");
    INKDESK.justUpdated = shortVersion(target);
    INKDESK.fwOffer = {};
    LOG_INF("OTA", "Now running %s", FW_VERSION);
  } else {
    // The new image did not confirm itself: the bootloader went back to this one.
    report(target, false, "rollback");
    LOG_ERR("OTA", "Update to %s rolled back, running %s", target.c_str(), FW_VERSION);
  }
  INKDESK.saveToFile();
}

void confirmRunning() {
  static bool done = false;
  if (done) return;
  done = true;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    LOG_INF("OTA", "Firmware %s confirmed", FW_VERSION);
  }
}

}  // namespace inkdesk::ota
