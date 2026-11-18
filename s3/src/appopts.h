#pragma once
// Settings pages that live in their apps, so everything the web sets for an S3 can be set on the device too
// (docs/settings-parity.md, mockup ~/projects/xt/mockups/s3-settings-parity.html). Each opens from its app
// (a "Sửa"/"Đổi" button) and from Settings. Changes go up with desk::setSetting and a sync right away.
#include <Arduino.h>

namespace app_weather {
void openCity();  // Thành phố: dash.city + dash.lat + dash.lon, from the web's list (places.ts) and world cities
String cityNow();  // the chosen city's name ("" when the server's default)
}  // namespace app_weather

namespace app_calendar {
void openLinks();  // Lịch đã nối: dash.ics_url (one link a line; "#" in front = kept but hidden)
int linkCount();   // links kept (shown and hidden)
}  // namespace app_calendar

namespace app_news {
void openSections();  // Chuyên mục: news.sections, news.per_section (the web's Tin tức page)
int sectionCount();
}  // namespace app_news

namespace app_markets {
void openCodes();  // Mã theo dõi: s3.markets (the codes the Markets app lists), the widget sheet's picker
}  // namespace app_markets
