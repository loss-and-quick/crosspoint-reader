#include "XtcThumbScaler.h"

#include <cstring>

namespace xtc {

bool ThumbScaler::fitSize(const uint32_t srcW, const uint32_t srcH, const uint32_t maxW, const uint32_t maxH,
                          uint16_t& outW, uint16_t& outH) {
  if (srcW == 0 || srcH == 0 || maxW == 0 || maxH == 0) return false;
  if (srcW <= maxW && srcH <= maxH) return false;
  uint64_t w, h;
  if (static_cast<uint64_t>(srcW) * maxH >= static_cast<uint64_t>(srcH) * maxW) {  // width-limited
    w = maxW;
    h = static_cast<uint64_t>(srcH) * maxW / srcW;
  } else {
    h = maxH;
    w = static_cast<uint64_t>(srcW) * maxH / srcH;
  }
  outW = static_cast<uint16_t>(w < 1 ? 1 : w);
  outH = static_cast<uint16_t>(h < 1 ? 1 : h);
  return true;
}

ThumbScaler::ThumbScaler(const uint32_t srcW, const uint32_t srcH, const uint16_t dstW, const uint16_t dstH,
                         uint32_t* sums)
    : srcW(srcW), srcH(srcH), dstW(dstW), dstH(dstH), sums(sums) {
  memset(sums, 0, dstW * sizeof(uint32_t));
}

bool ThumbScaler::pushRow(const uint8_t* packedRow, uint8_t* outRow) {
  if (nextRow >= srcH) return false;
  const uint32_t y = nextRow++;
  // Every source pixel belongs to exactly one output cell: x -> x * dstW / srcW.
  for (uint32_t x = 0; x < srcW; x++) {
    const uint32_t level = (packedRow[x >> 2] >> (6 - ((x & 3) * 2))) & 0x03;
    sums[x * dstW / srcW] += level * 85;
  }
  rowsInGroup++;

  const uint32_t dstY = y * dstH / srcH;
  if (y + 1 < srcH && (y + 1) * dstH / srcH == dstY) return false;  // output row continues

  memset(outRow, 0xFF, outRowBytes(dstW));
  for (uint32_t dx = 0; dx < dstW; dx++) {
    // Source columns of cell dx: [ceil(dx*srcW/dstW), ceil((dx+1)*srcW/dstW)).
    const uint32_t cols = ((dx + 1) * srcW + dstW - 1) / dstW - (dx * srcW + dstW - 1) / dstW;
    const uint32_t count = cols * rowsInGroup;
    const uint32_t avg = count ? sums[dx] / count : 255;
    sums[dx] = 0;

    uint32_t hash = dx * 374761393u + dstY * 668265263u;
    hash = (hash ^ (hash >> 13)) * 1274126177u;
    const int threshold = 128 + ((static_cast<int>(hash >> 24) - 128) / 2);  // 64-192, as Xtc thumbnails
    if (avg < static_cast<uint32_t>(threshold)) outRow[dx >> 3] &= static_cast<uint8_t>(~(0x80 >> (dx & 7)));
  }
  rowsInGroup = 0;
  return true;
}

}  // namespace xtc
