// Host build of FreeInkFont's allocator: plain malloc.
#include <FontAlloc.h>

#include <cstdlib>

extern "C" {
void* fiFontMalloc(size_t size) { return malloc(size); }
void* fiFontRealloc(void* ptr, size_t size) { return realloc(ptr, size); }
void fiFontFree(void* ptr) { free(ptr); }
}
