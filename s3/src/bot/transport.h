#pragma once
// EspOS Bot's HTTPS, as link.cpp's task uses it: on the board src/bot/transport_esp.cpp (HTTPClient over one kept TLS
// connection, the IDF's certificate bundle, the OTA writer); in the simulator sim/bot/transport_curl.cpp.
#include <stdint.h>

#include <string>

namespace bot {
namespace transport {

void begin();
// One request on the kept connection. hw: the hardware headers (X-Espos-Hwid / X-Espos-Mac4). Returns the HTTP
// status, or < 0: -1 no connection, -2 the body did not go, -3 no answer.
// token: the device token to send (nullptr: the stored one; "": none, as /v1/pair/start).
int request(const char* method, const std::string& url, const uint8_t* body, uint32_t len, const char* ctype,
            std::string& out, uint32_t timeoutMs, bool hw, const char* token = nullptr);
// The dog's question (a WAV). A voice answer (audio/pcm): onHead(the X-Assist JSON) first, then the PCM goes into
// the speaker (spk::pushPcm) until it ends or *cancel; voice is then true and body empty. Otherwise body = the JSON.
int ask(const std::string& url, const uint8_t* wav, uint32_t len, bool& voice, std::string& body,
        void (*onHead)(const std::string& json), volatile bool* cancel);
bool warm(const std::string& url);  // a fresh connection to url's host
void drop();                        // the kept connection closed
void dropIfIdle(uint32_t ms);       // ... when it carried nothing for ms (the server may have closed it)
// A firmware image: downloaded, checked against sha256 (hex), written to the other app slot and made the boot one.
// 200 = done (restart to run it); *pct follows the download.
int ota(const std::string& url, uint32_t size, const std::string& sha, volatile int* pct);

}  // namespace transport
}  // namespace bot
