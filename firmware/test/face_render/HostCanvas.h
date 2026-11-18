#pragma once

// A 1-bit screen on a computer for the declarative renderer (src/espos/Decl.h), drawn with the
// X4's own font files, so a face can be checked as a PNG without the device.

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "espos/Decl.h"

class HostCanvas : public espos::decl::Canvas {
 public:
  HostCanvas(int w, int h);
  ~HostCanvas() override;

  void fillRect(int x, int y, int w, int h, int radius, espos::decl::Col c) override;
  void strokeRect(int x, int y, int w, int h, int radius, int lw, espos::decl::Col c) override;
  void line(int x1, int y1, int x2, int y2, int lw, espos::decl::Col c) override;
  void fillCircle(int cx, int cy, int r, espos::decl::Col c) override;
  void strokeCircle(int cx, int cy, int r, int lw, espos::decl::Col c) override;
  void icon(const char* name, int cx, int cy, int size, espos::decl::Col c) override;
  bool image(const std::string& path, int x, int y, int w, int h) override;
  const std::vector<espos::decl::Font>& fonts() const override { return fonts_; }
  int textWidth(const espos::decl::Font& f, const char* utf8) const override;
  void text(const espos::decl::Font& f, int x, int y, const char* utf8, espos::decl::Col c) override;

  int width() const { return w_; }
  int height() const { return h_; }
  bool ink(int x, int y) const { return px_[static_cast<size_t>(y) * w_ + x] != 0; }
  void invert(int x, int y, int w, int h);
  // Pixels something tried to draw outside the screen.
  long outside() const { return outside_; }
  // Pixel writes asked for, on or off the screen (the renderer's work).
  long touched() const { return touched_; }
  // Text drawn, in order (tests read it).
  const std::vector<std::string>& texts() const { return texts_; }
  bool writePng(const std::string& path) const;

 private:
  void set(int x, int y, bool on);
  void put(int x, int y, espos::decl::Col c);
  bool inRound(int x, int y, int rx, int ry, int rw, int rh, int r) const;

  int w_, h_;
  std::vector<unsigned char> px_;  // 1 = ink
  long outside_ = 0;
  long touched_ = 0;
  std::vector<espos::decl::Font> fonts_;
  std::vector<std::string> texts_;
};

// Binding values from a JSON fixture: {"values": {"time.hm": "07:45", ...}, "lists": {"cal.events": [{...}]}}.
class JsonData : public espos::decl::Data {
 public:
  explicit JsonData(JsonVariantConst root) : root_(root) {}
  bool value(const char* name, std::string& out) const override;
  int count(const char* list) const override;
  bool field(const char* list, int index, const char* name, std::string& out) const override;

 private:
  JsonVariantConst root_;
};
