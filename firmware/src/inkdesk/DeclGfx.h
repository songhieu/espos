#pragma once

#include <string>

#include "inkdesk/Pkgs.h"
#include "inkdesk/Widgets.h"

class GfxRenderer;

// Declarative packages (inkdesk/Decl.h) drawn on the X4's e-ink screen.
namespace inkdesk::decl {

// Draws an installed face over the whole screen: the layout of `face`'s size, or its first one. Sets the
// renderer orientation to the layout's shape and leaves it set. `live`: the screen is redrawn every minute
// (the face may show the time). False when the package is missing or broken (the caller draws something else).
bool drawFace(GfxRenderer& r, const pkgs::FaceRef& face, bool live);

// A widget package inside a Home or sleep-grid box (the caller draws the frame).
void drawWidget(GfxRenderer& r, const std::string& id, const widgets::Box& b, const widgets::Data& d);

}  // namespace inkdesk::decl
