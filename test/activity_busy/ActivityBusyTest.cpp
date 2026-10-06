#include <gtest/gtest.h>

#include <utility>

#include "ActivityBusy.h"

TEST(BusyCounter, StartsIdle) {
  BusyCounter counter;
  EXPECT_FALSE(counter.busy());
  EXPECT_EQ(counter.holders(), 0u);
}

TEST(BusyGuard, HoldsForItsScope) {
  BusyCounter counter;
  {
    BusyGuard guard(counter);
    EXPECT_TRUE(counter.busy());
    EXPECT_TRUE(guard.holding());
  }
  EXPECT_FALSE(counter.busy());
}

TEST(BusyGuard, EveryHolderCounts) {
  BusyCounter counter;
  BusyGuard first(counter);
  {
    BusyGuard second(counter);
    EXPECT_EQ(counter.holders(), 2u);
  }
  EXPECT_TRUE(counter.busy());
  first.release();
  EXPECT_FALSE(counter.busy());
}

TEST(BusyGuard, ReleaseIsIdempotent) {
  BusyCounter counter;
  BusyGuard guard(counter);
  guard.release();
  guard.release();
  EXPECT_FALSE(guard.holding());
  EXPECT_EQ(counter.holders(), 0u);
}

TEST(BusyGuard, DefaultHoldsNothing) {
  BusyCounter counter;
  BusyGuard guard;
  EXPECT_FALSE(guard.holding());
  guard.release();
  EXPECT_FALSE(counter.busy());
}

TEST(BusyGuard, MoveTransfersTheHold) {
  BusyCounter counter;
  BusyGuard source(counter);
  BusyGuard target(std::move(source));
  EXPECT_FALSE(source.holding());  // NOLINT(bugprone-use-after-move): a moved-from guard is empty by contract
  EXPECT_TRUE(target.holding());
  EXPECT_EQ(counter.holders(), 1u);
}

TEST(BusyGuard, AssignedFromHelperKeepsExactlyOneHold) {
  BusyCounter counter;
  BusyGuard member;
  member = BusyGuard(counter);
  EXPECT_EQ(counter.holders(), 1u);
  member = BusyGuard(counter);  // replacing a hold drops the old one
  EXPECT_EQ(counter.holders(), 1u);
  member = BusyGuard();
  EXPECT_FALSE(counter.busy());
}

TEST(BusyGuard, SelfMoveKeepsTheHold) {
  BusyCounter counter;
  BusyGuard guard(counter);
  BusyGuard& alias = guard;
  guard = std::move(alias);
  EXPECT_TRUE(guard.holding());
  EXPECT_EQ(counter.holders(), 1u);
}

TEST(IdlePolicy, BlockingLoopRestartsTheIdleClock) {
  EXPECT_FALSE(idlepolicy::blockedLoopCountsAsActivity(0));
  EXPECT_FALSE(idlepolicy::blockedLoopCountsAsActivity(idlepolicy::kBlockingLoopMs - 1));
  EXPECT_TRUE(idlepolicy::blockedLoopCountsAsActivity(idlepolicy::kBlockingLoopMs));
  EXPECT_TRUE(idlepolicy::blockedLoopCountsAsActivity(10UL * 60UL * 1000UL));
}

TEST(IdlePolicy, IdleTimeoutSleeps) {
  EXPECT_EQ(idlepolicy::decideTimeout(false, false), idlepolicy::TimeoutAction::Sleep);
}

TEST(IdlePolicy, WorkInFlightDefersSleep) {
  EXPECT_EQ(idlepolicy::decideTimeout(true, false), idlepolicy::TimeoutAction::Defer);
}

TEST(IdlePolicy, ExclusiveStorageDefersSleep) {
  EXPECT_EQ(idlepolicy::decideTimeout(false, true), idlepolicy::TimeoutAction::Defer);
  EXPECT_EQ(idlepolicy::decideTimeout(true, true), idlepolicy::TimeoutAction::Defer);
}
