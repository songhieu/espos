#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "EspOSClient.h"
#include "espos/Pkgs.h"

// The data half of a sync: build the request from local state, apply the
// response (settings, tasks, dashboard, clock, device info) and return what is
// left to do on the SD card (books to download or delete).
namespace espos::sync {

struct BookDownload {
  int32_t id = 0;
  std::string title;
  std::string file;  // sanitized file name inside BOOKS_DIR
  std::string url;
  uint32_t size = 0;
};

struct WallDownload {
  int32_t id = 0;
  std::string url;
};

struct Outcome {
  client::Result result = client::Result::Network;
  int settingsSent = 0;
  int settingsApplied = 0;
  std::vector<BookDownload> downloads;
  std::vector<std::string> deletions;  // full paths of removed books
  std::vector<WallDownload> walls;     // sleep-screen pictures to fetch
  int wifiApplied = 0;                 // WiFi networks added or removed from the web
  std::string fontWanted;              // reading font chosen on the web, not on the card yet
  std::vector<pkgs::Download> pkgs;    // packages x4.pkgs asks for that are missing or older
};

// POST /v1/device/sync and apply everything except the book files.
// Heavy JSON is freed before returning.
Outcome run();

// The files a sync names besides the books: photo-widget pictures, sleep-screen pictures, then the
// packages to install. Returns how many failed. Called by the sync screen and, with nobody watching, by
// Home's automatic sync and by the timer-wake loop in main.cpp (through downloadAll): keep it free of UI.
int downloadFiles(const Outcome& outcome, const bool* cancel = nullptr);
// Applies book deletions and fetches books and pictures without any UI (sleep loop, Home).
// Returns how many downloads failed.
int downloadAll(const Outcome& outcome, const bool* cancel = nullptr);

// GET /v1/device/dashboard into the cache. Returns Ok on success.
client::Result refreshDashboard();

// Sets the system clock from a server wall clock (ms) when it is off by >30 s.
void adoptServerClock(int64_t nowMs);

// Turns a server file name into a safe FAT name ("book-<id>.epub" fallback).
std::string sanitizeFileName(const std::string& name, int32_t id);

}  // namespace espos::sync
