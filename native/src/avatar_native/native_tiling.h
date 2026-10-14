#pragma once
#include <cassert>
#include <cstdint>
namespace xe::gpu::texture_address {
constexpr unsigned kMacroTileWidthLog2=5,kMacroTileHeight2DLog2=5;
template <typename Address>
Address TiledCombine(const Address outer_inner_bytes, const uint32_t bank,
                     const uint32_t pipe, const uint32_t y_lsb) {
  assert(bank <= 0b1);
  assert(pipe <= 0b11);
  assert(y_lsb <= 0b1);
  return int32_t((y_lsb << 4) | (pipe << 6) | (bank << 11)) |
         (outer_inner_bytes & 0b1111) |
         (((outer_inner_bytes >> 4) & 0b1) << 5) |
         (((outer_inner_bytes >> 5) & 0b111) << 8) |
         (outer_inner_bytes >> 8 << 12);
}

// The absolute of the return value is below 2^31 (for 16384x8192 with 16 bytes
// per block, though it's not even clear if the hardware allows pitches greater
// than 8192, the maximum texture size, but the pitch field in a texture fetch
// constant is 14 bits wide).
inline int32_t Tiled2D(const int32_t x, const int32_t y,
                       const uint32_t pitch_aligned,
                       const unsigned int bytes_per_block_log2) {
  // Expecting that all the needed rounding (not only to 32x32, but also to a
  // power of two for mips) is done before the call so this function works with
  // the actual storage dimensions.
  assert(!(pitch_aligned & 31));
  const int32_t outer_blocks =
      ((y >> kMacroTileHeight2DLog2) *
           int32_t(pitch_aligned >> kMacroTileWidthLog2) +
       (x >> kMacroTileWidthLog2))
      << 6;
  const int32_t inner_blocks = (((y >> 1) & 0b111) << 3) | (x & 0b111);
  const int32_t outer_inner_bytes = (outer_blocks | inner_blocks)
                                    << bytes_per_block_log2;
  const uint32_t bank = (y >> 4) & 0b1;
  const uint32_t pipe = ((x >> 3) & 0b11) ^ (((y >> 3) & 0b1) << 1);
  return TiledCombine(outer_inner_bytes, bank, pipe, y & 1);
}


}
