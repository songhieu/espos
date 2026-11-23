#pragma once
// ui.term: a terminal view for JavaScript apps (API level 2, app-slots.md §15.4), first used by the Sessions app
// (dev.espos.sessions) to show a Claude Code session streamed from a Mac.
//
//   ui.term({key: "t", font: 12, wrap: "fit" | "wide", fill: true, onTap, onScroll, pill: "↓ {n} new lines"})
//   term.apply("t", text)   // a screen answer (desktop-protocol.md §9.2) as JSON text, parsed here, not in JS
//   term.clear(k) term.end(k) term.pan(k, cells) term.info(k) term.tail(k, lines)
//
// The lines live in PSRAM, outside the JS heap (at most `lines`, default 2000, 5000 at most), keyed by their logical
// number `n`; a line sent again replaces the old one. "fit" wraps each line to the view (hanging indent after
// Claude's marks; boxes drawn again at the view's width); "wide" keeps the sender's columns and pans sideways. The
// view follows the end until the person drags up; then `pill` ({n} = the lines that came since) shows at the bottom
// and a tap on it goes back to the end. Colours: xterm 16/256 and truecolor runs, made readable on the OS theme.
// The mono font comes from the app's package (fonts/m<px>.bin, b<px>.bin: tools in apps/sessions/tools/mkfonts.py);
// letters it lacks come from the OS's fonts, centred in their cell.
extern "C" {
#include "quickjs.h"
}

namespace pkg {
struct Package;
}

namespace jsterm {
void install(JSContext* ctx, const pkg::Package& p);  // the app opened: its natives (__termApply...)
void stop();                                         // the app is closing: buffers and fonts go
}  // namespace jsterm
