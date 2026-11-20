#pragma once
// A unit prepared in Kho máy (the shop's stock page) waits for its owner (admin-users' contract, 2026-11-19; screens
// mockups/espos-claim-device.html). ESPOS:PROVISION over USB wrote its device token (pending on the
// server, which answers 401 not_linked) and an 8-character claim code into NVS. First boot: the language, the WiFi
// (Settings' own page), then the code large with a QR of claim_url; the sync asks every 5 s; the first 200 erases the
// code and says hello. USB for Kho máy: ESPOS:INFO, ESPOS:CLAIM, ESPOS:FACTORY_RESET, ESPOS:ATTEST (devtools.cpp).
// eFuse: only the read side of a device attestation is built (HMAC with KEY5), off unless NVS "claim/attest" says
// so; nothing in this firmware can burn an eFuse (see claim.cpp).
#include <Arduino.h>

namespace claim {

#if ESPOS_NO_CLAIM  // an image without it (its size measured; a self-hosted build that never sells units)
inline bool pending() { return false; }
inline void set(const char*, const char*) {}
inline void claimed() {}
inline void tick() {}
inline bool usb(const char*) { return false; }
#else
bool pending();                               // a claim code waits for its owner
void set(const char* code, const char* url);  // ESPOS:PROVISION / ESPOS:CLAIM: kept in NVS ("claim")
void claimed();                               // the first 200 of a sync: the code goes, the welcome card
void tick();                                  // UI loop: the first-boot steps while pending
bool usb(const char* line);                   // an ESPOS: line for Kho máy; true when it was one of these
#ifdef SIM
void devStep(int step);                       // tests: 0 the language, 1 the WiFi, 2 the code
#endif
#endif  // ESPOS_NO_CLAIM

}  // namespace claim
