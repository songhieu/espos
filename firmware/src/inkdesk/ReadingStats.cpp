#include "ReadingStats.h"

#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "InkDeskStore.h"

namespace inkdesk::stats {
namespace {

constexpr const char* LOG_FILE = "/.crosspoint/inkdesk_reading.log";
constexpr const char* DAYS_FILE = "/.crosspoint/inkdesk_reading.json";
constexpr uint32_t MIN_DWELL_S = 2;
constexpr uint32_t MAX_DWELL_S = 600;
constexpr uint32_t MIN_SESSION_S = 10;
constexpr size_t KEEP_DAYS = 60;
constexpr size_t DETAIL_DAYS = 14;  // per-book detail kept this long (and while unsent)
constexpr size_t MAX_BOOKS_PER_DAY = 8;
constexpr size_t SYNC_DAYS = 10;
constexpr size_t SYNC_ENTRIES = 40;
constexpr size_t LOG_CAP = 16 * 1024;
constexpr size_t DAYS_CAP = 24 * 1024;

struct BookTotal {
  std::string key;  // "id:<n>" for InkDesk books, else the path
  int32_t id = 0;
  std::string title;
  uint32_t sec = 0;
  uint32_t pages = 0;
};

struct Day {
  std::string day;  // "YYYY-MM-DD", local
  uint32_t sec = 0;
  uint32_t pages = 0;
  bool unsent = false;
  std::vector<BookTotal> books;
};

// Local date `back` days before today ("" without a clock).
std::string localDay(const int back = 0) {
  if (!InkDeskStore::clockValid()) return {};
  const int64_t now = InkDeskStore::nowMs();
  if (now <= 0) return {};
  const time_t t = static_cast<time_t>(now / 1000 + static_cast<int64_t>(INKDESK.tzMin) * 60 - back * 86400LL);
  struct tm tm {};
  gmtime_r(&t, &tm);
  if (tm.tm_year + 1900 < 2024) return {};
  char buf[12];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return buf;
}

std::string clean(const std::string& s, const size_t max) {
  std::string out;
  out.reserve(std::min(s.size(), max));
  for (const char c : s) {
    if (out.size() >= max) break;
    out.push_back(c == '\t' || c == '\n' || c == '\r' ? ' ' : c);
  }
  // Do not end inside a UTF-8 sequence.
  while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
  if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0xC0) out.pop_back();
  return out;
}

std::vector<Day> loadDays() {
  std::vector<Day> days;
  std::string raw;
  if (!Storage.exists(DAYS_FILE) || !Storage.readFileToString("STAT", DAYS_FILE, DAYS_CAP, raw)) return days;
  JsonDocument doc;
  if (deserializeJson(doc, raw)) return days;
  raw.clear();
  for (JsonObjectConst o : doc["days"].as<JsonArrayConst>()) {
    Day d;
    d.day = o["d"] | "";
    d.sec = o["s"] | 0u;
    d.pages = o["p"] | 0u;
    d.unsent = o["u"] | false;
    for (JsonObjectConst b : o["b"].as<JsonArrayConst>()) {
      BookTotal t;
      t.key = b["k"] | "";
      t.id = b["i"] | 0;
      t.title = b["t"] | "";
      t.sec = b["s"] | 0u;
      t.pages = b["p"] | 0u;
      d.books.push_back(std::move(t));
    }
    if (!d.day.empty()) days.push_back(std::move(d));
  }
  return days;
}

bool saveDays(std::vector<Day>& days) {
  std::sort(days.begin(), days.end(), [](const Day& a, const Day& b) { return a.day < b.day; });
  if (days.size() > KEEP_DAYS) days.erase(days.begin(), days.end() - KEEP_DAYS);
  const std::string detailFrom = localDay(DETAIL_DAYS);
  JsonDocument doc;
  JsonArray arr = doc["days"].to<JsonArray>();
  for (const auto& d : days) {
    JsonObject o = arr.add<JsonObject>();
    o["d"] = d.day;
    o["s"] = d.sec;
    o["p"] = d.pages;
    if (d.unsent) o["u"] = true;
    if (!d.unsent && !detailFrom.empty() && d.day < detailFrom) continue;
    JsonArray ba = o["b"].to<JsonArray>();
    for (const auto& b : d.books) {
      JsonObject bo = ba.add<JsonObject>();
      bo["k"] = b.key;
      if (b.id > 0) bo["i"] = b.id;
      bo["t"] = b.title;
      bo["s"] = b.sec;
      bo["p"] = b.pages;
    }
  }
  return PersistableStoreBase::writeDocToFile(DAYS_FILE, doc);
}

Day& dayFor(std::vector<Day>& days, const std::string& day) {
  for (auto& d : days) {
    if (d.day == day) return d;
  }
  days.push_back(Day{});
  days.back().day = day;
  return days.back();
}

void addTo(Day& d, const BookTotal& s) {
  d.sec += s.sec;
  d.pages += s.pages;
  d.unsent = true;
  for (auto& b : d.books) {
    if (b.key == s.key) {
      b.sec += s.sec;
      b.pages += s.pages;
      if (!s.title.empty()) b.title = s.title;
      return;
    }
  }
  // Past this many books the day total stays right but the extra book is not listed.
  if (d.books.size() < MAX_BOOKS_PER_DAY) d.books.push_back(s);
}

std::string bookKey(const std::string& path, int32_t& id) {
  const std::string prefix = std::string(BOOKS_DIR) + "/";
  if (path.compare(0, prefix.size(), prefix) == 0) {
    const std::string file = path.substr(prefix.size());
    for (const auto& b : INKDESK.books) {
      if (b.file == file) {
        id = b.id;
        return "id:" + std::to_string(b.id);
      }
    }
  }
  return clean(path, 120);
}

}  // namespace

void Tracker::onRender(const uint32_t monotonicMs) {
  if (pendingForward_ && hasAnchor_) {
    const uint32_t elapsed = (monotonicMs - anchorMs_) / 1000;
    if (elapsed >= MIN_DWELL_S) {
      seconds_ += std::min(elapsed, MAX_DWELL_S);
      pages_++;
    }
  }
  anchorMs_ = monotonicMs;
  hasAnchor_ = true;
  pendingForward_ = false;
}

void Tracker::flush(const std::string& bookPath, const std::string& title) {
  const uint32_t seconds = seconds_;
  const uint32_t pages = pages_;
  *this = Tracker{};
  if (seconds < MIN_SESSION_S) return;
  int32_t id = 0;
  const std::string key = bookKey(bookPath, id);
  char head[48];
  snprintf(head, sizeof(head), "%s\t%ld\t%lu\t%lu\t", localDay().c_str(), static_cast<long>(id),
           static_cast<unsigned long>(seconds), static_cast<unsigned long>(pages));
  const std::string line = head + key + "\t" + clean(title, 80) + "\n";
  HalFile f = Storage.open(LOG_FILE, O_RDWR | O_CREAT | O_AT_END);
  if (!f) return;
  if (f.fileSize() < LOG_CAP) f.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
  f.close();
  LOG_INF("STAT", "Read %lu s, %lu pages", static_cast<unsigned long>(seconds), static_cast<unsigned long>(pages));
}

int goalMinutes() {
  const int g = INKDESK.getInt("read.goal_min", 30);
  return g >= 5 && g <= 240 ? g : 30;
}

void compact() {
  const std::string today = localDay();
  if (today.empty() || !Storage.exists(LOG_FILE)) return;
  std::string raw;
  if (!Storage.readFileToString("STAT", LOG_FILE, LOG_CAP + 1024, raw)) return;
  std::vector<Day> days = loadDays();
  size_t pos = 0;
  int folded = 0;
  while (pos < raw.size()) {
    size_t end = raw.find('\n', pos);
    if (end == std::string::npos) end = raw.size();
    const std::string line = raw.substr(pos, end - pos);
    pos = end + 1;
    // day \t id \t seconds \t pages \t key \t title
    std::vector<std::string> f;
    size_t s = 0;
    for (int i = 0; i < 5; i++) {
      const size_t t = line.find('\t', s);
      if (t == std::string::npos) break;
      f.push_back(line.substr(s, t - s));
      s = t + 1;
    }
    if (f.size() < 5) continue;
    f.push_back(line.substr(s));
    BookTotal b;
    b.id = atoi(f[1].c_str());
    b.sec = strtoul(f[2].c_str(), nullptr, 10);
    b.pages = strtoul(f[3].c_str(), nullptr, 10);
    b.key = f[4];
    b.title = f[5];
    if (b.sec == 0 || b.key.empty()) continue;
    // Read before the clock was set: it lands on today.
    addTo(dayFor(days, f[0].size() == 10 ? f[0] : today), b);
    folded++;
  }
  if (saveDays(days)) {
    Storage.remove(LOG_FILE);
    LOG_DBG("STAT", "Folded %d sessions", folded);
  }
}

Summary summary() {
  compact();
  Summary out;
  out.goalMin = goalMinutes();
  const std::string today = localDay();
  out.dated = !today.empty();
  if (!out.dated) return out;
  const std::vector<Day> days = loadDays();
  auto secOf = [&](const std::string& day) -> uint32_t {
    for (const auto& d : days) {
      if (d.day == day) return d.sec;
    }
    return 0;
  };
  struct tm tm {};
  const time_t t = static_cast<time_t>(InkDeskStore::nowMs() / 1000 + static_cast<int64_t>(INKDESK.tzMin) * 60);
  gmtime_r(&t, &tm);
  out.todayWday = tm.tm_wday;
  for (int k = 6; k >= 0; k--) {
    out.last7[6 - k] = secOf(localDay(k));
    if (out.last7[6 - k] > 0) out.recent = true;
  }
  for (const auto& d : days) {
    if (d.day == today) {
      out.todaySec = d.sec;
      out.todayPages = d.pages;
    }
  }
  // Week from Monday.
  const int sinceMonday = (tm.tm_wday + 6) % 7;
  for (int k = 0; k <= sinceMonday; k++) out.weekSec += secOf(localDay(k));
  // Month and its most read book.
  const std::string month = today.substr(0, 7);
  std::vector<BookTotal> books;
  for (const auto& d : days) {
    if (d.day.compare(0, 7, month) != 0) continue;
    out.monthSec += d.sec;
    out.monthPages += d.pages;
    for (const auto& b : d.books) {
      auto it = std::find_if(books.begin(), books.end(), [&](const BookTotal& x) { return x.key == b.key; });
      if (it == books.end()) {
        books.push_back(b);
      } else {
        it->sec += b.sec;
      }
    }
  }
  for (const auto& b : books) {
    if (b.sec > out.topSec) {
      out.topSec = b.sec;
      out.topTitle = b.title;
    }
  }
  // Streak: today once reached, then the days before it while each reached the goal.
  const uint32_t goal = static_cast<uint32_t>(out.goalMin) * 60;
  if (out.todaySec >= goal) out.streak = 1;
  for (int k = 1; k < static_cast<int>(KEEP_DAYS); k++) {
    if (secOf(localDay(k)) < goal) break;
    out.streak++;
  }
  return out;
}

void collect(JsonArray out, std::vector<std::string>& sent) {
  compact();
  const std::vector<Day> days = loadDays();
  for (auto it = days.rbegin(); it != days.rend() && sent.size() < SYNC_DAYS; ++it) {
    if (!it->unsent) continue;
    // The server takes at most 50 entries per list; a day goes whole or waits for the next sync.
    if (out.size() + std::max<size_t>(1, it->books.size()) > SYNC_ENTRIES) break;
    sent.push_back(it->day);
    if (it->books.empty()) {
      JsonObject o = out.add<JsonObject>();
      o["d"] = it->day;
      o["k"] = "";
      o["s"] = it->sec;
      o["p"] = it->pages;
      continue;
    }
    for (const auto& b : it->books) {
      JsonObject o = out.add<JsonObject>();
      o["d"] = it->day;
      o["k"] = b.key;
      if (b.id > 0) o["id"] = b.id;
      o["t"] = b.title;
      o["s"] = b.sec;
      o["p"] = b.pages;
    }
  }
}

void markSent(const std::vector<std::string>& sent) {
  if (sent.empty()) return;
  std::vector<Day> days = loadDays();
  for (auto& d : days) {
    if (std::find(sent.begin(), sent.end(), d.day) != sent.end()) d.unsent = false;
  }
  saveDays(days);
}

}  // namespace inkdesk::stats
