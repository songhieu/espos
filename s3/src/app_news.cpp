#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
// News (Tin tức): sections, a list with pictures, and the whole article (text and photos) from the
// server's news cache (VnExpress).
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include <vector>

#include "apps.h"
#include "appopts.h"
#include "data.h"
#include "desk.h"
#include "fonts.h"
#include "media.h"
#include "os.h"
#include "trace.h"
#include "uikit.h"

namespace app_news {
namespace {

using namespace kit;

struct Story {
  String id, title, lead, sec, thumb;
  int64_t pub = 0;
};

struct ListState {
  lv_obj_t *scr, *list;
  std::vector<String> slugs;  // "" = newest
  int sec = 0;
} L;

std::vector<Story> fromJson(JsonArrayConst items) {
  std::vector<Story> out;
  for (JsonVariantConst v : items) {
    Story s;
    s.title = data::newsTitle(v);
    if (!s.title.length()) continue;
    if (!v.is<const char*>()) {
      s.id = v["id"].is<const char*>() ? String(v["id"].as<const char*>()) : String((long)(v["id"] | 0));
      if (s.id == "0") s.id = "";
      s.lead = v["lead"] | "";
      s.sec = v["sec"] | "";
      s.thumb = v["thumb"] | "";
      s.pub = v["pub_ms"] | (int64_t)0;
    }
    out.push_back(s);
  }
  return out;
}

void openArticle(const Story& s);
int perSection();
std::vector<Story> shown;  // what the list shows (taps index into it)

void fillList(const std::vector<Story>& stories) {
  if (!L.list) return;
  lv_obj_clean(L.list);
  shown = stories;
  const int64_t now = desk::nowMs();
  const int w = twoCols(L.list);  // landscape: stories two a line
  for (int i = 0; i < (int)stories.size() && i < 30; i++) {
    const Story& s = stories[i];
    lv_obj_t* c = cell(L.list, 10);
    lv_obj_set_width(c, w);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(c, 84, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(c, C(pal().press), LV_STATE_PRESSED);
    int x = 0;
    if (s.thumb.length()) {
      lv_obj_t* m = media::image(c, s.thumb, 64, 64, 10, pal().cell2);
      lv_obj_set_pos(m, 0, 0);
      x = 74;
    }
    lv_obj_t* t = label(c, "", 15, true, pal().fg);
    const int n = wrapText(t, s.title.c_str(), w - 20 - x, 15, true, 2, 3);
    lv_obj_set_pos(t, x, 0);
    String meta = s.sec.length() ? s.sec : String("VnExpress");
    const String a = ago(s.pub, now);
    if (a.length()) meta += " · " + a;
    lv_obj_t* m = label(c, "", 12, false, pal().faint);
    fitText(m, meta.c_str(), w - 20 - x, 36, 12, 12);  // inside the card (a longer language: two lines)
    lv_obj_align_to(m, t, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    (void)n;
    lv_obj_add_event_cb(c, [](lv_event_t* e) {
      const int i = (int)(intptr_t)lv_event_get_user_data(e);
      if (i < (int)shown.size()) openArticle(shown[i]);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  if (stories.empty())
    hint(L.list, desk::link() == desk::Link::Linked ? N_("No news yet, waiting for the next sync.")
                                                     : N_("Link EspOS to read the news."));
}

void loading(lv_obj_t* parent) {
  lv_obj_t* r = clear(parent);
  lv_obj_set_size(r, lv_pct(100), 80);
  lv_obj_t* sp = spinner(r, 28, pal().line, col::IOS_BLUE);
  lv_obj_center(sp);
}

void sectionLoaded(void* ctx, int status, JsonDocument* doc, const uint8_t*, size_t) {
  if (!L.scr || (int)(intptr_t)ctx != L.sec) return;
  if (status == 200 && doc && !(*doc)["items"].isNull()) {
    std::vector<Story> st = fromJson((*doc)["items"]);
    if ((int)st.size() > perSection()) st.resize(perSection());  // the web's "Số bài mỗi chuyên mục"
    fillList(st);
    return;
  }
  // No section route (older server): the stories of that section in the dashboard.
  std::vector<Story> all = fromJson(data::newsItems()), mine;
  JsonArrayConst secs = desk::dash()["news"]["sections"];
  const String name = L.sec - 1 < (int)secs.size() ? String(secs[L.sec - 1]["name"] | "") : String("");
  for (const Story& s : all)
    if (s.sec == name) mine.push_back(s);
  fillList(mine);
}

void pickSection(int i) {
  L.sec = i;
  desk::cancelFetches(&L);
  if (i == 0 || i >= (int)L.slugs.size()) return fillList(fromJson(data::newsItems()));
  lv_obj_clean(L.list);
  loading(L.list);
  desk::fetch("/v1/device/news?sec=" + L.slugs[i], false, sectionLoaded, (void*)(intptr_t)i, true);
}

// ---------------------------------------------------------------- article
struct ArticleState {
  lv_obj_t *scr, *body, *spin;
  Story s;
} A;

void paragraph(lv_obj_t* body, const String& text, int px, bool bold, uint32_t color) {
  lv_obj_t* l = label(body, "", px, bold, color);
  wrapText(l, text.c_str(), os::W() - 32, px, bold, px >= 16 ? 5 : 4);
  lv_obj_set_style_pad_hor(l, 4, 0);
}

void articleLoaded(void*, int status, JsonDocument* doc, const uint8_t*, size_t) {
  if (!A.scr) return;
  if (A.spin) lv_obj_delete(A.spin), A.spin = nullptr;
  if (status != 200 || !doc) {
    hint(A.body, status == 404 ? N_("The server has not saved this article.")
                               : N_("Could not load the article, try again later."));
    return;
  }
  JsonObjectConst a = doc->as<JsonObjectConst>();
  // Normalised article (blocks of {p} / {img,w,h,cap}); the raw cache uses Para/ImgURL/Caption.
  for (JsonObjectConst b : a["blocks"].as<JsonArrayConst>()) {
    const char* p = b["p"] | (b["Para"] | "");
    const char* img = b["img"] | "";
    if (*p) {
      paragraph(A.body, p, 15, false, pal().fg);
    } else if (*img) {
      const int w = os::W() - 24;
      const int iw = b["w"] | 320, ih = b["h"] | 180;
      const int h = constrain(w * ih / max(1, iw), 80, os::landscape() ? 220 : 360);  // landscape: cut to 220 high
      media::image(A.body, img, w, h, 12, pal().cell2);
      const char* cap = b["cap"] | (b["Caption"] | "");
      if (*cap) paragraph(A.body, cap, 13, false, pal().faint);
    }
  }
  lv_obj_t* src = label(A.body, N_("Source: VnExpress"), 12, false, pal().faint);
  lv_obj_set_style_pad_hor(src, 4, 0);
}

void openArticle(const Story& s) {
  A = {};
  A.s = s;
  lv_obj_t* body;
  A.scr = os::screen(N_("News"), &body);
  A.body = body;
  lv_obj_set_style_pad_row(body, 10, 0);
  lv_obj_add_event_cb(A.scr, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) != A.scr) return;
    desk::cancelFetches(&A);
    A.scr = A.body = A.spin = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  if (s.thumb.length()) {
    // The list picture first, at full width, until the article's own arrives.
  }
  paragraph(body, s.title, 20, true, pal().fg);
  String meta = (s.sec.length() ? s.sec + " · " : String("")) + "VnExpress";
  const String a = ago(s.pub, desk::nowMs());
  if (a.length()) meta += " · " + a;
  lv_obj_t* m = label(body, meta.c_str(), 12, false, pal().faint);
  lv_obj_set_style_pad_hor(m, 4, 0);
  if (s.lead.length()) paragraph(body, s.lead, 16, true, pal().dim);
  if (s.id.length() && desk::link() == desk::Link::Linked) {
    A.spin = clear(body);
    lv_obj_set_size(A.spin, lv_pct(100), 60);
    lv_obj_t* sp = spinner(A.spin, 24, pal().line, col::IOS_BLUE);
    lv_obj_center(sp);
    desk::fetch("/v1/device/news/" + s.id, false, articleLoaded, &A, true);
  } else if (!s.lead.length()) {
    hint(body, N_("The server sent no text for this story."));
  }
  os::open(A.scr);
}

// ---------------------------------------------------------------- sections (news.sections, news.per_section)
// The web's News page (n2/NewsPage.tsx SECTIONS): the same 16 sections in the same order, at least one (the slugs
// are what both sides store; the names are translated). Shared with the morning paper; the S3 lists a section's
// newest per_section stories.
// i18n: sections of VnExpress, a Vietnamese newspaper
const char* const SECTIONS[][2] = {
    {"tin-moi-nhat", N_("Latest news")},     {"tin-noi-bat", N_("Top stories")},  {"thoi-su", N_("Current affairs")},
    {"the-gioi", N_("World")},               {"kinh-doanh", N_("Business")},       {"bat-dong-san", N_("Real estate")},
    {"khoa-hoc-cong-nghe", N_("Science & tech")}, {"suc-khoe", N_("Health")},     {"giao-duc", N_("Education")},
    {"phap-luat", N_("Law")},                {"gia-dinh", N_("Life")},             {"du-lich", N_("Travel")},
    {"giai-tri", N_("Entertainment")},       {"the-thao", N_("Sports")},           {"oto-xe-may", N_("Cars")},
    {"y-kien", N_("Opinion")}};
constexpr int NSEC = sizeof SECTIONS / sizeof SECTIONS[0];

// The followed slugs: the setting, else what the dashboard says, else the server's default.
String followed() {
  String v = desk::setting("news.sections", "");
  if (v.length()) return v;
  for (JsonObjectConst s : desk::dash()["news"]["sections"].as<JsonArrayConst>())
    v += (v.length() ? "," : "") + String(s["slug"] | "");
  return v.length() ? v : String("tin-moi-nhat,thoi-su,the-gioi,kinh-doanh,khoa-hoc-cong-nghe,suc-khoe");
}
bool follows(const String& list, const char* slug) { return ("," + list + ",").indexOf(String(",") + slug + ",") >= 0; }
int perSection() {
  const int n = desk::settingInt("news.per_section", 5);
  return n == 3 || n == 10 ? n : 5;
}

lv_obj_t* secBody = nullptr;
void fillSections();

void fillSections() {
  if (!secBody) return;
  lv_obj_clean(secBody);
  caption(secBody, N_("Following"));
  lv_obj_t* r = clear(secBody);
  lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(r, 6, 0);
  const String f = followed();
  for (int i = 0; i < NSEC; i++) {
    const bool on = follows(f, SECTIONS[i][0]);
    lv_obj_t* b = box(r, on ? col::IOS_BLUE : pal().chip, 15);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 3);
    lv_obj_center(label(b, SECTIONS[i][1], 13, true, on ? 0xffffff : pal().chipFg));
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      const int k = (int)(intptr_t)lv_event_get_user_data(e);
      const String now = followed();
      String out;
      for (int i = 0; i < NSEC; i++) {
        const bool on = follows(now, SECTIONS[i][0]) != (i == k);
        if (on) out += (out.length() ? "," : "") + String(SECTIONS[i][0]);
      }
      if (!out.length()) return os::toast(N_("Keep at least one section"));
      desk::setSetting("news.sections", out);
      desk::syncNow();
      lv_async_call([](void*) { fillSections(); }, nullptr);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  caption(secBody, N_("Stories per section"));
  static const char* const N[] = {"3", "5", "10"};
  const int per = perSection();
  chips(secBody, N, 3, per == 3 ? 0 : per == 5 ? 1 : 2, [](int i) {
    desk::setSetting("news.per_section", i == 0 ? "3" : i == 1 ? "5" : "10");
    desk::syncNow();
  });
  hint(secBody, N_("Shared with the News page on the web (and the morning paper). A new section's stories arrive "
                   "after the next sync."));
}

// ---------------------------------------------------------------- unread badge
Preferences prefs;
bool prefsOpen = false;

int64_t seenMs() {
  if (!prefsOpen) prefs.begin("news"), prefsOpen = true;
  return prefs.getString("seen", "0").toInt() * 1000LL;
}

}  // namespace

int sectionCount() { return data::codesOf(followed(), "").size(); }

void openSections() {
  lv_obj_t* s = os::screen(N_("Sections"), &secBody);
  lv_obj_add_event_cb(s, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == lv_event_get_target(e)) secBody = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  fillSections();
  os::open(s);
}

void seenAll() {
  int64_t newest = 0;
  for (JsonVariantConst v : data::newsItems()) newest = max<int64_t>(newest, v["pub_ms"] | (int64_t)0);
  if (!newest) return;
  if (!prefsOpen) prefs.begin("news"), prefsOpen = true;
  prefs.putString("seen", String((long)(newest / 1000)));
}

int badge() {
  const int64_t seen = seenMs();
  int n = 0;
  for (JsonVariantConst v : data::newsItems())
    if ((v["pub_ms"] | (int64_t)0) > seen) n++;
  return seen ? min(n, 9) : min(n, 3);
}

void open(const char* arg) {
  L = {};
  lv_obj_t* body;
  L.scr = os::screen(N_("News"), &body, os::ROOT);
  os::navAction(L.scr, N_("Edit"), [](lv_event_t*) { openSections(); });
  lv_obj_add_event_cb(L.scr, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) != L.scr) return;
    desk::cancelFetches(&L);
    L.scr = L.list = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  // Sections: "All" and the ones the reader follows (named in the device's language when the slug is known).
  static std::vector<String> names;
  names = {T_("All")};
  L.slugs = {""};
  for (JsonObjectConst s : desk::dash()["news"]["sections"].as<JsonArrayConst>()) {
    const char* slug = s["slug"] | "";
    String name = s["name"] | "";
    for (int i = 0; i < NSEC; i++)
      if (!strcmp(slug, SECTIONS[i][0])) name = T_(SECTIONS[i][1]);
    names.push_back(name);
    L.slugs.push_back(slug);
  }
  static std::vector<const char*> ptrs;
  ptrs.clear();
  for (const String& n : names) ptrs.push_back(n.c_str());
  if (names.size() > 1) chips(body, ptrs.data(), ptrs.size(), 0, pickSection);
  L.list = stack(body, false, 8);
  fillList(fromJson(data::newsItems()));
  hint(body, N_("News from VnExpress, through the EspOS server."));
  seenAll();
  os::open(L.scr);
  (void)arg;
}

}  // namespace app_news
