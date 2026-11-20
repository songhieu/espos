#pragma once
// The EspOS mark (a chip with a stack of layers) and the "EspOS" wordmark, as the brand files draw them
// (brand, README there). A8 pictures from brand_art.c (brand/tools/lvgl.py): LVGL paints an A8
// picture in the image's recolor colour, so the same picture is white on dark and ink on light. The layers are
// holes in the mark: they show whatever is behind it.
#include <lvgl.h>

#include "brand_art.h"

namespace brand {
// The picture (inflated into PSRAM on the first call; a 0 x 0 picture if that fails, never a crash).
const lv_image_dsc_t* dsc(brand_pic p);
// The mark's picture for a size: 96 (splash), 32 (rows), 16 (the small mark, 1 px pins, for tiles of ~28 px).
const lv_image_dsc_t* markDsc(int px);
// An image of the mark at that size (the nearest one made), in this colour.
lv_obj_t* mark(lv_obj_t* parent, int px, uint32_t rgb);
// An image of the wordmark (cap height 32 px, 135 x 44: under the 96 px mark it makes the stacked lockup).
lv_obj_t* word(lv_obj_t* parent, uint32_t rgb);
}  // namespace brand
