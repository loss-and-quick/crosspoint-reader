#include <gtest/gtest.h>

#include "XtcReading.h"

namespace {

using namespace xtc::reading;

TEST(XtcReadingLevels, MirrorMapsEachLevelToItsOppositeInDarkness) {
  // 0 white, 1 dark grey, 2 light grey, 3 black.
  EXPECT_EQ(mirrorLevel(0), 3);
  EXPECT_EQ(mirrorLevel(1), 2);
  EXPECT_EQ(mirrorLevel(2), 1);
  EXPECT_EQ(mirrorLevel(3), 0);
}

TEST(XtcReadingLevels, MirrorIsAnInvolutionAndKeepsAllFourLevelsDistinct) {
  bool seen[4] = {};
  for (uint8_t v = 0; v < 4; v++) {
    EXPECT_EQ(mirrorLevel(mirrorLevel(v)), v);  // no drift from a double inversion
    ASSERT_LT(mirrorLevel(v), 4);
    seen[mirrorLevel(v)] = true;
  }
  for (const bool s : seen) EXPECT_TRUE(s);  // a permutation: no posterization
}

TEST(XtcReadingLevels, MirrorReversesDarknessOrder) {
  // Darkness rank of an XTH level: white < light grey < dark grey < black.
  constexpr int rank[4] = {0, 2, 1, 3};
  for (uint8_t a = 0; a < 4; a++) {
    for (uint8_t b = 0; b < 4; b++) {
      EXPECT_EQ(rank[a]<rank[b], rank[mirrorLevel(a)]> rank[mirrorLevel(b)]);
    }
  }
}

TEST(XtcReadingPolarity, LightModeNeverTouchesAnything) {
  for (const uint8_t depth : {1, 2}) {
    for (const bool invertComics : {false, true}) {
      const auto p = planPolarity(false, invertComics, depth, true);
      EXPECT_FALSE(p.displayInverted || p.mirrorLevels || p.counterInvertPage || p.invertChrome);
    }
  }
}

TEST(XtcReadingPolarity, NightOneBitUsesPanelPolarityAndKeepsOriginalByCounterInverting) {
  const auto keep = planPolarity(true, false, 1, true);
  EXPECT_TRUE(keep.displayInverted);
  EXPECT_TRUE(keep.counterInvertPage);
  EXPECT_FALSE(keep.mirrorLevels);
  EXPECT_FALSE(keep.invertChrome);

  const auto invert = planPolarity(true, true, 1, true);
  EXPECT_TRUE(invert.displayInverted);
  EXPECT_FALSE(invert.counterInvertPage);
}

TEST(XtcReadingPolarity, NightTwoBitStaysOutOfPanelInversionSoGraysWork) {
  const auto keep = planPolarity(true, false, 2, true);
  EXPECT_FALSE(keep.displayInverted);
  EXPECT_FALSE(keep.mirrorLevels);
  EXPECT_FALSE(keep.counterInvertPage);
  EXPECT_TRUE(keep.invertChrome);

  const auto invert = planPolarity(true, true, 2, true);
  EXPECT_FALSE(invert.displayInverted);
  EXPECT_TRUE(invert.mirrorLevels);  // the negative comes from the levels, not the panel
  EXPECT_TRUE(invert.invertChrome);
}

TEST(XtcReadingPolarity, ScreensWithoutAPageFollowNightModeOnThePanel) {
  const auto p = planPolarity(true, false, 2, false);  // end-of-book menu, load error
  EXPECT_TRUE(p.displayInverted);
  EXPECT_FALSE(p.counterInvertPage);
  EXPECT_FALSE(p.invertChrome);
}

TEST(XtcReadingPlacement, HiddenBarLeavesThePageAlone) {
  const auto p = planPlacement(800, 800, 0, 0, 23, BarPosition::Hide);
  EXPECT_EQ(p.stripH, 0);
  EXPECT_FALSE(p.overlay);
  EXPECT_EQ(p.pageY, 0);
}

TEST(XtcReadingPlacement, FullScreenPageIsOverlaidAtTheBottom) {
  const auto p = planPlacement(800, 800, 0, 0, 23, BarPosition::Bottom);
  EXPECT_TRUE(p.overlay);
  EXPECT_EQ(p.stripY, 777);
  EXPECT_EQ(p.pageY, 0);
}

TEST(XtcReadingPlacement, ShortPageSitsAboveTheStripWithoutOverlap) {
  const auto p = planPlacement(768, 800, 0, 0, 23, BarPosition::Bottom);
  EXPECT_FALSE(p.overlay);
  EXPECT_EQ(p.pageY, 0);
  EXPECT_GE(p.stripY, 768);
}

TEST(XtcReadingPlacement, PageExactlyFillingTheRoomIsNotOverlaid) {
  EXPECT_FALSE(planPlacement(777, 800, 0, 0, 23, BarPosition::Bottom).overlay);
  EXPECT_TRUE(planPlacement(778, 800, 0, 0, 23, BarPosition::Bottom).overlay);
}

TEST(XtcReadingPlacement, BezelInsetMovesTheStripUp) {
  const auto p = planPlacement(700, 800, 0, 10, 23, BarPosition::Bottom);
  EXPECT_EQ(p.stripY, 767);
  EXPECT_FALSE(p.overlay);
}

TEST(XtcReadingPlacement, TopBarPushesAFittingPageDown) {
  const auto p = planPlacement(768, 800, 0, 0, 23, BarPosition::Top);
  EXPECT_EQ(p.stripY, 0);
  EXPECT_FALSE(p.overlay);
  EXPECT_EQ(p.pageY, 23);
}

TEST(XtcReadingPlacement, TopBarOverlaysAFullScreenPage) {
  const auto p = planPlacement(800, 800, 0, 0, 23, BarPosition::Top);
  EXPECT_TRUE(p.overlay);
  EXPECT_EQ(p.pageY, 0);
}

TEST(XtcReadingPercent, PageForPercentCoversTheWholeBook) {
  EXPECT_EQ(pageForPercent(100, 0), 0u);
  EXPECT_EQ(pageForPercent(100, 1), 0u);
  EXPECT_EQ(pageForPercent(100, 50), 49u);
  EXPECT_EQ(pageForPercent(100, 100), 99u);
  EXPECT_EQ(pageForPercent(7, 100), 6u);
  EXPECT_EQ(pageForPercent(7, 50), 3u);
  EXPECT_EQ(pageForPercent(0, 50), 0u);
  EXPECT_EQ(pageForPercent(10, 250), 9u);  // clamped
  EXPECT_EQ(pageForPercent(10, -5), 0u);
}

TEST(XtcReadingPercent, PageForPercentLandsOnAPageThatShowsThatPercent) {
  for (const uint32_t count : {1u, 3u, 7u, 40u, 199u}) {
    for (int percent = 1; percent <= 100; percent++) {
      const uint32_t page = pageForPercent(count, percent);
      ASSERT_LT(page, count);
      EXPECT_GE(percentForPage(count, page), percent) << count << " " << percent;
    }
  }
}

TEST(XtcReadingPercent, PercentForPage) {
  EXPECT_EQ(percentForPage(0, 0), 0);
  EXPECT_EQ(percentForPage(4, 0), 25);
  EXPECT_EQ(percentForPage(4, 3), 100);
  EXPECT_EQ(percentForPage(4, 9), 100);  // past the end (end-of-book page)
}

}  // namespace
