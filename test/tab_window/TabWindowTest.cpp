#include <gtest/gtest.h>

#include "activities/TabWindow.h"

using namespace tabwindow;

TEST(TabWindow, AllTabsVisibleWhenTheyFit) {
  EXPECT_EQ(visibleTabCount(5, 100, 500), 5);
  EXPECT_EQ(visibleTabCount(5, 100, 499), 4);
  EXPECT_EQ(visibleTabCount(6, 300, 400), 2);  // never fewer than two
  EXPECT_EQ(visibleTabCount(1, 900, 400), 1);
  EXPECT_EQ(visibleTabCount(4, 0, 400), 4);
}

TEST(TabWindow, FollowKeepsActiveInViewWithMinimalMove) {
  EXPECT_EQ(scrollTabWindow(0, 3, 8, 2), 0);
  EXPECT_EQ(scrollTabWindow(0, 3, 8, 3), 1);
  EXPECT_EQ(scrollTabWindow(4, 3, 8, 1), 1);
  EXPECT_EQ(scrollTabWindow(1, 3, 8, 2), 1);
  EXPECT_EQ(scrollTabWindow(0, 3, 8, 7), 5);
  EXPECT_EQ(scrollTabWindow(0, 8, 8, 7), 0);
}

TEST(TabWindow, SwipeShiftDirectionAndDistance) {
  EXPECT_EQ(swipeShift(-10, 100, 3), 1);   // short swipe left: one tab later
  EXPECT_EQ(swipeShift(10, 100, 3), -1);   // short swipe right: one tab earlier
  EXPECT_EQ(swipeShift(-240, 100, 3), 2);  // distance in tab widths
  EXPECT_EQ(swipeShift(-900, 100, 3), 3);  // at most one page
  EXPECT_EQ(swipeShift(900, 100, 4), -4);
  EXPECT_EQ(swipeShift(0, 100, 3), 0);
  EXPECT_EQ(swipeShift(-50, 0, 3), 1);
}

TEST(TabWindow, SwipeScrollClamps) {
  EXPECT_EQ(swipeScroll(0, -300, 100, 3, 8), 3);
  EXPECT_EQ(swipeScroll(3, -300, 100, 3, 8), 5);  // last window
  EXPECT_EQ(swipeScroll(5, -300, 100, 3, 8), 5);
  EXPECT_EQ(swipeScroll(1, 300, 100, 3, 8), 0);
  EXPECT_EQ(swipeScroll(0, 300, 100, 3, 8), 0);
  EXPECT_EQ(swipeScroll(0, -300, 100, 8, 8), 0);  // nothing to scroll
}
