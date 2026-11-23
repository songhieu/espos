#pragma once
// Server notes for an installed app, as that app's notifications and pop-ups (app-slots.md §6 rules; the first user:
// Claude asking for permission while the Sessions app is closed, desktop-protocol.md §9.6).
//
// A dashboard note with "app" (an installed package id) becomes a notification of that app (tag: its "ref", else
// "arg"; a tap opens the app with "arg"), once, and goes away when the note leaves the dashboard. A note with "acts"
// also raises a card over whatever is on screen (Home, the saver, the aquarium, another app) when the app has the
// "overlay" permission, its notifications are on, it is not quiet hours or Do Not Disturb, the app is not open, and
// no assistant turn or alarm is in front. Several notes of one app stack on one card ("+2 more waiting").
//
//   {"app": "dev.espos.sessions", "arg": "900:s-1:r-1", "ref": "r-1", "exp_ms": 1795273800000, "title": "...",
//    "pop": {"head": "espos", "title": "Claude wants to run a command", "mono": "git push origin main", "text": "..."},
//    "acts": [{"id": "deny", "post": "/send", "body": {...}}, {"id": "approve", "post": "/send", "body": {...}, "risky": true}]}
//
// Buttons are an OS vocabulary (approve, deny, ok, open), labelled by the device in its language. A tap posts the act's
// body (+ "nonce", + "ack_risky": true after a risky act's second tap) to the app's own server path with the device's credential
// (src/js/jsnet.h osPost): exactly the request the app makes itself. The answer ("state", "text", "err")
// becomes a toast. Nothing is ever done without a tap.
#include <ArduinoJson.h>

namespace popnote {
void sync(JsonArrayConst notes);  // the dashboard's notes changed (notif.cpp importDesk)
void tick();                      // every loop (notif::tick): countdowns, expiry, waiting cards
}  // namespace popnote
