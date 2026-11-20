#pragma once
// PawBoop's packs. The game's screen pieces and the bubble fonts are package vn.pawboop.pet (pawboop_ui.pbu,
// font/*.bin); the dog is its own package (pet/dogs.h: pet.pbs pictures, voice.pba lines, assist.pba the
// assistant's lines and number kit). In the internal app area files are memory-mapped, never copied (the
// simulator maps $SIM_DATA/apparea.bin the same way; PAWBOOP_PACKS=dir reads a folder). On the memory card the
// indexes, the pictures' records and the voice files' tables are kept in PSRAM; a picture's bytes and a line's
// packets are read when they are needed.
#include <Arduino.h>
#include <lvgl.h>

#include "formats.h"
#include "pkg/pkg.h"

namespace pet {
namespace pk {

// A PBA1 voice file (formats.h): its header, clip table and texts in place or in PSRAM; packets on demand.
class Pba {
 public:
  bool load(const char* pkgId, const char* name);
  void free();
  bool ok() const { return head_ != nullptr; }
  const pba_head_t* head() const { return (const pba_head_t*)head_; }
  int count() const { return ok() ? head()->nclips : 0; }
  const pba_clip_t* at(int i) const { return (const pba_clip_t*)(head_ + sizeof(pba_head_t)) + i; }
  const pba_clip_t* find(const char* name) const;  // by the clip's name ("hello_2"), nullptr when absent
  const char* text(const pba_clip_t* l) const { return (const char*)head_ + head()->text_off + l->text; }
  // The line's packets ({u16 len; opus}...): in place when mapped, else read into buf (l->bytes); nullptr on an
  // I/O error (card pulled out). Any task.
  const uint8_t* packets(const pba_clip_t* l, uint8_t* buf) const;
  bool mapped() const { return map_ != nullptr; }
  float seconds(const pba_clip_t* l) const { return (float)l->samples / head()->rate; }
  uint32_t bytes() const { return len_; }  // what it holds in PSRAM
  const pkg::File& file() const { return file_; }  // where it is (sfx.cpp streams a tune from it)

 private:
  const uint8_t* head_ = nullptr;  // header + clips + texts
  const uint8_t* map_ = nullptr;   // the whole file, memory-mapped
  uint8_t* own_ = nullptr;         // our PSRAM copy of the table
  uint32_t len_ = 0;
  pkg::File file_;
};

bool open();              // the packs of the pet and of the chosen dog (again when it changed; see reload())
bool ready();
// What is missing: "pet" (PawBoop itself), "dog" (no dog installed), a pack's name; "" when all are there.
const char* missing();
uint32_t mapMs();         // how long open() took
const char* dogKey();     // the dog whose packs are loaded ("corgi"), "" when none
uint32_t generation();    // bumps every time another dog's packs are loaded
// The dog changed (pet/dogs.h): its packs are loaded at the next open(). Call only when nothing shows the old
// dog (pet::switchDog closes the game, the book and the pop-up first). The old dog's tables are kept one more
// switch, so a pointer still held for a moment never dangles.
void reload();

const pbs2_head_t* dog();
const pbs2_clip_t* clip(const char* name);   // nullptr when the pack has no such clip
int clipCount();
const pbs2_clip_t* clipAt(int i);
const pbs2_frame_t* frame(const pbs2_clip_t* c, int i);  // i wraps for looping clips
// A picture's bytes: scan_len bytes of JPEG scan, then mask_len bytes of mask (mask(): the mask only). In place
// when the pack is memory-mapped; read into buf (dataMax() bytes) from the memory card. nullptr on a read error.
const uint8_t* data(const pbs2_frame_t* f, uint8_t* buf);
const uint8_t* mask(const pbs2_frame_t* f, uint8_t* buf);
uint32_t dataMax();  // 0 when memory-mapped (no buffer needed)
const uint8_t* jpegHeader();

const pbu_piece_t* piece(const char* name);  // nullptr when absent
// The big pieces read from the memory card (the screen, the splash: 300 KB each) go back to PSRAM's free; they are
// read again when drawn. The game calls it when it closes or makes room for the run.
void trim();
// RGB565 little endian, often at an odd address: read it byte by byte (rd16), never as uint16_t*.
const uint8_t* pixels(const pbu_piece_t* p);
const uint8_t* alpha(const pbu_piece_t* p);  // nullptr for fmt 0
inline uint16_t rd16(const uint8_t* b) { return (uint16_t)(b[0] | b[1] << 8); }
// An LVGL image of a piece (RGB565 or RGB565A8) that points into the pack (no copy). Kept for the run.
const lv_image_dsc_t* image(const pbu_piece_t* p);
// A w x h part of a piece (from its colours only) as an RGB565 image in PSRAM. Kept for the run.
const lv_image_dsc_t* crop(const char* piece, int x, int y, int w, int h);

// The dog's lines (voice.pba): 44 moments x 4 (Biscuit 5 for three of them), "on" = the picture clip or "@event".
const Pba& lines();
const pba_head_t* voice();
const pba_clip_t* line(const char* name);    // by the line's name ("hello")
int lineCount();
const pba_clip_t* lineAt(int i);
const char* text(const pba_clip_t* l);       // what the dog says (UTF-8)
// The same words for the OS's fonts, which have no "…" (U+2026): written as three dots. The words are the dog's.
String shown(const pba_clip_t* l);
float seconds(const pba_clip_t* l);

// The bubble fonts (Lexend, the page's font, with every letter of the dogs' lines): 0 = the bubble (bold 12),
// 1 = the dog's name in the header (bold 14), 2 = the splash's line (semibold 11). The OS's text font of that
// size when PawBoop's package has none (version 1).
const lv_font_t* font(int which);

}  // namespace pk
}  // namespace pet
