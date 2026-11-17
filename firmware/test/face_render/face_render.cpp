// Draws a declarative face or widget (s3desk/docs/profiles.md §4) the way the X4 does, into a PNG.
//
//   face_render PKG_DIR_OR_JSON [--size 800x480] [--layout N] [--data fixture.json] [--out out.png]
//               [--widget WxH] [--perms data:weather,data:calendar]
//
// PKG_DIR: a package folder (manifest.json + its entry); the perms come from the manifest.
// --widget: draws it as a Home widget: a card frame of that size on a white page, the layout inset 6 px.
// Prints what it drew and exits 1 when the file is not a level-1 layout or something fell off the screen.

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "HostCanvas.h"

namespace {

bool readFile(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

bool isDir(const std::string& p) {
  std::ifstream in(p + "/manifest.json");
  return static_cast<bool>(in);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: face_render PKG_DIR|LAYOUT.json [--size WxH] [--layout N] [--data F] [--out F] "
                    "[--widget WxH] [--perms a,b]\n");
    return 2;
  }
  std::string src = argv[1], dataPath, out = "face.png", permArg;
  int sw = 800, sh = 480, layout = -1, ww = 0, wh = 0;
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--size") sscanf(v.c_str(), "%dx%d", &sw, &sh);
    else if (k == "--layout") layout = atoi(v.c_str());
    else if (k == "--data") dataPath = v;
    else if (k == "--out") out = v;
    else if (k == "--widget") sscanf(v.c_str(), "%dx%d", &ww, &wh);
    else if (k == "--perms") permArg = v;
  }

  std::string dir = ".", layoutPath = src;
  std::vector<std::string> perms;
  if (isDir(src)) {
    dir = src;
    std::string raw;
    readFile(src + "/manifest.json", raw);
    JsonDocument m;
    if (deserializeJson(m, raw)) {
      fprintf(stderr, "manifest.json is not JSON\n");
      return 1;
    }
    for (JsonVariantConst p : m["perms"].as<JsonArrayConst>()) perms.push_back(p | "");
    const char* entry = m["entry"]["face"] | (m["entry"]["widget"] | "face.json");
    layoutPath = src + "/" + entry;
  }
  for (size_t a = 0; a < permArg.size();) {
    size_t b = permArg.find(',', a);
    if (b == std::string::npos) b = permArg.size();
    perms.push_back(permArg.substr(a, b - a));
    a = b + 1;
  }

  std::string raw;
  if (!readFile(layoutPath, raw)) {
    fprintf(stderr, "cannot read %s\n", layoutPath.c_str());
    return 1;
  }
  JsonDocument doc;
  if (DeserializationError e = deserializeJson(doc, raw)) {
    fprintf(stderr, "%s: %s\n", layoutPath.c_str(), e.c_str());
    return 1;
  }
  JsonDocument fixture;
  if (!dataPath.empty()) {
    std::string fr;
    if (!readFile(dataPath, fr) || deserializeJson(fixture, fr)) {
      fprintf(stderr, "cannot read the data fixture %s\n", dataPath.c_str());
      return 1;
    }
  }
  JsonData data(fixture.as<JsonVariantConst>());

  inkdesk::decl::Stats st;
  bool ok;
  int cw = sw, ch = sh;
  if (ww > 0) {
    cw = ww + 40;
    ch = wh + 40;
  }
  HostCanvas canvas(cw, ch);
  if (ww > 0) {
    // A Home card: the OS frame (Card skin: 2 px, radius 12), the layout inside it.
    canvas.strokeRect(20, 20, ww, wh, 12, 2, inkdesk::decl::Col::Ink);
    ok = inkdesk::decl::render(doc.as<JsonVariantConst>(), layout, canvas, data, perms, dir, 26, 26, ww - 12,
                               wh - 12, &st);
  } else {
    ok = inkdesk::decl::render(doc.as<JsonVariantConst>(), layout, canvas, data, perms, dir, 0, 0, sw, sh, &st);
  }
  if (!ok) {
    fprintf(stderr, "%s: not a level-%d layout file\n", layoutPath.c_str(), inkdesk::decl::LEVEL);
    return 1;
  }
  canvas.writePng(out);
  printf("%s: layout %dx%d on %dx%d, %d drawn, %d skipped, %ld px outside\n", out.c_str(), st.w, st.h,
         ww > 0 ? ww : sw, ww > 0 ? wh : sh, st.drawn, st.skipped, canvas.outside());
  for (const auto& t : canvas.texts()) {
    if (t.find("\xE2\x80\xA6") != std::string::npos) {
      fprintf(stderr, "text with an ellipsis: %s\n", t.c_str());
      return 1;
    }
  }
  return canvas.outside() > 0 ? 1 : 0;
}
