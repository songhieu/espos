#pragma once
// The options of one widget as rows of pills, built from the registry (wid.h: styles, sizes, flags, the option
// kind, skins): Kiểu, Cỡ, the on/off options, the option picker (market sets, a note, a photo, a my-widget, cities,
// a news section, a date and its name) and Thẻ (the card look). Home's edit mode (light sheet) and the saver editor
// (dark sheet) both show it; each passes its colours and gets the changed item back.
#include <lvgl.h>

#include "wid.h"

namespace widopts {

struct Look {
  uint32_t text, dim, pill, pillText, on, onText;
};

// it: the item as it is now. cb(item, sizeChanged) after every change, from a pill's event: the caller stores the
// item and redraws later (the rows are rebuilt by the caller, never inside cb). sizes: show the Cỡ row.
using Changed = void (*)(const wid::Item& it, bool sizeChanged);
void build(lv_obj_t* parent, const wid::Item& it, const Look& look, int width, Changed cb, bool sizes = true);

// The market codes picker alone, as in a markets widget's sheet and the web's MarketCodes: quick sets, or codes one by
// one (PNJ gold, indices, stocks typed on the keyboard, Vietcombank rates, coins) in the order picked, at most `most`.
// Used by the Markets app for s3.markets. cb(value) after each change; the caller rebuilds the rows (not inside cb).
void codes(lv_obj_t* parent, const String& value, const Look& look, int width, void (*cb)(const String& value), int most);

}  // namespace widopts
