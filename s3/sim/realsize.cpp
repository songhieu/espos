// sim/realsize.h: the board's screen at its real size on this computer's screen.
#include "realsize.h"

#include <SDL.h>

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif

float realZoom(int index) {
#ifdef __APPLE__
  SDL_Rect b;
  if (SDL_GetDisplayBounds(index, &b) != 0) return 0;
  CGDirectDisplayID ids[16];
  uint32_t n = 0;
  if (CGGetActiveDisplayList(16, ids, &n) != kCGErrorSuccess) return 0;
  for (uint32_t i = 0; i < n; i++) {
    const CGRect r = CGDisplayBounds(ids[i]);  // points, the same space as SDL's display bounds
    if ((int)r.origin.x != b.x || (int)r.origin.y != b.y) continue;
    const CGSize mm = CGDisplayScreenSize(ids[i]);
    if (mm.width <= 0) return 0;
    return (float)(r.size.width / mm.width) * PANEL_MM_PER_PX;
  }
#endif
  return 0;
}
