// The firmware's LVGL configuration, plus what the simulator needs.
#include "../../include/lv_conf.h"
#undef LV_USE_TJPGD
#define LV_USE_TJPGD 1
#undef LV_USE_OS
#define LV_USE_OS LV_OS_NONE
#undef LV_DRAW_SW_DRAW_UNIT_CNT
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
