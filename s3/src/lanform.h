#pragma once
// Settings and secrets from a computer on the same Wi-Fi (app-slots.md §15.1, lane 1f): a screen with a QR code, the
// address and a 6-digit PIN; while it is open the device serves a one-time page (port 8080) with the app's address
// and text fields and its device-only secrets. What is posted is checked and stored by appset (appset::set,
// appset::setSecret) on the UI loop. The page closes after one save, after 10 minutes, after 5 wrong PINs, or with the
// screen. Nothing goes through EspOS's servers; values are never traced.
namespace lanform {
void open(const char* appId);
}
