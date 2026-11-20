#include "media.h"

#include <map>
#include <vector>

#include "desk.h"
#include "jpeg.h"
#include "trace.h"
#include "uikit.h"

namespace media {
namespace {

enum class St { Wait, Ok, Fail };

struct Entry {
  String url;
  int w, h;
  St st = St::Wait;
  uint16_t* px = nullptr;
  lv_image_dsc_t dsc = {};
  uint32_t used = 0;
  int refs = 0;                    // image objects showing it
  std::vector<lv_obj_t*> waiting;  // boxes to fill when it arrives
};
std::map<String, Entry*> cache;
size_t bytes = 0;
constexpr size_t LIMIT = 1536 * 1024;

String keyOf(const String& url, int w, int h) {
  const int q = url.indexOf('?');
  return (q >= 0 ? url.substring(0, q) : url) + "#" + w + "x" + h;
}

void show(lv_obj_t* boxObj, Entry* e) {
  lv_obj_t* i = lv_image_create(boxObj);
  lv_image_set_src(i, &e->dsc);
  lv_obj_center(i);
  e->refs++;
  lv_obj_add_event_cb(i, [](lv_event_t* ev) {
    Entry* e = (Entry*)lv_event_get_user_data(ev);
    e->refs--;
  }, LV_EVENT_DELETE, e);
}

// A box waiting for its picture is deleted (its screen closed): it stops waiting; nobody waits: not downloaded.
void waitDeleted(lv_event_t* ev) {
  Entry* e = (Entry*)lv_event_get_user_data(ev);
  lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(ev);
  for (auto it = e->waiting.begin(); it != e->waiting.end(); ++it)
    if (*it == o) {
      e->waiting.erase(it);
      break;
    }
  if (e->waiting.empty() && e->st == St::Wait) {
    desk::cancelFetches(e);
    e->st = St::Fail;
  }
}

void arrived(void* ctx, int status, JsonDocument*, const uint8_t* data, size_t len) {
  Entry* e = (Entry*)ctx;
  if (status == 200 && data && len) {
    const uint32_t t0 = millis();
    e->px = jpeg::cover(data, len, e->w, e->h);
    if (e->px) {
      e->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      e->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
      e->dsc.header.w = e->w;
      e->dsc.header.h = e->h;
      e->dsc.header.stride = e->w * 2;
      e->dsc.data_size = e->w * e->h * 2;
      e->dsc.data = (const uint8_t*)e->px;
      e->st = St::Ok;
      bytes += e->dsc.data_size;
      trace::add("media %dx%d decoded in %lu ms (%u KB cached)", e->w, e->h, millis() - t0, (unsigned)(bytes / 1024));
    } else {
      e->st = St::Fail;
    }
  } else {
    e->st = St::Fail;
  }
  // The boxes stop waiting: their delete callback goes, so a failed entry that trim() deletes is never reached from a
  // box deleted later (a board crashed there, 0.5.7: "Cache error" in this callback during a Home rebuild after failed
  // downloads).
  for (lv_obj_t* b : e->waiting) {
    lv_obj_remove_event_cb_with_user_data(b, waitDeleted, e);
    if (e->st == St::Ok) show(b, e);
  }
  e->waiting.clear();
  trim();
}

}  // namespace

lv_obj_t* image(lv_obj_t* parent, const String& url, int w, int h, int radius, uint32_t placeholder) {
  lv_obj_t* b = kit::box(parent, placeholder, radius);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_clip_corner(b, radius > 0, 0);
  if (!url.length()) return b;
  const String key = keyOf(url, w, h);
  Entry*& e = cache[key];
  if (!e) {
    e = new Entry();
    e->url = url, e->w = w, e->h = h;
    desk::fetch(url, true, arrived, e);
  }
  e->used = millis();
  if (e->st == St::Ok) {
    show(b, e);
  } else if (e->st == St::Wait) {
    e->waiting.push_back(b);
    lv_obj_add_event_cb(b, waitDeleted, LV_EVENT_DELETE, e);
  }
  return b;
}

void trim() {
  while (bytes > LIMIT) {
    auto oldest = cache.end();
    for (auto it = cache.begin(); it != cache.end(); ++it)
      if (it->second->st == St::Ok && it->second->refs == 0 &&
          (oldest == cache.end() || it->second->used < oldest->second->used))
        oldest = it;
    if (oldest == cache.end()) break;
    bytes -= oldest->second->dsc.data_size;
    heap_caps_free(oldest->second->px);
    delete oldest->second;
    cache.erase(oldest);
  }
  // Failed ones may be tried again later.
  for (auto it = cache.begin(); it != cache.end();)
    if (it->second->st == St::Fail && it->second->waiting.empty()) {
      delete it->second;
      it = cache.erase(it);
    } else {
      ++it;
    }
}

}  // namespace media
