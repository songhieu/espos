// The news family (docs/widgets.md §3): the first stories (list), stories with their pictures (photo), one story
// big with its lead (one); the option picks a section (its slug; "" = every followed one). Titles are never cut.
// web/app/src/os/wid/news.ts draws the same.
#include <ArduinoJson.h>

#include "../data.h"
#include "../desk.h"
#include "../media.h"
#include "kit.h"

namespace wid {

using namespace kit;

const std::vector<Style> NEWS_STYLES = {{"list", N_("Headlines")}, {"photo", N_("With photos")}, {"one", N_("One big story")}};

namespace {

// The stories of the item's section (the dashboard names it in news.sections), or all of them.
std::vector<JsonVariantConst> stories(const String& slug, String& secName) {
  std::vector<JsonVariantConst> out;
  JsonArrayConst items = data::newsItems();
  secName = "";
  if (slug.length())
    for (JsonObjectConst s : desk::dash()["news"]["sections"].as<JsonArrayConst>())
      if (slug == (s["slug"] | "")) secName = s["name"] | "";
  for (JsonVariantConst v : items)
    if (!slug.length() || (secName.length() && secName == (v["sec"] | ""))) out.push_back(v);
  return out;
}

String meta(JsonVariantConst v, int64_t now) {
  String m = "VnExpress";
  const String a = ago(v["pub_ms"] | (int64_t)0, now);
  if (a.length()) m += " · " + a;
  return m;
}

}  // namespace

// ---------------------------------------------------------------- news
// Every title is shown whole: a layout takes the newest story whose title fits its box (with all its parts; else
// with the fewest: no meta line, no picture), and a lead only when it fits whole.
void wNews(lv_obj_t* parent, const Item& it, Card c) {
  String secName;
  const auto items = stories(it.opt, secName);
  const String st = styleOf(it);
  const String title = secName.length() ? secName : String(it.size == L ? T_("Latest news · VnExpress") : T_("Latest news"));
  const uint32_t capC = SKIN.own ? 0xc8213f : GREY;
  // The medium photo look puts its caption beside the story's picture (drawn with the story).
  const bool sideCap = st == "photo" && it.size == M && !items.empty();
  if (!sideCap) cap(c.o, title.c_str(), capC);
  if (data::newsItems().size() == 0) return empty(c, noData(), GREY, 22);
  if (items.empty()) return empty(c, N_("No stories in this section yet. Wait for the next sync."), GREY, 22);
  const int64_t now = desk::nowMs();
  const uint32_t meta_ = SKIN.own ? 0x8a8f99 : GREY;
  auto pic = [&](const String& url, int x, int y, int w, int h, int r) {
    lv_obj_t* m = media::image(c.o, url, w, h, r, 0xeceef3);
    lv_obj_set_pos(m, x, y);
    return m;
  };
  // The first of the newest stories that `draw` can place: with all its parts, else with the fewest.
  auto first = [&](auto draw) {
    for (int tight = 0; tight < 2; tight++)
      for (size_t i = 0; i < items.size() && i < 8; i++)
        if (draw(items[i], tight == 1)) return true;
    return false;
  };
  bool ok = true;

  // ---- stories with their pictures
  if (st == "photo") {
    if (it.size == S) {
      ok = first([&](JsonVariantConst v, bool tight) {
        const String thumb = v["thumb"] | "";
        for (int ph : {56, 40, 0}) {  // the picture smaller, then none (the tight pass only, for a story with one)
          if (ph ? tight || !thumb.length() : thumb.length() && !tight) continue;
          const int y = ph ? 20 + ph + 6 : 20;
          if (!fitIn(c.o, data::newsTitle(v), 0, y, c.w, c.h - y, 14, 12, true, INK, 1)) continue;
          if (ph) pic(thumb, 0, 20, c.w, ph, 10);
          return true;
        }
        return false;
      });
    } else if (it.size == M) {
      ok = first([&](JsonVariantConst v, bool tight) {
        const String thumb = v["thumb"] | "";
        const int x = thumb.length() ? 128 : 0;
        if (!fitIn(c.o, data::newsTitle(v), x, 20, c.w - x, tight ? c.h - 20 : c.h - 40, 15, 12, true, INK, 2))
          return false;
        cap(c.o, title.c_str(), capC, x);
        if (x) pic(thumb, 0, 0, 118, c.h, 12);
        if (!tight) at(c.o, meta(v, now), x, c.h - 16, 12, false, meta_);
        return true;
      });
      if (!ok) cap(c.o, title.c_str(), capC);
    } else {
      // Four tiles; a long title takes room from its picture.
      const int cw = (c.w - 10) / 2, th = (c.h - 22 - 10) / 2;
      int slot = 0;
      for (size_t i = 0; i < items.size() && i < 12 && slot < 4; i++) {
        const int x = (slot % 2) * (cw + 10), y = 22 + (slot / 2) * (th + 10);
        for (int ph : {70, 56, 40}) {
          if (!fitIn(c.o, data::newsTitle(items[i]), x, y + ph + 6, cw, th - ph - 6, 13, 12, true, INK, 1)) continue;
          pic(items[i]["thumb"] | "", x, y, cw, ph, 10);
          slot++;
          break;
        }
      }
      ok = slot > 0;
    }
  }

  // ---- one story big, with its lead
  else if (st == "one") {
    if (it.size == S) {
      ok = first([&](JsonVariantConst v, bool tight) {
        if (!fitIn(c.o, data::newsTitle(v), 0, 20, c.w, tight ? c.h - 20 : c.h - 40, 17, 12, true, INK, 2)) return false;
        if (!tight) at(c.o, ago(v["pub_ms"] | (int64_t)0, now), 0, c.h - 16, 12, false, GREY);
        return true;
      });
    } else {
      // The title, its picture (large) as big as leaves room for the lead; without room for the lead, the title
      // bigger and no lead.
      ok = first([&](JsonVariantConst v, bool tight) {
        const String thumb = v["thumb"] | "", t = data::newsTitle(v), lead = v["lead"] | "";
        const bool large = it.size == L;
        auto draw = [&](int ph, int box, int maxPx, bool withLead) {
          int y = ph ? 22 + ph + 8 : 20;
          if (ph) pic(thumb, 0, 22, c.w, ph, 12);
          lv_obj_t* l = fit(c.o, t, 0, y, c.w, box, maxPx, 12, true, INK, 2);
          y += heightOf(l) + 6;
          if (withLead) fit(c.o, lead, 0, y, c.w, c.h - 20 - y, 13, 12, false, GREY, 2);
          at(c.o, meta(v, now), 0, c.h - 16, 12, false, meta_);
          return true;
        };
        if (tight) {
          const int ph = large && thumb.length() ? 90 : 0, y = ph ? 22 + ph + 8 : 20;
          return measure(c.o, t, c.w, c.h - 20 - y, large ? 18 : 15, 12, true) >= 0 && draw(ph, c.h - 20 - y, large ? 18 : 15, false);
        }
        const int box = large ? 64 : 42, maxPx = large ? 18 : 15;
        for (int ph : {124, 100, 80, 0}) {
          if (ph ? !large || !thumb.length() : large && thumb.length()) continue;
          const int y = ph ? 22 + ph + 8 : 20;
          const int th = measure(c.o, t, c.w, box, maxPx, 12, true);
          if (th < 0) continue;
          const int ly = y + th + 6;
          if (lead.length() && (ly + 17 > c.h - 20 || measure(c.o, lead, c.w, c.h - 20 - ly, 13, 12, false) < 0)) continue;
          return draw(ph, box, maxPx, lead.length() > 0);
        }
        const int ph = large && thumb.length() ? 124 : 0, y = ph ? 22 + ph + 8 : 20;
        const int big = large ? 22 : 20;
        return measure(c.o, t, c.w, c.h - 20 - y, big, 12, true) >= 0 && draw(ph, c.h - 20 - y, big, false);
      });
    }
  }

  // ---- the first stories (the default)
  else if (it.size == S) {
    ok = first([&](JsonVariantConst v, bool tight) {
      if (!fitIn(c.o, data::newsTitle(v), 0, 20, c.w, tight ? c.h - 20 : c.h - 40, 15, 12, true, INK, 2)) return false;
      if (!tight) at(c.o, ago(v["pub_ms"] | (int64_t)0, now), 0, c.h - 16, 12, false, GREY);
      return true;
    });
  } else if (it.size == M) {
    ok = first([&](JsonVariantConst v, bool tight) {
      const String thumb = v["thumb"] | "";
      const int tw = thumb.length() ? 84 : 0;
      if (!fitIn(c.o, data::newsTitle(v), 0, 20, c.w - tw - (tw ? 10 : 0), tight ? c.h - 20 : c.h - 40, 15, 12, true, INK, 2))
        return false;
      if (!tight) at(c.o, meta(v, now), 0, c.h - 16, 12, false, meta_);
      if (tw) {
        lv_obj_t* m = media::image(c.o, thumb, 84, 84, 12, 0xeceef3);
        lv_obj_align(m, LV_ALIGN_TOP_RIGHT, 0, 6);
      }
      return true;
    });
  } else {
    // Rows: a picture and the title beside it (up to three lines); a title that does not fit gives its row to the
    // next story.
    int y = 22, shown = 0;
    for (JsonVariantConst v : items) {
      const int room = c.h - y;
      if (room < 19) break;
      const String thumb = v["thumb"] | "";
      if (thumb.length() && room < 52) continue;
      const int x = thumb.length() ? 62 : 0;
      lv_obj_t* l = fitIn(c.o, data::newsTitle(v), x, y, c.w - x, min(59, room), 14, 12, true, INK, 1);
      if (!l) continue;
      if (x) pic(thumb, 0, y, 52, 52, 10);
      y += max(x ? 52 : 0, heightOf(l)) + 10;
      shown++;
    }
    ok = shown > 0;
  }
  // No title fits anywhere (a very long one in a small card): the newest, as much as its box shows.
  if (!ok) fit(c.o, data::newsTitle(items[0]), 0, 20, c.w, c.h - 20, 15, 12, true, INK, 2);
}

}  // namespace wid
