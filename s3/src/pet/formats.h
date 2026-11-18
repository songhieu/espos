#pragma once
// The three PawBoop packs, as the game's lead wrote them (pawboop_board_handoff/fw/pbs2.h, pbu.h, pba.h).
// Little endian, no padding; all three are read in place (memory mapped on the board).
#include <stdint.h>

#pragma pack(push, 1)
// biscuit_M.pbs: header, ONE baseline JPEG header (hdr_len bytes, the picture's height and width go to
// hdr[dims_off..+3], big endian, height first), nclips clip records, nframes + 1 offsets, the pictures.
// A picture = pbs2_frame_t + scan_len bytes of JPEG scan + mask_len bytes of run lengths (transparent first,
// alternating; a byte 255 = 255 more and the run goes on).
typedef struct { char magic[4]; uint16_t w, h, fps, nclips; uint32_t nframes; uint16_t hdr_len, dims_off; } pbs2_head_t;
typedef struct { char name[16]; uint32_t first; uint16_t count; uint8_t loop, view; } pbs2_clip_t;
typedef struct { uint16_t bx, by, bw, bh; int16_t sx, sy; uint8_t sr, flags; uint32_t scan_len, mask_len; } pbs2_frame_t;

// pawboop_ui.pbu: the screen's pieces. fmt 0: w*h RGB565; fmt 1: w*h RGB565 then w*h alpha (not premultiplied,
// which is exactly LVGL's RGB565A8 layout).
typedef struct { char magic[4]; uint16_t count; uint16_t zero; } pbu_head_t;
typedef struct { char name[24]; int16_t x, y; uint16_t w, h; uint8_t fmt, adv; uint16_t zero; uint32_t off; } pbu_piece_t;

// biscuit_voice_12k.pba: Opus 16 kHz mono, 60 ms packets; per line packets x { u16 len; u8 opus[len]; }.
typedef struct {
  char magic[4];
  uint16_t rate;
  uint8_t channels, packet_ms;
  uint16_t nclips, pre_skip;
  uint32_t text_off, data_off;
} pba_head_t;
typedef struct {
  char name[16];
  char on[16];  // the picture clip it belongs to, or "@event"
  uint32_t off, bytes, samples;
  uint16_t packets, text;
} pba_clip_t;
#pragma pack(pop)

static_assert(sizeof(pbs2_head_t) == 20 && sizeof(pbs2_clip_t) == 24 && sizeof(pbs2_frame_t) == 22, "pbs2");
static_assert(sizeof(pbu_head_t) == 8 && sizeof(pbu_piece_t) == 40, "pbu");
static_assert(sizeof(pba_head_t) == 20 && sizeof(pba_clip_t) == 48, "pba");
