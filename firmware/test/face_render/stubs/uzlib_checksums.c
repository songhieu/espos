/* The vendored uzlib has no checksum sources; glyph groups are raw DEFLATE and never call these. */
#include <uzlib.h>

uint32_t uzlib_adler32(const void* data, unsigned int length, uint32_t prev_sum) {
  (void)data;
  (void)length;
  return prev_sum;
}
uint32_t uzlib_crc32(const void* data, unsigned int length, uint32_t crc) {
  (void)data;
  (void)length;
  return crc;
}
