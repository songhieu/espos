#pragma once
/* The memory pool of LVGL's heap (lv_conf.h): PSRAM on the board, plain malloc on a computer. */
#include <stddef.h>
#ifdef SIM
#include <stdlib.h>
static inline void* lv_pool_alloc(size_t n) { return malloc(n); }
#else
#include "esp_heap_caps.h"
static inline void* lv_pool_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
#endif
