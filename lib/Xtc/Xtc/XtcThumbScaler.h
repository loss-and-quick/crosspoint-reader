/**
 * XtcThumbScaler.h
 *
 * Pure (HAL-free) streaming downscaler that turns the rows of a source image into
 * the rows of a 1-bit thumbnail, the same way Xtc::generateThumbBmp does: area
 * averaging followed by hash-noise thresholding. Source rows are pushed one at a
 * time, so no more than one source row, one output row and one accumulator per
 * output column is ever held.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace xtc {

class ThumbScaler {
 public:
  // Largest size with the aspect of src that fits in maxW x maxH. Returns false
  // (leaving out* untouched) when src already fits: thumbnails are never upscaled.
  static bool fitSize(uint32_t srcW, uint32_t srcH, uint32_t maxW, uint32_t maxH, uint16_t& outW, uint16_t& outH);
  // BMP row size (4-byte aligned) of a 1-bit image dstW pixels wide.
  static constexpr size_t outRowBytes(const uint32_t dstW) { return (dstW + 31) / 32 * 4; }
  // Source row size for pushRow: 4 pixels per byte.
  static constexpr size_t srcRowBytes(const uint32_t srcW) { return (srcW + 3) / 4; }

  // `sums` is caller-owned scratch of dstW entries. Requires 0 < dstW <= srcW and 0 < dstH <= srcH.
  ThumbScaler(uint32_t srcW, uint32_t srcH, uint16_t dstW, uint16_t dstH, uint32_t* sums);

  // Consumes one source row of srcRowBytes(srcW) bytes: 2 bits per pixel, MSB
  // first, 0 = black ... 3 = white (the layout of Bitmap::readNextRow). Returns true
  // when this row completed an output row, written to `outRow` (outRowBytes(dstW)
  // bytes, bit 1 = white, padding set to white).
  bool pushRow(const uint8_t* packedRow, uint8_t* outRow);

 private:
  uint32_t srcW, srcH;
  uint16_t dstW, dstH;
  uint32_t* sums;
  uint32_t nextRow = 0;
  uint32_t rowsInGroup = 0;
};

}  // namespace xtc
