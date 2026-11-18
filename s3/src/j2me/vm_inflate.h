#pragma once
// Raw deflate (RFC 1951: no zlib header, no checksum), written for the VM (no zlib): JAR entries, and PNG IDAT data
// for the MIDP layer (skip the 2-byte zlib header; the Adler-32 after the last block is ignored). Stored, fixed and
// dynamic blocks, any number of them. false: corrupt or cut-short data (never reads or writes out of bounds).
#include <cstddef>
#include <cstdint>
#include <vector>

#include "jvm.h"

namespace j2me {

// out is replaced by the data; it grows past sizeHint as needed (sizeHint 0: a guess from len).
bool inflateRaw(const uint8_t* src, size_t len, std::vector<uint8_t>& out, size_t sizeHint = 0);
// The same into PSRAM on the board (std::vector uses internal RAM there).
bool inflateRaw(const uint8_t* src, size_t len, PVec<uint8_t>& out, size_t sizeHint = 0);
// Exactly outLen bytes into out (a JAR entry, whose size is known); false if the data is not exactly that long.
bool inflateRaw(const uint8_t* src, size_t len, uint8_t* out, size_t outLen);

}  // namespace j2me
