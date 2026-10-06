#include <gtest/gtest.h>

#include "XtcReading.h"

namespace {

using namespace xtc::reading;

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
