// Not speed-critical (big copies are memcpy, hashing is in hardware): built for size, the app slot is full.
#pragma GCC optimize("Os")
// The memory card ("Thẻ nhớ"): SD_MMC 4-bit, FAT, mounted at /sdcard. There is no card-detect pin: a task
// tries to mount every few seconds when there is no card, and asks a mounted card for its status otherwise.
// Packages live in /espos/pkgs/<id>/ with their manifest.json (no index). Simulator: the folder $SIM_DATA/sd
// is the card (no folder = no card; the setting sim.card = 0 pulls it out).
#include <atomic>

#include "internal.h"
#include "trace.h"

#ifdef SIM
#include <string>

#include "desk.h"
extern std::string simDir;
#else
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "hal/board.h"
#include "diskio_sdmmc.h"  // after ff.h (BYTE)
#include "sdmmc_cmd.h"
#endif

namespace pkg {
namespace in {
namespace card {
namespace {

std::atomic<bool> mounted{false}, badCard{false}, busy{false};
std::atomic<uint32_t> ver{1};
std::atomic<uint64_t> totalB{0}, freeB{0};

// A scan of the card's packages, made where the card was mounted, applied on the UI loop.
std::mutex smu;
bool scanPending = false, gonePending = false;
std::vector<Rec> scanned;

void scanNow() {
  std::vector<Rec> found = scanRoot(sdRoot(), SD);
  // Leftovers of an install or a move that did not finish.
  for (const String& n : listDir(sdRoot()))
    if (n.startsWith(".new-") || n.startsWith(".old-")) rmTree(sdRoot() + "/" + n);
  std::lock_guard<std::mutex> l(smu);
  scanned = found;
  scanPending = true;
}

#ifdef SIM
uint32_t lastCheck = 0;

bool simPresent() { return isDir(String(simDir.c_str()) + "/sd") && desk::setting("sim.card", "1") != "0"; }

void refreshInfo() {
  const char* mb = getenv("SIM_SD_MB");
  const uint64_t total = (uint64_t)(mb ? atoi(mb) : 8192) << 20;
  const uint64_t used = treeBytes(String(simDir.c_str()) + "/sd");
  totalB = total;
  freeB = used < total ? total - used : 0;
}
#else
sdmmc_card_t* sd = nullptr;

void refreshInfo() {
  FATFS* fs = nullptr;
  DWORD fre = 0;
  char drv[4] = {(char)('0' + ff_diskio_get_pdrv_card(sd)), ':', 0, 0};
  if (f_getfree(drv, &fre, &fs) != FR_OK || !fs) return;
  const uint64_t cl = (uint64_t)fs->csize * 512;
  totalB = (uint64_t)(fs->n_fatent - 2) * cl;
  freeB = (uint64_t)fre * cl;
}

void tryMount() {
  const hal::CardDesc& c = hal::board().card;  // the board's slot (hal/storage.h)
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = c.width;
  slot.clk = (gpio_num_t)c.clk;
  slot.cmd = (gpio_num_t)c.cmd;
  slot.d0 = (gpio_num_t)c.d0;
  slot.d1 = (gpio_num_t)c.d1;
  slot.d2 = (gpio_num_t)c.d2;
  slot.d3 = (gpio_num_t)c.d3;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_vfs_fat_sdmmc_mount_config_t cfg = {};
  cfg.format_if_mount_failed = false;
  cfg.max_files = 6;
  cfg.allocation_unit_size = 16 * 1024;
  esp_err_t r = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &cfg, &sd);
  // A board whose data lines are not yet checked (the 2.8" board's page lists them without saying which is which):
  // when 4 lines get no answer, the card is tried on D0 alone (slower, but it works whatever the others are).
  if (c.try1bit && r != ESP_OK && r != ESP_FAIL) {
    slot.width = 1;
    r = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &cfg, &sd);
    static bool told = false;
    if (r == ESP_OK && !told) told = true, trace::add("card: 1 data line (4 did not answer)");
  }
  if (r == ESP_OK) {
    badCard = false;
    refreshInfo();
    trace::add("card: mounted, %u MB, %u MB free", (unsigned)(totalB >> 20), (unsigned)(freeB >> 20));
    mkdirs(sdRoot());
    scanNow();
    mounted = true;
    ver++;
  } else {
    sd = nullptr;
    // ESP_FAIL: the card answered but has no FAT file system we can read.
    if ((r == ESP_FAIL) != badCard) {
      badCard = r == ESP_FAIL;
      if (badCard) trace::add("card: cannot read its file system (needs FAT32)");
      ver++;
    }
  }
}

void unmount() {
  closeHandles("/sdcard/");
  esp_vfs_fat_sdcard_unmount("/sdcard", sd);
  sd = nullptr;
  mounted = false;
  {
    std::lock_guard<std::mutex> l(smu);
    gonePending = true;
  }
  trace::add("card: removed");
  ver++;
}

void probeTask(void*) {
  for (const char* tag : {"sdmmc_common", "sdmmc_sd", "sdmmc_cmd", "sdmmc_init", "sdmmc_req", "sdmmc_io", "vfs_fat_sdmmc",
                          "sdmmc_periph", "sdmmc"})
    esp_log_level_set(tag, ESP_LOG_NONE);
  vTaskDelay(pdMS_TO_TICKS(1500));
  for (;;) {
    if (!mounted) tryMount();
    else if (!busy && sdmmc_get_status(sd) != ESP_OK) unmount();
    vTaskDelay(pdMS_TO_TICKS(mounted ? 2500 : 4000));
  }
}
#endif

}  // namespace

void begin() {
#ifdef SIM
  if (simPresent()) {
    mkdirs(sdRoot());
    refreshInfo();
    scanNow();
    mounted = true;
  }
  lastCheck = millis();
#else
  if (!hal::has(hal::CAP_SDCARD) || hal::board().card.bus != hal::CardBus::SDMMC) return;  // no slot
  // The stack is in PSRAM: this task never touches the internal flash.
  xTaskCreatePinnedToCoreWithCaps(probeTask, "card", 4096, nullptr, 1, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

void poll() {
#ifdef SIM
  if (millis() - lastCheck > 1500) {
    lastCheck = millis();
    const bool now = simPresent();
    if (now && !mounted) {
      mkdirs(sdRoot());
      refreshInfo();
      scanNow();
      mounted = true;
      ver++;
      trace::add("card: in (sim)");
    } else if (!now && mounted && !busy) {
      closeHandles(String(simDir.c_str()) + "/sd/");
      mounted = false;
      {
        std::lock_guard<std::mutex> l(smu);
        gonePending = true;
      }
      ver++;
      trace::add("card: out (sim)");
    } else if (now && mounted) {
      static uint32_t lastInfo = 0;
      if (millis() - lastInfo > 5000) lastInfo = millis(), refreshInfo();
    }
  }
#endif
  // Apply what the probe found (UI loop).
  std::vector<Rec> found;
  bool gotScan = false, gone = false;
  {
    std::lock_guard<std::mutex> l(smu);
    if (scanPending) found.swap(scanned), gotScan = true, scanPending = false;
    gone = gonePending;
    gonePending = false;
  }
  if (!gotScan && !gone) return;
  {
    std::lock_guard<std::recursive_mutex> l(mu);
    if (gone)
      for (Rec& r : recs)
        if (r.p.vol == SD) r.p.available = false;
    if (gotScan) {
      for (auto it = recs.begin(); it != recs.end();) it = it->p.vol == SD ? recs.erase(it) : std::next(it);
      for (Rec& r : found) {
        bool clash = false;
        for (const Rec& o : recs) clash |= o.p.id == r.p.id;  // the same app on Máy wins
        if (!clash) recs.push_back(r);
      }
      // Remember them, so they stay on Home (greyed) while the card is out.
      JsonDocument d;
      JsonArray a = d.to<JsonArray>();
      for (const Rec& r : recs)
        if (r.p.vol == SD) {
          JsonObject m = a.add<JsonObject>();
          toManifest(r.p, m);
          m["kb"] = (r.p.bytes + 1023) / 1024;
          JsonArray parts = m["parts"].to<JsonArray>();
          for (const String& s : r.p.parts) parts.add(s);
        }
      String out;
      serializeJson(d, out);
      writeText(sdCacheFile(), out);
    }
  }
  changed();
}

bool present() { return mounted; }
bool bad() { return badCard && !mounted; }
uint32_t version() { return ver; }

bool info(uint64_t& total, uint64_t& f) {
  if (!mounted) return false;
  total = totalB, f = freeB;
  return true;
}

void setBusy(bool b) {
  if (b) busy = true;
#ifndef SIM
  if (!b && mounted && sd) refreshInfo();
#else
  if (!b && mounted) refreshInfo();
#endif
  if (!b) busy = false;
}

}  // namespace card
}  // namespace in
}  // namespace pkg
