#pragma once
// Baseline JPEG decoding (ROM TJpgDec on the device) into RGB565 buffers in PSRAM.
#include <cstddef>
#include <cstdint>

namespace jpeg {

// Picture size from the JPEG header; false when it is not a baseline JPEG.
bool size(const uint8_t* data, size_t len, int& w, int& h);
// Exactly w x h, cover-cropped around the centre (TJpgDec's 1/2/4/8 scaling, then an area
// resample). Free with heap_caps_free. nullptr on failure.
uint16_t* cover(const uint8_t* data, size_t len, int w, int h);

}  // namespace jpeg
