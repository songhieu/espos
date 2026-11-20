#pragma once
// EspOS Bot's USB serial lines: the provisioning protocol the S3 speaks (the web installer and Kho máy: ESPOS:HELLO,
// INFO, PROVISION, CLAIM, FACTORY_RESET) and a small console for tests (SHOT, STATE, UI, FAKE, MIC, SET, SYNC, ...).
namespace bot {
namespace usb {
void loop();
}
}  // namespace bot
