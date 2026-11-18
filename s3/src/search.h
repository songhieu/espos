#pragma once
// Search, as a phone's: pulled down on a Home page (or the field at the top of the App Library). Finds apps,
// Settings pages, reminders and notes as you type; letters without accents match ("thoi tiet" finds "Thời tiết").
#include <Arduino.h>

namespace search {
void open();
// "Thời Tiết" -> "thoi tiet": lower case without Vietnamese accents (for matching).
String fold(const char* s);
}  // namespace search
