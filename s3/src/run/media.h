#pragma once
// The run's pictures: the dog and the things (PBS2, mapped, or their tables in PSRAM and frames read from the card), the HUD and cards (PBU1, in
// PSRAM), the roads (PBV1, read picture by picture from the flash or the card). Pictures are decoded by a
// background task on core 0 (inline in the simulator) into a sprite cache and a ring of road slots.
#include <Arduino.h>

#include <vector>

#include "pet/formats.h"
#include "pkg/pkg.h"

namespace run {

uint32_t nowUs();  // the real clock (the simulator's millis() is simulated time)

namespace media {

// ------------------------------------------------------------------ packs
// A PBS2 file. In the internal flash: memory-mapped, read in place. On a memory card: only its table (header,
// clips, frame offsets) and every frame's record are in PSRAM; a frame's picture bytes are read from the card when
// it is decoded (holding the whole packs, 1.4 MB, starved the game beside PawBoop).
struct Pack {
  uint8_t* data = nullptr;       // the whole file (mapped), or its table only (card, our PSRAM copy)
  bool owned = false;            // data is our PSRAM copy
  pbs2_frame_t* recs = nullptr;  // card: frame k's record
  pkg::File file;                // card: where the frames' bytes are read
  uint32_t size = 0;
  uint32_t maxPx = 0, maxW = 0;  // the largest box (for the decoder's scratch)
  uint32_t maxBytes = 0;         // the largest frame's scan + mask (the decoders' buffer)
  const pbs2_head_t* head() const { return (const pbs2_head_t*)data; }
  const uint8_t* jpegHeader() const { return data + sizeof(pbs2_head_t); }
  int clipCount() const { return head()->nclips; }
  const pbs2_clip_t* clipAt(int i) const;
  const pbs2_clip_t* clip(const char* name) const;  // nullptr when absent
  const pbs2_frame_t* frame(const pbs2_clip_t* c, int i) const;
  // A frame's scan then its mask: in place, or read from the card into buf (maxBytes); nullptr on a read error.
  const uint8_t* bytes(const pbs2_frame_t* f, uint8_t* buf) const;
  bool load(const char* pkgId, const char* name, String& err);
  void free();
};

struct Ui {  // a PBU1 file: its table memory-mapped or in PSRAM; every piece copied 2-byte aligned into PSRAM
  // (the start cards "menu_<map>", 110 KB each, one at a time when shown: eight maps' cards took 0.9 MB)
  static constexpr uint32_t LAZY = 0xFFFFFFFF;
  uint8_t* data = nullptr;
  bool owned = false;
  uint8_t* px = nullptr;      // the pieces' pixels then alpha, each piece at an even offset
  uint32_t* at = nullptr;     // piece i's offset in px (LAZY: read when it is drawn)
  uint32_t size = 0;
  pkg::File file;
  mutable uint8_t* one = nullptr;  // the lazy piece read last
  mutable int oneIdx = -1;
  const pbu_piece_t* piece(const char* name) const;
  int index(const pbu_piece_t* p) const { return p - (const pbu_piece_t*)(data + sizeof(pbu_head_t)); }
  const uint8_t* lazy(int i) const;
  const uint8_t* pixels(const pbu_piece_t* p) const {  // RGB565 LE, 2-byte aligned (nullptr: a lazy piece unread)
    const int i = index(p);
    return at[i] != LAZY ? px + at[i] : lazy(i);
  }
  const uint8_t* alpha(const pbu_piece_t* p) const {
    const uint8_t* x = pixels(p);
    return p->fmt && x ? x + 2u * p->w * p->h : nullptr;
  }
  bool load(const char* pkgId, const char* name, String& err);
  void free();
};

#pragma pack(push, 1)
struct pbv_head_t {
  char magic[4];
  uint16_t w, h, fps, nclips;
  uint32_t nframes;
};
#pragma pack(pop)

struct Road {  // a PBV1 file: read one picture at a time
  pkg::File file;
  int w = 0, h = 0, n = 0;
  uint32_t* off = nullptr;  // n + 1 offsets (PSRAM)
  bool open(const char* pkgId, const char* name);
  void free();
};

// ------------------------------------------------------------------ decoded pictures
struct Sprite {  // colours and alpha (0..255) of a picture's box
  const pbs2_frame_t* f = nullptr;
  int w = 0, h = 0;
  const uint16_t* rgb = nullptr;
  const uint8_t* a = nullptr;
};

constexpr int ROAD_W = 232, ROAD_H = 240;

struct Stats {
  uint32_t sprAhead = 0, sprHelper = 0, sprInline = 0, sprUs = 0, sprMaxUs = 0, sprWaitUs = 0, sprFailed = 0;
  uint32_t sprHeld = 0, cacheMaxKB = 0, pinnedKB = 0;  // ...; the most the cache held  // pictures not decoded ahead for want of room (they waited a picture)
  uint32_t roadDec = 0, roadUs = 0, roadMaxUs = 0, roadInline = 0, roadFailed = 0;
  uint32_t readN = 0, readUs = 0, readMaxUs = 0;  // card reads of road pictures
  uint32_t tjpgd = 0, fastFailed = 0;             // pictures TJpgDec decoded; refused by Espressif's decoder
  uint32_t cacheKB = 0, evicted = 0;
};
Stats& stats();
Stats snapshot();  // a copy taken under the decoders' lock (for reading while they run)

// Starts the decoders (a task on core 0 and a helper on core 1 on the board); scratch for the packs' largest boxes.
bool begin(const Pack* const* packs, int npacks, bool cardRoads = false);  // cardRoads: a road file is on the card
void helper(bool on);  // a second decoder on core 1 at the idle priority (default off: it slowed the roads)
void end();  // stops the task, frees every decoded picture and buffer

// The picture of a frame, decoded now if the background task does not have it ready. Valid until the next
// frame() call (the cache keeps what this frame uses).
// decode = false: only when it is ready now (else it is wanted and nullptr comes back: draw an older picture).
const Sprite* sprite(const Pack& p, const pbs2_frame_t* f, bool decode = true);
// Decode ahead (background), from the next kick(). urgent: before the other wishes (a thing that comes near: big,
// and drawn in a picture or two).
void want(const Pack& p, const pbs2_frame_t* f, bool urgent = false);
void keep(const Pack& p, const pbs2_frame_t* f);  // decode ahead and never drop it (her run loop)
void kick();                                        // the decoders start on what was wanted
void frame();                                       // a new picture starts: older entries may be evicted

// Roads. A key = road file index (in the table given to roads()) * 4096 + picture, or a fade: two pictures
// mixed (b's weight alpha 0..255, kept in 64 steps) when a link has no file. Up to 16 road files (8 maps, 8 gates).
constexpr int FADE = 1 << 30;
inline int fadeKey(int roadA, int picA, int roadB, int picB, int alpha) {
  return FADE | (roadA & 15) << 26 | (picA & 255) << 18 | (roadB & 15) << 14 | (picB & 255) << 6 | (alpha >> 2 & 63);
}
void roads(Road* table, int n);
// The pictures wanted next, most urgent first: decoded ahead in that order; others are dropped.
void plan(const int* keys, int n);
// The pixels of a picture (232x240 RGB565): ready now, or (late) the picture shown last, or decoded now when
// nothing was shown yet. *late = true when an older picture stands in.
const uint16_t* road(int key, bool* late);
bool roadReady(int key);  // decoded and waiting

// Runs fn on the decoder task (inline in the simulator), e.g. loading the packs; posted() is true once it
// returned.
void post(void (*fn)(void*), void* arg);
bool posted();

// Bench: decode one picture now on this thread into scratch memory (the cache is not touched). us, 0 = failed.
uint32_t timeSprite(const Pack& p, const pbs2_frame_t* f);
uint32_t timeRoad(int key);
void compare(int key, const Pack& p, const pbs2_frame_t* f);  // Espressif's decoder vs TJpgDec (Serial)
// Espressif's decoder takes internal RAM for its tables only while this much stays free (else PSRAM).
extern uint32_t jpegReserve;
// Espressif's decoder (the board), or the live simulator at the board's speed: roads20 keeps up at 20 pictures a
// second. false: TJpgDec in the script simulator.
bool fast();

}  // namespace media
}  // namespace run
