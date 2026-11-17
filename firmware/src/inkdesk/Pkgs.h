#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

// Packages on the X4 (the Lite profile, s3desk/docs/profiles.md): declarative faces and widgets kept
// on the card in /InkDesk/pkgs/<id>/ (the package's files + manifest.json with the file list), installed
// from .inkp files the sync names, or copied onto the card.
namespace inkdesk::pkgs {

constexpr const char* DIR = "/InkDesk/pkgs";
constexpr size_t LAYOUT_CAP = 12 * 1024;  // face.json / widget.json

struct Info {
  std::string id, name, version, type, refresh, entry;
  uint32_t code = 0;
  uint32_t kb = 0;
  std::vector<std::string> screens;  // "800x480"
  std::vector<std::string> perms;
  bool card = false;  // copied onto the card by hand, not from the store
};

// A face as dash.layout names it: "pkg:<id>" or "pkg:<id>@<w>x<h>" (that layout of the face).
struct FaceRef {
  std::string id;
  int w = 0, h = 0;  // 0: the face's first layout
  static bool parse(const std::string& layout, FaceRef& out);  // false for the built-in styles
  static bool parseSize(const std::string& size, int& w, int& h);  // "480x800"
  std::string format() const;
  bool portrait() const { return h > w; }
};

std::string dirOf(const std::string& id);
// Installed packages (at most 32): faces, then widgets, then the rest; the store's before the copied ones.
std::vector<Info> list();
bool get(const std::string& id, Info& out);
// Removes the package's folder. `wasFace`: it was the sleep face in use (the caller calls leaveFace()).
bool remove(const std::string& id, bool* wasFace = nullptr);
// Takes the package out of x4.pkgs (removed by the owner on the device, so the web does not put it back).
void forget(const std::string& id);
// "Dùng làm màn chờ": the face becomes the sleep screen; the drawn sleep screen is turned on, and the
// pictures mode switches to it (owner decision, profiles.md). Saves the store.
void useFace(const FaceRef& ref);
// The sleep style goes back to the built-in "desk" (its face was removed).
void leaveFace();
// .inkp files copied into /InkDesk/pkgs and not unpacked yet.
int sideloadPending();
// Unpacks them (no SHA-256: they come with none). Returns how many installed.
int sideload();

// The refresh hint of the face that dash.layout names ("minute", "hour", "data", "day"), "" when none.
std::string faceRefresh();

// ---- Sync (profiles.md §9) ----
struct Download {
  std::string id, name, version, url, sha256;
  uint32_t code = 0, size = 0;
};
// "caps", "rt", "screen" and "pkgs" (at most 32 installed) for the sync body.
void addToSyncBody(JsonObject req);
// After the server's settings are applied: removes the packages x4.pkgs marks "rm" (and their entries,
// sent back on the next sync); a removed face in use falls back to "desk".
void applyRemovals();
// The packages of the answer's "pkgs" (`answered`: the key was there) that x4.pkgs wants and that are
// missing or older than its "code" asks.
std::vector<Download> plan(JsonArrayConst answer, bool answered);
// Downloads and installs them one by one (a download stops at the answer's size, or at the largest
// package when it gave none). Returns how many failed.
int install(const std::vector<Download>& list, const bool* cancel);

}  // namespace inkdesk::pkgs
