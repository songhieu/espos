#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "../lunar.h"
#include "art.h"
#include "gfx.h"

namespace bot {
namespace ui {

using namespace gfx;

namespace {

// ---------------------------------------------------------------- words
struct Pair {
  const char* en;
  const char* vi;
};
const Pair STR[S_COUNT_] = {
    {"I'm listening", "Mình nghe đây"},
    {"I didn't catch that. Could you say it again?", "Mình chưa nghe rõ. Bạn nói lại nha?"},
    {"Join the WiFi", "Vào WiFi"},
    {"to set me up", "để cài mạng"},
    {"Add device", "Thêm máy"},
    {"Connecting to WiFi", "Đang vào WiFi"},
    {"No WiFi", "Chưa có WiFi"},
    {"Updating", "Đang cập nhật"},
    {"The free trial is over. Get a plan on espos.dev to keep talking with me.",
     "Hết lượt dùng thử. Mua gói tại espos.dev để nói chuyện tiếp với mình nha."},
    {"Keep holding to forget the WiFi", "Giữ tiếp để quên WiFi"},
    {"WiFi forgotten", "Đã quên WiFi"},
    {"Alarm", "Báo thức"},
    {"Time's up", "Hết giờ"},
    {"Hello!", "Xin chào!"},
    {"Saved", "Đã lưu"},
    {"Restarting", "Đang khởi động lại"},
    {"Can't reach espos.dev", "Chưa kết nối được espos.dev"},
    {"Set me up at espos.dev with the USB cable", "Cài máy tại espos.dev bằng cáp USB"},
    {"Your account is locked. See espos.dev", "Tài khoản đang bị khoá. Xem espos.dev"},
    {"Password", "Mật khẩu"},
    {"Wrong WiFi password", "Sai mật khẩu WiFi"},
    {"Can't find that WiFi", "Không thấy mạng WiFi"},
    {"Couldn't join the WiFi", "Chưa vào được WiFi"},
    {"Try again on your phone", "Thử lại trên điện thoại"},
    {"WiFi connected", "Đã kết nối WiFi"},
    {"Done! Hello there", "Xong! Chào bạn nha"},
    {"Getting a code", "Đang lấy mã"},
    {"Pairing over WiFi: coming soon", "Ghép máy qua WiFi: sắp có"},
    {"Volume", "Âm lượng"},
    {"Lost the WiFi", "Mất WiFi"},
    {"Trying again · press the button to set up WiFi", "Đang thử lại · bấm nút để cài WiFi"},
    {"Set up a new WiFi on your phone", "Cài WiFi mới trên điện thoại"},
    {"Set it again on your phone", "Cài lại trên điện thoại"},
};
const char* const WD_EN[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const WD_EN3[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const WD_VI[7] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
const char* const WD_VI2[7] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
const char* const MON_EN[12] = {"January", "February", "March",     "April",   "May",      "June",
                                "July",    "August",   "September", "October", "November", "December"};
const char* const MON_EN3[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

Lang lang_ = Lang::EN, drawLang = Lang::EN;  // drawLang: the setup screens take turns in both
uint8_t look_ = 0;
Now now_;
Weather wx_;
std::vector<Screen> screens_;
Info info_;
bool offline_ = false;
State state_ = State::STANDBY;
uint32_t ms_ = 0, t0 = 0;  // the last tick, the start of the current state

const char* tr(Str s) { return drawLang == Lang::VI ? STR[s].vi : STR[s].en; }

std::string fmt(const char* f, int a, int b = 0) {
  char t[32];
  snprintf(t, sizeof t, f, a, b);
  return t;
}
int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
int wdayOf(const Now& n) { return clampi(n.wday, 0, 6); }
int monOf(const Now& n) { return clampi(n.mon, 1, 12) - 1; }

uint8_t lk() { return look_ < art::LOOK_COUNT ? look_ : 0; }

// ---------------------------------------------------------------- the scene player
struct Player {
  const art::Scene* sc = nullptr;
  uint32_t at = 0;  // when the scene started
  bool loop = true;
  void start(art::SceneId id, bool lp) { start(&art::SCENES[lk()][id], lp); }
  void start(const art::Scene* s, bool lp) { sc = s, at = ms_, loop = lp; }
  uint32_t length() const {
    uint32_t n = 0;
    for (uint16_t i = 0; sc && i < sc->n; i++) n += sc->steps[i].ms;
    return n;
  }
  bool done() const { return !loop && sc && ms_ - at >= length(); }
  uint16_t frame() const {
    if (!sc || !sc->n) return 0;
    const uint32_t len = length();
    uint32_t t = ms_ - at;
    if (loop) t %= len;
    for (uint16_t i = 0; i < sc->n; i++) {
      if (t < sc->steps[i].ms) return sc->steps[i].frame;
      t -= sc->steps[i].ms;
    }
    return sc->steps[sc->n - 1].frame;  // played once: its last picture stays
  }
  void draw() const { gfx::frame(frame()); }
};
Player pl;

// ---------------------------------------------------------------- talk
// The words go up a line at a time (a short slide), so at rest every line on the screen is whole.
struct Talk {
  std::vector<std::string> lines;
  std::string words;
  Mood mood = Mood::TALK;
  int x = 0, y = 0, w = 0, vis = 1, steps = 0;  // vis: whole lines in the box; steps: lines to go up
  uint32_t first = 0, perLine = 0, speakMs = 0, doneMs = 0;
};
Talk tk;
std::string sadWords;  // notHeard(): the words after the sad face
constexpr uint32_t SLIDE_MS = 240;

void layoutTalk(const std::string& words) {
  const art::Box b = art::TALK_BOX[lk()];
  const int lh = lineH(F11);
  tk.words = words;
  tk.x = b.x0, tk.y = b.y0, tk.w = b.x1 - b.x0 + 1;
  tk.lines = wrap(F11, words, tk.w);
  tk.vis = (b.y1 - b.y0 + 1) / lh;  // art2's boxes hold whole 13-px lines (marks and descenders inside)
  if (tk.vis < 1) tk.vis = 1;
  tk.steps = (int)tk.lines.size() > tk.vis ? (int)tk.lines.size() - tk.vis : 0;
  // reading speed: ~60 ms a letter; a line stays 0.9 s at least and 2 s at most
  size_t letters = 0, firstLetters = 0;
  for (size_t i = 0; i < tk.lines.size(); i++) {
    letters += tk.lines[i].size();
    if ((int)i < tk.vis) firstLetters += tk.lines[i].size();
  }
  tk.perLine = tk.lines.empty() ? 1000 : (uint32_t)clampi((int)(letters * 60 / tk.lines.size()), 900, 2000);
  tk.first = (uint32_t)clampi((int)(firstLetters * 60), 1500, 12000);
  tk.speakMs = tk.first + tk.steps * (SLIDE_MS + tk.perLine);
  tk.doneMs = tk.speakMs + 1500;
}

// How far the words have gone up (pixels) at t, and whether they are moving.
int talkOffset(uint32_t t, bool* moving) {
  *moving = false;
  if (t < tk.first || !tk.steps) return 0;
  const uint32_t per = SLIDE_MS + tk.perLine, k = (t - tk.first) / per, in = (t - tk.first) % per;
  if ((int)k >= tk.steps) return tk.steps * lineH(F11);
  const int base = (int)k * lineH(F11);
  if (in >= SLIDE_MS) return base + lineH(F11);
  *moving = true;
  return base + (int)(in * lineH(F11) / SLIDE_MS);
}

void drawTalk() {
  const uint32_t t = ms_ - t0;
  const bool speaking = t < tk.speakMs;
  if (speaking) {
    if (pl.sc != &art::SCENES[lk()][art::SC_TALK]) pl.start(art::SC_TALK, true);
  } else {
    const art::SceneId still = tk.mood == Mood::SAD ? art::SC_TALK_SAD : art::SC_TALK_HAPPY;
    if (pl.sc != &art::SCENES[lk()][still]) pl.start(still, true);
  }
  pl.draw();
  bool moving;
  const int off = talkOffset(t, &moving);
  const art::Box b = art::TALK_BOX[lk()];
  fill(b.x0, b.y0, b.x1, b.y1, false);
  clip(b.x0, b.y0, b.x1, b.y1);
  const int lh = lineH(F11);
  for (size_t i = 0; i < tk.lines.size(); i++) {
    const int y = tk.y + (int)i * lh - off;
    if (y > b.y1 || y + lh < b.y0) continue;
    if (!moving && (y < b.y0 || y + lh > b.y1 + 1)) continue;  // at rest: whole lines only
    text(F11, tk.lines[i], tk.x, y);
  }
  clipAll();
}

// ---------------------------------------------------------------- standby (art2's screens, standby2.draw_live)
const char* const WD_VI_SHORT[7] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};

int idx(const char* id) {
  for (int i = 0; i < art::STANDBY_COUNT; i++)
    if (!strcmp(art::STANDBY[i].id, id)) return i;
  return 0;
}

const std::vector<Screen>& rotation() {
  // 2026-11-20: the bold clock, the wall-calendar page, the weather, the eyes, the aquarium
  static const std::vector<Screen> def = {{(int16_t)idx("clock_bold"), 20}, {(int16_t)idx("date_page"), 8},
                                          {SCR_WEATHER_AUTO, 8}, {(int16_t)idx("eyes"), 20}, {(int16_t)idx("aquarium"), 15}};
  return screens_.empty() ? def : screens_;
}
int rotIdx = 0, cur = 0;  // the rotation's place, the art2 screen it shows
uint32_t rotAt = 0;
bool noneShown = false;

// "weather": rain, storm or snow -> weather_rain; else 18:00-06:00 -> night; else weather_sun (admin-users' rule).
// The calendar page gives way to the morning coffee 06:00-09:00 (2026-11-20).
int resolve(int16_t id) {
  if (id == SCR_WEATHER_AUTO) {
    const std::string& k = wx_.icon;
    if (k == "rain" || k == "storm" || k == "snow") return idx("weather_rain");
    if (now_.valid && (now_.hour >= 18 || now_.hour < 6)) return idx("night");
    return idx("weather_sun");
  }
  if (id == idx("date_page") && now_.valid && now_.hour >= 6 && now_.hour < 9) return idx("coffee");
  return id >= 0 && id < art::STANDBY_COUNT ? id : 0;
}

// A screen waits for what it shows: the weather, the gold price, the news.
bool shown(int16_t id) {
  if (id == SCR_WEATHER_AUTO) return wx_.valid;
  const char* s = id >= 0 && id < art::STANDBY_COUNT ? art::STANDBY[id].id : "";
  if (!strncmp(s, "weather_", 8)) return wx_.valid;
  if (!strcmp(s, "gold")) return !info_.goldSell.empty();
  if (!strcmp(s, "newspaper")) return !info_.headlines.empty();
  return true;
}

void startRot(int i) {
  const auto& r = rotation();
  noneShown = true;
  for (size_t k = 0; k < r.size(); k++) {  // the next one that can be shown
    const int j = (i + (int)k) % (int)r.size();
    if (shown(r[j].id)) {
      rotIdx = j, noneShown = false;
      break;
    }
  }
  rotAt = ms_;
  cur = noneShown ? idx("clock_bold") : resolve(r[rotIdx].id);
  pl.start(&art::STANDBY[cur].scene, true);
}

// The values of the live fields (standby2's SAMPLE keys), from the clock, the weather and the dashboard.
struct Vals {
  std::string s[art::FD_COUNT];
  int hh = 0, mm = 0, ss = 0, firstWd = 0, days = 30, today = 1;
};

std::string twoDigits(int v) { return fmt("%02d", v); }
std::string commaNum(const std::string& v) {  // "148.5" -> "148,5" (the Vietnamese decimal comma)
  std::string o = v;
  for (char& c : o) if (c == '.') c = ',';
  return o;
}

}  // namespace

// The greeting for the local hour (owner, 2026-11-20): sáng 05-10, trưa 10-13, chiều 13-18, tối 18-22, khuya.
std::string greeting(int hour, bool vi) {
  if (hour >= 5 && hour < 10) return vi ? "Chào buổi sáng" : "Good morning";
  if (hour >= 10 && hour < 13) return vi ? "Chào buổi trưa" : "Good day";
  if (hour >= 13 && hour < 18) return vi ? "Chào buổi chiều" : "Good afternoon";
  if (hour >= 18 && hour < 22) return vi ? "Chào buổi tối" : "Good evening";
  return vi ? "Chúc ngủ ngon" : "Good night";
}

namespace {

void buildVals(Vals& v) {
  const bool vi = drawLang == Lang::VI;
  const Now& n = now_;
  v.hh = n.hour, v.mm = n.min, v.ss = n.sec;
  const std::string t = n.valid ? fmt("%02d:%02d", n.hour, n.min) : std::string("--:--");
  v.s[art::FD_TIME] = v.s[art::FD_TIME_N] = v.s[art::FD_TIME_M] = t;
  v.s[art::FD_HH] = n.valid ? twoDigits(n.hour) : "--";
  v.s[art::FD_MM] = n.valid ? twoDigits(n.min) : "--";
  v.s[art::FD_SS] = twoDigits(n.sec);
  const int wd = wdayOf(n), mo = monOf(n);
  v.s[art::FD_WEEKDAY] = vi ? WD_VI[wd] : WD_EN[wd];
  v.s[art::FD_WEEKDAY_SHORT] = vi ? WD_VI_SHORT[wd] : WD_EN3[wd];
  v.s[art::FD_DAY] = fmt("%d", n.day);
  v.s[art::FD_DATE_LINE] = dateLine(n, 200);
  v.s[art::FD_DATE_MID] = v.s[art::FD_WEEKDAY] + fmt(" %d/%d", n.day, n.mon);
  v.s[art::FD_MONTH_SHORT] = vi ? fmt("Tháng %d", n.mon) : std::string(MON_EN[mo]);
  v.s[art::FD_MONTH_LINE] = (vi ? fmt("Tháng %d", n.mon) : std::string(MON_EN[mo])) + fmt(" · %d", n.year);
  // the lunar date: the device's own maths (the S3's, Ho Ngoc Duc's), in the bot's time zone
  const espos::lunar::Date ld = espos::lunar::fromSolar(n.day, n.mon, n.year, 7);
  const std::string lday = fmt("%d/%d", ld.day, ld.month) + (ld.leap ? "N" : "");
  v.s[art::FD_LUNAR_DAY] = fmt("%d/%d", ld.day, ld.month);
  v.s[art::FD_LUNAR_SHORT] = (vi ? "ÂL " : "Lunar ") + lday;
  v.s[art::FD_LUNAR_LABEL] = espos::lunar::yearName(ld.year);
  const int toTet = espos::lunar::daysToTet(n.day, n.mon, n.year, 7);
  v.s[art::FD_TET_DAYS] = fmt("%d", toTet);
  v.s[art::FD_TET_NAME] = (vi ? "Tết " : "Tết · ") + espos::lunar::yearName(ld.year + 1);
  // the month grid, Monday first
  static const int MD[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (n.year % 4 == 0 && n.year % 100) || n.year % 400 == 0;
  v.days = MD[mo] + (mo == 1 && leap ? 1 : 0), v.today = n.day;
  v.firstWd = ((wd - (n.day - 1) % 7) + 7 + 6) % 7;  // the 1st's weekday, Monday = 0
  // the weather
  const std::string words = weatherWords(wx_);
  v.s[art::FD_TEMP] = v.s[art::FD_TEMP_RAIN] = wx_.valid ? fmt("%d°", wx_.t) : "";
  v.s[art::FD_WEATHER] = v.s[art::FD_RAIN_S] = words;
  v.s[art::FD_CITY_HILO] = wx_.city + fmt(" %d°/%d°", wx_.hi, wx_.lo);
  v.s[art::FD_CITY_HUM] = info_.hum >= 0 ? wx_.city + (vi ? fmt(" ẩm %d%%", info_.hum) : fmt(" %d%% humid", info_.hum))
                                          : wx_.city + fmt(" %d°/%d°", wx_.hi, wx_.lo);
  std::string low = words;
  if (!low.empty() && (unsigned char)low[0] < 0x80) low[0] = (char)tolower((unsigned char)low[0]);
  v.s[art::FD_TEMP_NIGHT] = fmt("%d° ", wx_.t) + low;
  // the news, the gold price, the day's saying
  if (!info_.headlines.empty()) v.s[art::FD_HEADLINE] = info_.headlines[(ms_ / 30000) % info_.headlines.size()];
  v.s[art::FD_GREET] = greeting(n.valid ? n.hour : 12, vi);
  v.s[art::FD_GOLD_SELL] = vi ? commaNum(info_.goldSell) : info_.goldSell;
  v.s[art::FD_GOLD_BUY_LINE] = (vi ? "Mua " : "Buy ") + (vi ? commaNum(info_.goldBuy) : info_.goldBuy);
  v.s[art::FD_USD_LINE] = info_.usd.empty() ? "" : info_.usd + (vi ? " đ" : " VND");
  v.s[art::FD_GOLD_UNIT_S] = vi ? "tr/lượng" : "m/tael";
  const int jd = espos::lunar::julianDay(n.day, n.mon, n.year);
  v.s[art::FD_QUOTE] = vi ? art::QUOTES_VI[jd % art::QUOTES_VI_N] : art::QUOTES_EN[jd % art::QUOTES_EN_N];
}

// ---- the live fields' drawing (botdraw.h's, for the hands)
void disc(float cx, float cy, float r, bool on) {
  for (int yy = (int)floorf(cy - r); yy <= (int)ceilf(cy + r); yy++)
    for (int xx = (int)floorf(cx - r); xx <= (int)ceilf(cx + r); xx++) {
      const float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy;
      if (dx * dx + dy * dy <= r * r) px(xx, yy, on);
    }
}
void stroke(float x0, float y0, float x1, float y1, float w, bool on) {
  const int n = (int)(hypotf(x1 - x0, y1 - y0) * 2) + 1;
  for (int i = 0; i <= n; i++) disc(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, w / 2, on);
}
void line1(float x0, float y0, float x1, float y1) {  // a clean 1-px line (px.path1: no L-shaped corners)
  int xs[400], ys[400], n = 0;
  const int k = (int)(fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) * 3) + 1;
  for (int i = 0; i <= k && n < 400; i++) {
    const int x = (int)floorf(x0 + (x1 - x0) * i / k), y = (int)floorf(y0 + (y1 - y0) * i / k);
    if (n && xs[n - 1] == x && ys[n - 1] == y) continue;
    if (n >= 2 && abs(xs[n - 2] - x) == 1 && abs(ys[n - 2] - y) == 1 && abs(xs[n - 1] - xs[n - 2]) + abs(ys[n - 1] - ys[n - 2]) == 1)
      n--;
    if (n && xs[n - 1] == x && ys[n - 1] == y) continue;
    xs[n] = x, ys[n] = y, n++;
  }
  for (int i = 0; i < n; i++) px(xs[i], ys[i], true);
}
void rrectFill(int x0, int y0, int x1, int y1, float r, bool on) {  // px.rrect: pixel centres inside
  const float ix0 = x0 + r, iy0 = y0 + r, ix1 = x1 + 1 - r, iy1 = y1 + 1 - r;
  for (int yy = y0; yy <= y1; yy++)
    for (int xx = x0; xx <= x1; xx++) {
      const float xc = xx + 0.5f, yc = yy + 0.5f;
      const float dx = fmaxf(fmaxf(ix0 - xc, xc - ix1), 0), dy = fmaxf(fmaxf(iy0 - yc, yc - iy1), 0);
      if (dx * dx + dy * dy <= r * r + 0.01f) px(xx, yy, on);
    }
}

void drawLive(const art::Live& f, const Vals& v, uint32_t t) {
  const float ts = t / 1000.0f;
  if (f.kind == art::LK_DIGITS) {
    const bool colon = !f.blink || (ms_ % 1000) < 500;
    digits(f.ref, v.s[f.field], f.x0, f.y0, f.align, colon, f.color);
  } else if (f.kind == art::LK_TEXT) {
    std::string s = v.s[f.field];
    if (f.upper) s = upper(s);
    if (s.empty()) return;
    const Font font = f.ref == 0 ? F11 : T5;
    const int bw = f.x1 - f.x0 + 1, dy = font == T5 ? 2 : 0;
    const int ax = f.align == 0 ? f.x0 : f.align == 1 ? (f.x0 + f.x1 + 1) / 2 : f.x1 + 1;
    clip(f.x0, f.y0, f.x1, f.y1);
    if (f.wrap) {  // lines; too many: they go up 12 px/s, holding 1.5 s at the start and 2 s at the end
      const std::vector<std::string> ls = wrap(font, s, bw);
      const int lh = lineH(font), total = (int)ls.size() * lh, room = f.y1 - f.y0 + 1;
      int yy = f.y0, off = 0;
      if (total <= room) {
        yy = f.y0 + (room - total) / 2;
      } else {
        const int end = total - room;
        const float cyc = 1.5f + end / 12.0f + 2.0f, tt = fmodf(ts, cyc);
        off = tt < 1.5f ? 0 : std::min(end, (int)((tt - 1.5f) * 12));
      }
      for (size_t j = 0; j < ls.size(); j++) text(font, ls[j], ax, yy + (int)j * lh - off + dy, f.align, f.color);
    } else {
      const int tw = textW(font, s);
      if (tw <= bw) {
        text(font, s, ax, f.y0 + dy, f.align, f.color);
      } else {  // a marquee: the whole line passes at 20 px/s, a gap of 24, again
        const int span = tw + 24, off = (int)(ts * 20) % span;
        for (int rep = 0; rep < 2; rep++) text(font, s, f.x0 - off + rep * span, f.y0 + dy, 0, f.color);
      }
    }
    clipAll();
  } else if (f.kind == art::LK_HANDS) {
    const float cx = f.p[0], cy = f.p[1];
    const float am = (v.mm + v.ss / 60.0f) / 60.0f * 2 * (float)M_PI, ah = ((v.hh % 12) + v.mm / 60.0f) / 12.0f * 2 * (float)M_PI;
    const float lens[2] = {(float)f.p[2], (float)f.p[3]}, ws[2] = {f.p[5] / 10.0f, f.p[6] / 10.0f}, angs[2] = {ah, am};
    for (int k = 0; k < 2; k++) {
      const float ex = cx + sinf(angs[k]) * lens[k], ey = cy - cosf(angs[k]) * lens[k];
      stroke(cx, cy, ex, ey, ws[k] + 2, false);
      if (ws[k] > 1.2f) stroke(cx, cy, ex, ey, ws[k], true);
      else line1(cx, cy, ex, ey);
    }
    if (f.p[4]) {
      const float as = v.ss / 60.0f * 2 * (float)M_PI;
      line1(cx - sinf(as) * 4, cy + cosf(as) * 4, cx + sinf(as) * f.p[4], cy - cosf(as) * f.p[4]);
    }
    disc(cx, cy, 2.6f, true);
    px((int)cx, (int)cy, false);
  } else if (f.kind == art::LK_PROGRESS) {
    const int n = (f.x1 - f.x0 + 1) * (v.ss * 1000 + (int)(ms_ % 1000)) / 60000;
    for (int x = f.x0; x < f.x0 + n; x++) px(x, f.y0, true);
  } else if (f.kind == art::LK_MONTH) {
    const int cw = f.p[0], ch = f.p[1], y0 = f.p[2];
    for (int d = 1; d <= v.days; d++) {
      const int i = v.firstWd + d - 1, col = i % 7, row = i / 7;
      const int cx = f.x0 + col * cw + cw / 2, y = y0 + row * ch;
      const std::string s = fmt("%d", d);
      if (d == v.today) {
        rrectFill(cx - 7, y - 1, cx + 7, y + 5, 2, true);
        text(T5, s, cx + 1, y, 1, false);
      } else {
        text(T5, s, cx + 1, y, 1, true);
      }
    }
  }
}

// OLED wear: a screen marked "drift" moves round a 2-px square, a step every 3 minutes.
void drift() {
  static const int8_t DX[8] = {0, 2, 2, 2, 0, -2, -2, -2}, DY[8] = {0, 0, 2, -2, -2, -2, 0, 2};
  const int k = (int)(ms_ / 180000 % 8);
  shift(DX[k], DY[k]);
}

void drawStandby() {
  const auto& r = rotation();
  if (rotIdx >= (int)r.size()) startRot(0);
  if (ms_ - rotAt >= r[rotIdx].secs * 1000u || (!noneShown && !shown(r[rotIdx].id))) startRot(rotIdx + 1);
  const art::Standby& S = art::STANDBY[cur];
  pl.draw();
  Vals v;
  buildVals(v);
  for (int i = S.live0; i < S.live1; i++) drawLive(art::LIVE[i], v, ms_ - rotAt);
  if (S.ovClear >= 0) {  // drawn over the live fields (the flip clock's split line)
    static uint8_t m[sizeof fb];
    frameInto(S.ovClear, m);
    for (size_t i = 0; i < sizeof fb; i++) fb[i] &= ~m[i];
    frameInto(S.ovSet, m);
    for (size_t i = 0; i < sizeof fb; i++) fb[i] |= m[i];
  }
  if (S.drift) drift();
}

// No WiFi: three arcs and a slash, 9x8, in the top right corner (cleared around it so it reads on any picture).
void offlineMark() {
  static const char* const M[8] = {"..#####..", ".#.....#.", "#..###..#", "..#...#..", "....#....", "....#....",
                                    ".........", "........."};
  fill(117, 0, 127, 9, false);
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 9; x++)
      if (M[y][x] == '#') px(118 + x, 1 + y, true);
  for (int i = 0; i < 9; i++) px(118 + i, 1 + i * 7 / 8, true), px(118 + i, 1 + i * 7 / 8 + 1, false);
}

void drawNight() {
  clear();
  const std::string t = now_.valid ? fmt("%02d:%02d", now_.hour, now_.min) : std::string("--:--");
  const int w = textW(F16B, t), h = 19;
  // a new place every two minutes, all over the panel
  uint32_t k = (uint32_t)(ms_ / 120000) * 2654435761u;
  k ^= k >> 13;
  const int x = (int)(k % (uint32_t)(W - w)), y = (int)((k >> 8) % (uint32_t)(H - h));
  text(F16B, t, x, y);
}

// ---------------------------------------------------------------- setup, claim, alarm, note
std::string apSsid, apPass, claimCode, claimUrl, alarmLabel, noteTitle, noteLine;
bool both = false, alarmTimer = false;
int alarmH = 0, alarmM = 0, notePct = -1;

std::string qrEsc(const std::string& v) {
  std::string e;
  for (char c : v) {
    if (strchr("\\;,:\"", c)) e += '\\';
    e += c;
  }
  return e;
}
std::string wifiQr(const std::string& ssid, const std::string& pass) {
  return pass.empty() ? "WIFI:T:nopass;S:" + qrEsc(ssid) + ";;" : "WIFI:T:WPA;S:" + qrEsc(ssid) + ";P:" + qrEsc(pass) + ";;";
}

// A QR code at the left, as large as fits: its width (0: none fits, the words take the panel).
int qrLeft(const std::string& s) {
  for (int scale = 2; scale >= 1; scale--) {
    const int n = qrSize(s, scale);
    if (n && n <= H) return qr(s, 0, (H - n) / 2, scale);
  }
  return 0;
}

struct Line {
  Font f;
  std::string s;
};
// Lines centred on x in a column, the block centred top to bottom.
int rowH(const Line& l) { return l.f == F16B ? 17 : lineH(F11); }  // F16B: codes and titles, no descenders
void column(const std::vector<Line>& ls, int x) {
  int h = 0;
  for (const Line& l : ls) h += rowH(l);
  int y = (H - h) / 2 + 1;
  for (const Line& l : ls) {
    text(l.f, l.s, x, y, 1);
    y += rowH(l);
  }
}
void addWrapped(std::vector<Line>& ls, const std::string& s, int w) {
  for (const std::string& l : wrap(F11, s, w)) ls.push_back({F11, l});
}

void drawSetupAp() {
  clear();
  const int q = qrLeft(wifiQr(apSsid, apPass));
  const int x0 = q ? q + 3 : 0, w = W - x0;
  std::vector<Line> ls;
  addWrapped(ls, tr(S_JOIN_WIFI), w - 2);
  ls.push_back({F11, apSsid});
  if (!apPass.empty()) {
    ls.push_back({F11, tr(S_PASSWORD)});
    ls.push_back({F11, apPass});
  } else {
    addWrapped(ls, tr(S_TO_SET_UP), w - 2);
  }
  column(ls, x0 + w / 2);
}

void drawClaim() {
  clear();
  const int q = claimUrl.empty() ? 0 : qrLeft(claimUrl);
  const int x0 = q ? q + 3 : 0, w = W - x0;
  std::vector<Line> ls;
  ls.push_back({F11, "espos.dev"});
  if (claimCode.size() == 8 && textW(F16B, claimCode) > w - 2) {
    ls.push_back({F16B, claimCode.substr(0, 4)});
    ls.push_back({F16B, claimCode.substr(4)});
  } else {
    const std::string c = claimCode.size() == 8 ? claimCode.substr(0, 4) + "-" + claimCode.substr(4) : claimCode;
    if (covers(F16B, c) && textW(F16B, c) <= w - 2) ls.push_back({F16B, c});
    else addWrapped(ls, c, w - 2);
  }
  addWrapped(ls, tr(S_ADD_DEVICE), w - 2);
  column(ls, x0 + w / 2);
}

void drawAlarm() {
  clear();
  const std::string t = fmt("%02d:%02d", alarmH, alarmM);
  digits(art::DS_BOLD40, t, 64, 2, 1);
  const std::string label = alarmLabel.empty() ? tr(alarmTimer ? S_TIMER : S_ALARM) : alarmLabel;
  std::vector<std::string> ls = wrap(F11, label, 124);
  int y = ls.size() > 1 ? 40 : 46;
  for (size_t i = 0; i < ls.size() && i < 2; i++, y += lineH(F11)) text(F11, ls[i], 64, y, 1);
  if ((ms_ - t0) % 1000 >= 500) invert();
}

void drawNote() {
  clear();
  std::vector<Line> ls;
  if (covers(F16B, noteTitle) && textW(F16B, noteTitle) <= 124) ls.push_back({F16B, noteTitle});
  else addWrapped(ls, noteTitle, 124);
  if (!noteLine.empty()) addWrapped(ls, noteLine, 124);
  int h = 0;
  for (const Line& l : ls) h += l.f == F16B ? 19 : lineH(F11);
  if (notePct >= 0) h += 10;
  int y = (H - h) / 2 + 1;
  for (const Line& l : ls) {
    text(l.f, l.s, 64, y, 1);
    y += l.f == F16B ? 19 : lineH(F11);
  }
  if (notePct >= 0) {
    const int x0 = 14, x1 = 113, yb = y + 3;
    rect(x0, yb, x1, yb + 6);
    const int fw = (x1 - x0 - 3) * clampi(notePct, 0, 100) / 100;
    if (fw > 0) fill(x0 + 2, yb + 2, x0 + 1 + fw, yb + 4, true);
  }
}

void enter(State s) {
  state_ = s;
  t0 = ms_;
  clipAll();
}

uint8_t prev[sizeof(fb)];
bool first = true;

}  // namespace

const char* str(Str s) { return s < S_COUNT_ ? (lang_ == Lang::VI ? STR[s].vi : STR[s].en) : ""; }

void setLook(uint8_t l) {
  if (l >= art::LOOK_COUNT) l = 0;
  if (l == look_) return;
  look_ = l;
  if (pl.sc) {  // the same scene in the new look
    for (int id = 0; id < art::SC_COUNT; id++)
      for (int k = 0; k < art::LOOK_COUNT; k++)
        if (pl.sc == &art::SCENES[k][id]) pl.sc = &art::SCENES[l][id];
  }
  if (state_ == State::TALK) layoutTalk(std::string(tk.words));  // the words wrap again in the new box
}
uint8_t look() { return look_; }
void setLang(Lang l) { lang_ = drawLang = l; }
Lang lang() { return lang_; }
void setNow(const Now& n) { now_ = n; }
void setWeather(const Weather& w) { wx_ = w; }
void setInfo(const Info& i) { info_ = i; }
void setOffline(bool on) { offline_ = on; }

int16_t screenIndex(const std::string& id) {
  if (id == "weather") return SCR_WEATHER_AUTO;
  const std::string k = id == "clock" ? "clock_bold" : id == "date" ? "date_page" : id == "dog" ? "eyes" : id;
  for (int i = 0; i < art::STANDBY_COUNT; i++)
    if (k == art::STANDBY[i].id) return (int16_t)i;
  return -2;
}
const char* standbyId() { return art::STANDBY[cur].id; }
const char* screenId(int16_t i) { return i == SCR_WEATHER_AUTO ? "weather" : i >= 0 && i < art::STANDBY_COUNT ? art::STANDBY[i].id : ""; }

void setScreens(const std::vector<Screen>& s) {
  screens_.clear();
  for (const Screen& x : s)
    if ((x.id == SCR_WEATHER_AUTO || (x.id >= 0 && x.id < art::STANDBY_COUNT)) && x.secs) screens_.push_back(x);
  rotIdx = 0;
  if (state_ == State::STANDBY) startRot(0);
}

State state() { return state_; }
void standby() {
  if (state_ == State::STANDBY) return;
  enter(State::STANDBY);
  startRot(0);
}
void night() { enter(State::NIGHT); }
void listen() {
  enter(State::LISTEN);
  pl.start(art::SC_LISTEN, true);
}
void think() {
  enter(State::THINK);
  pl.start(art::SC_THINK, true);
}
void talk(const std::string& words, Mood mood) {
  enter(State::TALK);
  tk.mood = mood;
  layoutTalk(words);
  pl.start(art::SC_TALK, true);
}
bool talkDone() {
  if (state_ == State::SAD) return false;  // the sad face first, then its words
  return state_ != State::TALK || ms_ - t0 >= tk.doneMs;
}
void happy() {
  enter(State::HAPPY);
  pl.start(art::SC_HAPPY, true);
}
void notHeard() {
  enter(State::SAD);
  sadWords = str(S_NOT_HEARD);
  pl.start(art::SC_SAD, false);
}
void setupAp(const std::string& ssid, const std::string& pass, bool bothLangs) {
  enter(State::SETUP_AP);
  apSsid = ssid, apPass = pass, both = bothLangs;
}
void claim(const std::string& code, const std::string& url, bool bothLangs) {
  enter(State::CLAIM);
  claimCode = code, claimUrl = url, both = bothLangs;
}
void alarm(const std::string& label, int h, int m, bool timer) {
  enter(State::ALARM);
  alarmLabel = label, alarmH = h, alarmM = m, alarmTimer = timer;
}
void note(const std::string& title, const std::string& line, int pct) {
  if (state_ != State::NOTE) enter(State::NOTE);
  noteTitle = title, noteLine = line, notePct = pct;
}

bool tick(uint32_t ms) {
  ms_ = ms;
  drawLang = lang_;
  if (both && (state_ == State::SETUP_AP || state_ == State::CLAIM)) drawLang = (ms / 4000) % 2 ? Lang::VI : Lang::EN;
  clipAll();
  switch (state_) {
    case State::STANDBY:
      drawStandby();
      if (offline_) offlineMark();
      break;
    case State::NIGHT:
      drawNight();
      if (offline_) offlineMark();
      break;
    case State::LISTEN:
      pl.draw();
      break;
    case State::THINK:
    case State::HAPPY: pl.draw(); break;
    case State::SAD:
      if (pl.done()) {  // the sad face has played: the words
        const std::string w = sadWords;
        talk(w, Mood::SAD);
        drawTalk();
      } else {
        pl.draw();
      }
      break;
    case State::TALK: drawTalk(); break;
    case State::SETUP_AP: drawSetupAp(); break;
    case State::CLAIM: drawClaim(); break;
    case State::ALARM: drawAlarm(); break;
    case State::NOTE: drawNote(); break;
  }
  const bool changed = first || memcmp(prev, fb, sizeof fb);
  if (changed) memcpy(prev, fb, sizeof fb), first = false;
  return changed;
}

std::string dateLine(const Now& n, int maxW) {
  const int wd = wdayOf(n), mo = monOf(n);
  if (drawLang == Lang::VI) {
    const std::string a = std::string(WD_VI[wd]) + fmt(", %d tháng %d", n.day, n.mon);
    return textW(F11, a) <= maxW ? a : std::string(WD_VI2[wd]) + fmt(", %d/%d", n.day, n.mon);
  }
  const std::string a = std::string(WD_EN[wd]) + fmt(", %d ", n.day) + MON_EN[mo];
  return textW(F11, a) <= maxW ? a : std::string(WD_EN3[wd]) + fmt(", %d ", n.day) + MON_EN3[mo];
}

std::string weatherWords(const Weather& w) {
  if (drawLang == Lang::VI && !w.desc.empty()) return w.desc;
  const std::string& k = w.icon;
  const bool vi = drawLang == Lang::VI;
  if (k == "sun") return vi ? "Nắng" : "Sunny";
  if (k == "moon") return vi ? "Trời quang" : "Clear";
  if (k == "cloudsun") return vi ? "Ít mây" : "Partly cloudy";
  if (k == "cloud") return vi ? "Nhiều mây" : "Cloudy";
  if (k == "fog") return vi ? "Sương mù" : "Fog";
  if (k == "rain") return vi ? "Mưa" : "Rain";
  if (k == "snow") return vi ? "Tuyết" : "Snow";
  if (k == "storm") return vi ? "Dông" : "Thunderstorm";
  return "";
}

}  // namespace ui
}  // namespace bot
