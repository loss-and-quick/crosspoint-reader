#include <gtest/gtest.h>

#include <vector>

#include "XtcThumbScaler.h"

namespace {

using xtc::ThumbScaler;

// Packs one source row (levels 0-3) the way Bitmap::readNextRow does.
std::vector<uint8_t> pack(const std::vector<uint8_t>& levels) {
  std::vector<uint8_t> out(ThumbScaler::srcRowBytes(levels.size()), 0);
  for (size_t x = 0; x < levels.size(); x++) out[x >> 2] |= levels[x] << (6 - (x & 3) * 2);
  return out;
}

bool whiteAt(const std::vector<uint8_t>& row, const uint32_t x) { return row[x >> 3] & (0x80 >> (x & 7)); }

// Scales a w x h image whose pixel level comes from `level(x, y)`; returns the output rows.
template <typename F>
std::vector<std::vector<uint8_t>> scale(const uint32_t w, const uint32_t h, const uint16_t dw, const uint16_t dh,
                                        F level) {
  std::vector<uint32_t> sums(dw);
  ThumbScaler scaler(w, h, dw, dh, sums.data());
  std::vector<std::vector<uint8_t>> rows;
  std::vector<uint8_t> out(ThumbScaler::outRowBytes(dw));
  for (uint32_t y = 0; y < h; y++) {
    std::vector<uint8_t> src(w);
    for (uint32_t x = 0; x < w; x++) src[x] = level(x, y);
    if (scaler.pushRow(pack(src).data(), out.data())) rows.push_back(out);
  }
  return rows;
}

TEST(XtcThumbFit, GatewayCoverFitsEachThemeSlot) {
  uint16_t w = 0, h = 0;
  // 240x400 cover into the Lyra (226), RoundedRaff (300) and Base (400) slots.
  ASSERT_TRUE(ThumbScaler::fitSize(240, 400, 226 * 6 / 10, 226, w, h));
  EXPECT_EQ(w, 135);
  EXPECT_EQ(h, 225);
  ASSERT_TRUE(ThumbScaler::fitSize(240, 400, 300 * 6 / 10, 300, w, h));
  EXPECT_EQ(w, 180);
  EXPECT_EQ(h, 300);
  EXPECT_FALSE(ThumbScaler::fitSize(240, 400, 400 * 6 / 10, 400, w, h));  // already 1:1
}

TEST(XtcThumbFit, KeepsAspectAndNeverUpscales) {
  uint16_t w = 0, h = 0;
  ASSERT_TRUE(ThumbScaler::fitSize(800, 400, 120, 200, w, h));  // wide image: width-limited
  EXPECT_EQ(w, 120);
  EXPECT_EQ(h, 60);
  ASSERT_TRUE(ThumbScaler::fitSize(100, 1000, 120, 200, w, h));  // tall image: height-limited
  EXPECT_EQ(w, 20);
  EXPECT_EQ(h, 200);
  ASSERT_TRUE(ThumbScaler::fitSize(1000, 10, 100, 100, w, h));  // extreme aspect never reaches 0
  EXPECT_EQ(w, 100);
  EXPECT_EQ(h, 1);
  EXPECT_FALSE(ThumbScaler::fitSize(50, 80, 120, 200, w, h));
}

TEST(XtcThumbScaler, EmitsExactlyDstHeightRows) {
  for (const auto& dims :
       {std::vector<uint32_t>{240, 400, 135, 225}, {240, 400, 180, 300}, {7, 13, 3, 5}, {5, 5, 5, 5}}) {
    const auto rows = scale(dims[0], dims[1], dims[2], dims[3], [](uint32_t, uint32_t) { return 3; });
    EXPECT_EQ(rows.size(), dims[3]) << dims[0] << "x" << dims[1];
    for (const auto& row : rows) EXPECT_EQ(row.size(), ThumbScaler::outRowBytes(dims[2]));
  }
}

TEST(XtcThumbScaler, SolidColoursStayBlackOrWhiteIncludingPadding) {
  const auto white = scale(240, 400, 135, 225, [](uint32_t, uint32_t) { return 3; });
  for (const auto& row : white) {
    for (uint32_t x = 0; x < 135; x++) ASSERT_TRUE(whiteAt(row, x));
    for (uint32_t x = 135; x < row.size() * 8; x++) ASSERT_TRUE(whiteAt(row, x));  // padding
  }
  const auto black = scale(240, 400, 135, 225, [](uint32_t, uint32_t) { return 0; });
  for (const auto& row : black) {
    for (uint32_t x = 0; x < 135; x++) ASSERT_FALSE(whiteAt(row, x));
  }
}

TEST(XtcThumbScaler, KeepsWholeImageNotACrop) {
  // Left half black, right half white: the thumbnail must show both halves, which a
  // centered 1:1 crop of a larger image would not.
  const auto rows = scale(240, 400, 135, 225, [](uint32_t x, uint32_t) { return x < 120 ? 0 : 3; });
  ASSERT_EQ(rows.size(), 225u);
  for (const auto& row : rows) {
    EXPECT_FALSE(whiteAt(row, 10));
    EXPECT_FALSE(whiteAt(row, 60));
    EXPECT_TRUE(whiteAt(row, 75));
    EXPECT_TRUE(whiteAt(row, 125));
  }
  // Top black, bottom white.
  const auto vertical = scale(240, 400, 135, 225, [](uint32_t, uint32_t y) { return y < 200 ? 0 : 3; });
  EXPECT_FALSE(whiteAt(vertical.front(), 50));
  EXPECT_TRUE(whiteAt(vertical.back(), 50));
}

TEST(XtcThumbScaler, MidGraysDitherToMatchingDensity) {
  const auto rows = scale(240, 400, 135, 225, [](uint32_t, uint32_t) { return 1; });  // level 1 = 85/255
  size_t white = 0;
  for (const auto& row : rows) {
    for (uint32_t x = 0; x < 135; x++) white += whiteAt(row, x);
  }
  const double fraction = static_cast<double>(white) / (135.0 * 225.0);
  EXPECT_GT(fraction, 0.05);  // noise thresholds span 64-192, so 85 is dark but not uniformly black
  EXPECT_LT(fraction, 0.30);
  const auto light = scale(240, 400, 135, 225, [](uint32_t, uint32_t) { return 2; });
  white = 0;
  for (const auto& row : light) {
    for (uint32_t x = 0; x < 135; x++) white += whiteAt(row, x);
  }
  EXPECT_GT(static_cast<double>(white) / (135.0 * 225.0), 0.70);
}

TEST(XtcThumbScaler, ExtraRowsAreIgnored) {
  std::vector<uint32_t> sums(2);
  ThumbScaler scaler(4, 4, 2, 2, sums.data());
  const auto row = pack({3, 3, 3, 3});
  std::vector<uint8_t> out(ThumbScaler::outRowBytes(2));
  int emitted = 0;
  for (int i = 0; i < 6; i++) emitted += scaler.pushRow(row.data(), out.data());
  EXPECT_EQ(emitted, 2);
}

}  // namespace
