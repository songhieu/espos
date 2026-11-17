// The declarative renderer (src/inkdesk/Decl.cpp, s3desk/docs/profiles.md §4) drawn with the X4's fonts.

#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "HostCanvas.h"

using inkdesk::decl::render;

namespace {

const char* kData = R"({"values": {
  "time.hm": "07:45", "time.live": "1", "date.d": "17", "date.dd": "17", "date.mm": "11", "date.y": "2026",
  "date.yday": "321", "date.wd": "Thứ Ba", "date.month": "Tháng 11", "date.dm": "17/11", "lunar.dm": "6/10",
  "lunar.year": "Bính Ngọ", "battery.pct": "82", "sync.hm": "07:40", "weather.ok": "1", "weather.temp": "28",
  "weather.min": "24", "weather.max": "31", "weather.rain": "40", "weather.desc": "Nhiều mây",
  "weather.icon": "cloudsun", "weather.city": "Hà Nội", "cal.count": "4", "cal.next.time": "09:00",
  "cal.next.title": "Họp nhóm thiết kế"},
 "lists": {
  "weather.days": [{"d": "T4", "min": 23, "max": 29, "icon": "rain"}, {"d": "T5", "min": 22, "max": 28, "icon": "cloud"},
                   {"d": "T6", "min": 24, "max": 31, "icon": "sun"}],
  "cal.events": [{"time": "09:00", "title": "Họp nhóm thiết kế"}, {"time": "12:00", "title": "Ăn trưa với Lan"},
                 {"time": "15:30", "title": "Gọi cho nhà in về bìa sách mới của bộ truyện thiếu nhi"},
                 {"time": "19:00", "title": "Lớp yoga"}]}})";

struct Fixture {
  JsonDocument data, doc;
  explicit Fixture(const std::string& layout) {
    deserializeJson(data, kData);
    deserializeJson(doc, layout);
  }
  // Draws and returns the texts drawn.
  std::vector<std::string> draw(HostCanvas& c, const std::vector<std::string>& perms = {}, int index = -1) {
    JsonData d(data.as<JsonVariantConst>());
    EXPECT_TRUE(render(doc.as<JsonVariantConst>(), index, c, d, perms, ".", 0, 0, c.width(), c.height()));
    return c.texts();
  }
};

std::string one(const std::string& items) {
  return R"({"decl": 1, "layouts": [{"size": [800, 480], "items": [)" + items + "]}]}";
}

std::string readFixture(const char* name) {
  std::ifstream in(std::string(FACE_FIXTURES) + "/" + name);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST(FaceRender, VietnameseCase) {
  EXPECT_EQ(inkdesk::decl::upper("thứ ba, chủ nhật"), "THỨ BA, CHỦ NHẬT");
  EXPECT_EQ(inkdesk::decl::upper("ơ ư đ ạ ỹ"), "Ơ Ư Đ Ạ Ỹ");
  EXPECT_EQ(inkdesk::decl::lower("BÍNH NGỌ Ư"), "bính ngọ ư");
  EXPECT_EQ(inkdesk::decl::title("THÁNG 11"), "Tháng 11");
  EXPECT_EQ(inkdesk::decl::title("CHỦ NHẬT"), "Chủ Nhật");
}

TEST(FaceRender, PicksTheClosestShape) {
  JsonDocument doc;
  deserializeJson(doc, R"({"decl": 1, "layouts": [{"size": [800, 480]}, {"size": [480, 800]}, {"size": [456, 210]}]})");
  const auto v = doc.as<JsonVariantConst>();
  EXPECT_EQ(inkdesk::decl::pick(v, 800, 480), 0);
  EXPECT_EQ(inkdesk::decl::pick(v, 480, 800), 1);
  EXPECT_EQ(inkdesk::decl::pick(v, 320, 480), 1);
  EXPECT_EQ(inkdesk::decl::pick(v, 444, 198), 2);
}

TEST(FaceRender, RefusesOtherLevels) {
  JsonDocument doc, data;
  deserializeJson(doc, R"({"decl": 2, "layouts": [{"size": [800, 480], "items": []}]})");
  HostCanvas c(800, 480);
  JsonData d(data.as<JsonVariantConst>());
  EXPECT_FALSE(render(doc.as<JsonVariantConst>(), -1, c, d, {}, ".", 0, 0, 800, 480));
}

TEST(FaceRender, BindingsNeedTheirPermission) {
  Fixture f(one(R"({"t": "text", "x": 0, "y": 0, "w": 400, "h": 60, "text": "{weather.temp}° {time.hm}"})"));
  HostCanvas without(800, 480), with(800, 480);
  EXPECT_EQ(f.draw(without), std::vector<std::string>{"° 07:45"});
  EXPECT_EQ(f.draw(with, {"data:weather"}), std::vector<std::string>{"28° 07:45"});
}

TEST(FaceRender, ConditionsAndFilters) {
  Fixture f(one(R"(
    {"t": "text", "if": "cal.count > 3", "x": 0, "y": 0, "w": 400, "h": 40, "text": "a"},
    {"t": "text", "if": "weather.temp >= 30", "x": 0, "y": 40, "w": 400, "h": 40, "text": "b"},
    {"t": "text", "if": "!cal.count", "x": 0, "y": 80, "w": 400, "h": 40, "text": "c"},
    {"t": "text", "if": "date.wd == 'Thứ Ba'", "x": 0, "y": 120, "w": 400, "h": 40, "text": "{date.wd|upper}"},
    {"t": "text", "if": "nothing.here", "x": 0, "y": 160, "w": 400, "h": 40, "text": "e"},
    {"t": "text", "x": 0, "y": 200, "w": 400, "h": 40, "text": "{{x}} {missing}"})"));
  HostCanvas c(800, 480);
  const auto t = f.draw(c, {"data:weather", "data:calendar"});
  EXPECT_EQ(t, (std::vector<std::string>{"a", "THỨ BA", "{x} "}));
}

TEST(FaceRender, ListsAndPackageLists) {
  Fixture f(R"({"decl": 1, "lists": {"q": ["một", "hai", "ba"]}, "layouts": [{"size": [800, 480], "items": [
    {"t": "list", "of": "cal.events", "max": 2, "x": 0, "y": 0, "dy": 50, "items": [
      {"t": "text", "x": 0, "y": 0, "w": 400, "h": 40, "text": "{i}. {item.time} {item.title}"},
      {"t": "text", "if": "last", "x": 400, "y": 0, "w": 100, "h": 40, "text": "cuối"}]},
    {"t": "list", "of": "pkg.q", "pick": "day", "x": 0, "y": 200, "items": [
      {"t": "text", "x": 0, "y": 0, "w": 400, "h": 40, "text": "{item}"}]}]}]})");
  HostCanvas c(800, 480);
  // 2 rows (max 2, so neither is the last of the 4 events), then day 321 % 3 = 0.
  EXPECT_EQ(f.draw(c, {"data:calendar"}),
            (std::vector<std::string>{"1. 09:00 Họp nhóm thiết kế", "2. 12:00 Ăn trưa với Lan", "một"}));
}

TEST(FaceRender, TextIsNeverCut) {
  const std::string longText = "Gọi cho nhà in về bìa sách mới của bộ truyện thiếu nhi trước chiều thứ Sáu";
  Fixture f(one(R"({"t": "text", "x": 10, "y": 10, "w": 150, "h": 40, "size": 50, "text": ")" + longText + R"("})"));
  HostCanvas c(800, 480);
  const auto t = f.draw(c);
  std::string joined;
  for (const auto& line : t) joined += (joined.empty() ? "" : " ") + line;
  EXPECT_EQ(joined, longText);
  EXPECT_GT(t.size(), 1u);
  EXPECT_EQ(c.outside(), 0);
}

TEST(FaceRender, DigitsFallBackToLettersFont) {
  Fixture f(one(R"({"t": "text", "x": 0, "y": 0, "w": 800, "h": 200, "size": 125, "font": "digits", "text": "{date.dm}"},
                   {"t": "text", "x": 0, "y": 200, "w": 800, "h": 200, "size": 125, "font": "digits", "text": "{date.wd}"})"));
  HostCanvas c(800, 480);
  EXPECT_EQ(f.draw(c), (std::vector<std::string>{"17/11", "Thứ Ba"}));
}

// The sample packages (s3desk/build-pkgs) in every layout: nothing off the screen, no "…".
TEST(FaceRender, SamplesDrawCleanly) {
  struct Case {
    const char* file;
    int w, h;
    std::vector<std::string> perms;
  };
  const std::vector<Case> cases = {
      {"bigclock.json", 800, 480, {"data:weather", "data:calendar"}},
      {"bigclock.json", 480, 800, {"data:weather", "data:calendar"}},
      {"today.json", 800, 480, {"data:weather", "data:calendar"}},
      {"quote.json", 444, 198, {}},
      {"quote.json", 210, 198, {}},
  };
  for (const auto& k : cases) {
    Fixture f(readFixture(k.file));
    HostCanvas c(k.w, k.h);
    const auto t = f.draw(c, k.perms);
    EXPECT_FALSE(t.empty()) << k.file;
    EXPECT_EQ(c.outside(), 0) << k.file << " " << k.w << "x" << k.h;
    for (const auto& s : t) EXPECT_EQ(s.find("\xE2\x80\xA6"), std::string::npos) << s;
  }
}

// A broken or hostile file must not keep the renderer busy (MUST-FIX 1 of review round 1): sizes, widths,
// radii and positions of 1e8-1e300 are capped, lines clipped, and the pixel work stays bounded.
TEST(FaceRender, HugeGeometryStaysBounded) {
  const std::vector<std::string> items = {
      R"({"t": "box", "x": -1e9, "y": -1e9, "w": 1e9, "h": 1e9, "lw": 1e9, "r": 1e9})",
      R"({"t": "box", "x": 0, "y": 0, "w": 1e8, "h": 1e8, "fill": "ink", "r": 1e8})",
      R"({"t": "bar", "x": 0, "y": 0, "w": 1e9, "h": 1e9, "lw": 1e9, "r": 1e9, "value": "1e9", "max": 1e-9})",
      R"({"t": "line", "x": -1e9, "y": 5, "x2": 1e9, "y2": 1e8, "lw": 1e9})",
      R"({"t": "line", "x": 1e9, "y": 1e9, "x2": 2e9, "y2": 2e9, "lw": 5})",
      R"({"t": "circle", "x": 400, "y": 240, "r": 1e9, "lw": 1e9, "fill": "muted"})",
      R"({"t": "icon", "x": 0, "y": 0, "w": 1e9, "h": 1e9, "name": "sun"})",
      R"({"t": "text", "x": 1e9, "y": -1e9, "w": 1e9, "h": 1e9, "size": 1e9, "text": "x"})",
      R"({"t": "group", "x": 1e300, "y": -1e300, "items": [{"t": "box", "x": 1, "y": 1, "w": 10, "h": 10}]})",
      // All of them in one layout: the pixel budget stops the rest.
      R"({"t": "box", "x": 0, "y": 0, "w": 1e9, "h": 1e9, "fill": "ink"},
         {"t": "box", "x": 0, "y": 0, "w": 1e9, "h": 1e9, "fill": "paper"},
         {"t": "box", "x": 0, "y": 0, "w": 1e9, "h": 1e9, "lw": 1e9, "r": 1e9},
         {"t": "circle", "x": 0, "y": 0, "r": 1e9, "fill": "ink"})",
  };
  for (const auto& item : items) {
    Fixture f(one(item));
    HostCanvas c(800, 480);
    JsonData d(f.data.as<JsonVariantConst>());
    inkdesk::decl::Stats st;
    const auto t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(render(f.doc.as<JsonVariantConst>(), -1, c, d, {}, ".", 0, 0, 800, 480, &st)) << item;
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_LE(st.work, 4000000) << item;
    EXPECT_LE(c.touched(), 4000000L + 2000L * 2000) << item;  // the budget, plus at most one capped thing
    EXPECT_LT(ms, 3000) << item;
  }
}

TEST(FaceRender, ManyItemsStopAtTheStepLimit) {
  // 20 x 20 x 20 rows of an item that is never drawn: 8,000 visits without the step limit.
  std::string row = R"({"t": "text", "if": "nothing", "x": 0, "y": 0, "w": 10, "h": 10, "text": "a"})";
  for (int k = 0; k < 3; k++) row = R"({"t": "list", "of": "pkg.n", "items": [)" + row + "]}";
  std::string list = R"("lists": {"n": [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20]})";
  Fixture f(R"({"decl": 1, )" + list + R"(, "layouts": [{"size": [800, 480], "items": [)" + row + "]}]}");
  HostCanvas c(800, 480);
  JsonData d(f.data.as<JsonVariantConst>());
  inkdesk::decl::Stats st;
  ASSERT_TRUE(render(f.doc.as<JsonVariantConst>(), -1, c, d, {}, ".", 0, 0, 800, 480, &st));
  EXPECT_LE(st.steps, 4000);
  EXPECT_EQ(st.drawn, 0);
}

TEST(FaceRender, SquareCornersStaySquare) {
  Fixture f(one(R"({"t": "box", "x": 10, "y": 10, "w": 50, "h": 50, "lw": 2, "r": 0},
                   {"t": "box", "x": 100, "y": 10, "w": 50, "h": 50, "lw": 2, "r": 8})"));
  HostCanvas c(800, 480);
  f.draw(c);
  EXPECT_TRUE(c.ink(10, 10));    // "r": 0
  EXPECT_FALSE(c.ink(100, 10));  // rounded
  EXPECT_TRUE(c.ink(125, 10));
}

TEST(FaceRender, LinesAreClippedToTheTarget) {
  Fixture f(one(R"({"t": "line", "x": -1e9, "y": 100, "x2": 1e9, "y2": 100, "lw": 1})"));
  HostCanvas c(800, 480);
  f.draw(c);
  EXPECT_TRUE(c.ink(0, 100));
  EXPECT_TRUE(c.ink(400, 100));
  EXPECT_TRUE(c.ink(799, 100));
  EXPECT_LE(c.outside(), 4);
}

// Fitting many long texts into tiny boxes measures a lot: it stops at the measuring limit (round 2, a).
TEST(FaceRender, TextFittingIsBounded) {
  std::string word = "Chữ dài để đo mãi ";
  std::string text;
  while (text.size() < 400) text += word;
  std::string items;
  for (int k = 0; k < 100; k++) {
    if (k) items += ",";
    items += R"({"t": "text", "x": 0, "y": 0, "w": 40, "h": 2, "lines": 8, "size": 50, "text": ")" + text + R"("})";
  }
  Fixture f(one(items));
  HostCanvas c(800, 480);
  JsonData d(f.data.as<JsonVariantConst>());
  inkdesk::decl::Stats st;
  const auto t0 = std::chrono::steady_clock::now();
  ASSERT_TRUE(render(f.doc.as<JsonVariantConst>(), -1, c, d, {}, ".", 0, 0, 800, 480, &st));
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  EXPECT_LE(st.measured, 300000);
  EXPECT_GT(st.skipped, 0);
  EXPECT_LT(ms, 3000);
}

TEST(FaceRender, AtMostEightPictures) {
  std::string items = R"({"t": "list", "of": "pkg.n", "items": [
      {"t": "image", "x": 0, "y": 0, "w": 10, "h": 10, "src": "missing.bmp"}]})";
  Fixture f(R"({"decl": 1, "lists": {"n": [1,2,3,4,5,6,7,8,9,10,11,12]}, "layouts": [{"size": [800, 480], "items": [)" +
            items + "]}]}");
  HostCanvas c(800, 480);
  JsonData d(f.data.as<JsonVariantConst>());
  inkdesk::decl::Stats st;
  ASSERT_TRUE(render(f.doc.as<JsonVariantConst>(), -1, c, d, {}, ".", 0, 0, 800, 480, &st));
  EXPECT_EQ(st.images, 8);
  EXPECT_EQ(st.imagesSkipped, 4);
}
