#pragma once
// The board's screen at its real size on this computer's screen: the ES3C35P's 3.5" 320x480 panel has pixels
// 0.153 mm apart (48.96 x 73.44 mm). macOS gives each display's size in millimetres and its width in points, so
// points per panel pixel = points per mm x 0.153. Used by the live simulator (sim/live.cpp) and QEMU's viewer
// (tools/qemu/view.cpp). Link sim/realsize.cpp and -framework ApplicationServices.
// The board's pixel pitch, and points per panel pixel for SDL display `index` (0 when unknown): sim/realsize.cpp
// (on its own: the macOS headers clash with ours).
constexpr float PANEL_MM_PER_PX = 0.153f;
float realZoom(int index);
