#pragma once
// voice.dictate / voice.stop for JavaScript apps (permission "dictate", app-slots.md §15.4): speech to text through the
// OS. The OS records (src/wake capture: the mic dot shows, the wake word holds), draws its own "Listening" card with
// the level, sends the WAV to the account's server (POST /v1/device/dictate?lang=, the device's own credential, metered
// like an assistant turn) and gives the app the text only: the app never gets the audio.
//
//   voice.dictate({lang: "en", max_ms: 30000}).then(({text}) => ..., (e) => e.code)
//   voice.stop()   // ends the recording now (hold to talk: on the button's onUp); the text still comes
//   voice.dictate({expect: "yesno", about: "<what was read back>", until_quiet: true}) -> {text, yesno}: a spoken yes/no
//   (desktop-protocol.md §11 voice confirm): yesno is confirm|cancel|unclear|other, decided by the server from the words
//
// Codes: denied (no "dictate"), no_mic, busy (the assistant is talking, or one is running), no_room (PSRAM), offline,
// locked (402: the OS's lock sheet shows), failed, cancelled (the app closed).
extern "C" {
#include "quickjs.h"
}

namespace pkg {
struct Package;
}

namespace jsvoice {
void install(JSContext* ctx, const pkg::Package& p);
void stop();
}  // namespace jsvoice
