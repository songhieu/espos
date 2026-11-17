#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

// Declarative faces and widgets from packages, format level 1 (s3desk/docs/profiles.md §4).
// The layout JSON is read into one document, drawn through a Canvas, and freed by the caller.
// No Arduino or HAL code here: test/face_render builds this file on a computer and draws PNGs.
namespace inkdesk::decl {

constexpr int LEVEL = 1;

// Colours resolved for a black-and-white screen: Gray is a 50 % dither for fills.
enum class Col : uint8_t { None, Ink, Paper, Gray };

enum class Family : uint8_t { Sans, Serif, Digits, Ui };

struct Font {
  int id = 0;
  Family family = Family::Sans;
  bool bold = false;
  int px = 0;      // font size (em) in pixels: what a layout's "size" asks for
  int lineH = 0;   // line height in pixels
  int inkTop = 0;  // digit fonts: empty rows above the digits in the line box
  int inkH = 0;    // digit fonts: the digits' height, used for placing (0: lineH)
};

class Canvas {
 public:
  virtual ~Canvas() = default;
  virtual void fillRect(int x, int y, int w, int h, int radius, Col c) = 0;
  virtual void strokeRect(int x, int y, int w, int h, int radius, int lw, Col c) = 0;
  virtual void line(int x1, int y1, int x2, int y2, int lw, Col c) = 0;
  virtual void fillCircle(int cx, int cy, int r, Col c) = 0;
  virtual void strokeCircle(int cx, int cy, int r, int lw, Col c) = 0;
  // A system icon (profiles.md §4.3) `size` px wide, centred on (cx, cy).
  virtual void icon(const char* name, int cx, int cy, int size, Col c) = 0;
  // A picture file fitted inside the box, centred. False when it cannot be drawn.
  virtual bool image(const std::string& path, int x, int y, int w, int h) = 0;
  // The fonts this screen has, any order.
  virtual const std::vector<Font>& fonts() const = 0;
  virtual int textWidth(const Font& f, const char* utf8) const = 0;
  // `y` is the top of the line.
  virtual void text(const Font& f, int x, int y, const char* utf8, Col c) = 0;
};

// Values for the bindings (profiles.md §4.4).
class Data {
 public:
  virtual ~Data() = default;
  virtual bool value(const char* name, std::string& out) const = 0;
  virtual int count(const char* list) const = 0;
  virtual bool field(const char* list, int index, const char* name, std::string& out) const = 0;
};

struct Stats {
  int drawn = 0;    // things drawn
  int skipped = 0;  // unknown items, failed conditions, over a limit
  int steps = 0;    // items visited, list rows (limited, like `drawn`, so any file draws in bounded time)
  long work = 0;    // pixels the drawing may have touched (limited)
  long measured = 0;  // characters measured while fitting texts (limited)
  int images = 0;     // pictures drawn (at most 8)
  int imagesSkipped = 0;  // pictures past that limit (the device logs it once)
  int w = 0, h = 0; // the layout picked
};

// Sizes of the layouts in the file, in file order.
struct Size {
  int w, h;
};
std::vector<Size> sizes(JsonVariantConst doc);

// The layout index that best fits a w x h target (same size, else the closest aspect ratio,
// ties to the bigger one), -1 when there is none.
int pick(JsonVariantConst doc, int w, int h);

// Draws layout `index` (-1: the best fit) into the target box. `perms`: the manifest's "perms";
// `dir`: the package folder (pictures). False when the file is not a level-1 layout file.
bool render(JsonVariantConst doc, int index, Canvas& c, const Data& d, const std::vector<std::string>& perms,
            const std::string& dir, int tx, int ty, int tw, int th, Stats* st = nullptr);

// UTF-8 upper and lower case for Latin, Vietnamese, Greek and Cyrillic letters.
std::string upper(const std::string& s);
std::string lower(const std::string& s);
// "THỨ BA" -> "Thứ Ba".
std::string title(const std::string& s);

}  // namespace inkdesk::decl
