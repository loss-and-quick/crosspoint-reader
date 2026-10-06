#include <gtest/gtest.h>

#include <vector>

#include "WifiConnectPolicy.h"

using namespace wificonnect;

namespace {
using Step = SavedSequence::Step;
using Order = std::vector<size_t>;

constexpr int32_t kNone = INT32_MIN;  // not seen by the scan

void feed(SavedSequence& seq, const std::vector<int32_t>& scan) {
  for (size_t i = 0; i < scan.size(); i++) {
    if (scan[i] != kNone) seq.see(i, scan[i]);
  }
}

// Drains the sequence the way the connector does: feed the scan when asked, collect the joins.
Order drain(SavedSequence& seq, const std::vector<int32_t>& scan) {
  Order joined;
  for (int guard = 0; guard < 64; guard++) {
    const auto n = seq.next();
    if (n.step == Step::Done) break;
    if (n.step == Step::Scan) {
      feed(seq, scan);
    } else {
      joined.push_back(n.index);
    }
  }
  return joined;
}
}  // namespace

TEST(WifiConnectSequence, LastConnectedThenScannedNetworksStrongestFirst) {
  SavedSequence seq;
  seq.begin(3, 1);  // home, work, cafe; work was connected last
  auto n = seq.next();
  EXPECT_EQ(n.step, Step::Join);
  EXPECT_EQ(n.index, 1u);
  EXPECT_EQ(seq.next().step, Step::Scan);
  feed(seq, {-50, -30, -80});  // the last connected one is never offered again
  n = seq.next();
  EXPECT_EQ(n.step, Step::Join);
  EXPECT_EQ(n.index, 0u);
  n = seq.next();
  EXPECT_EQ(n.step, Step::Join);
  EXPECT_EQ(n.index, 2u);
  EXPECT_EQ(seq.next().step, Step::Done);
}

TEST(WifiConnectSequence, NoLastConnectedStartsWithTheScan) {
  SavedSequence seq;
  seq.begin(1, 1);
  EXPECT_EQ(drain(seq, {-60}), (Order{0}));
}

TEST(WifiConnectSequence, OnlyNetworksInRangeAreTriedStrongestFirst) {
  SavedSequence seq;
  seq.begin(4, 4);
  EXPECT_EQ(drain(seq, {-90, kNone, -50, -70}), (Order{2, 3, 0}));
}

TEST(WifiConnectSequence, SeveralAccessPointsCountAtTheirStrongestSignal) {
  SavedSequence seq;
  seq.begin(2, 2);
  EXPECT_EQ(seq.next().step, Step::Scan);
  seq.see(0, -80);
  seq.see(1, -60);
  seq.see(0, -40);
  seq.see(0, -90);
  EXPECT_EQ(seq.next().index, 0u);
  EXPECT_EQ(seq.next().index, 1u);
  EXPECT_EQ(seq.next().step, Step::Done);
}

TEST(WifiConnectSequence, EqualSignalKeepsSavedOrder) {
  SavedSequence seq;
  seq.begin(3, 3);
  EXPECT_EQ(drain(seq, {-60, -60, -60}), (Order{0, 1, 2}));
}

TEST(WifiConnectSequence, NothingInRangeEndsAfterTheScan) {
  SavedSequence seq;
  seq.begin(2, 0);
  EXPECT_EQ(seq.next().index, 0u);
  EXPECT_EQ(seq.next().step, Step::Scan);
  EXPECT_EQ(seq.next().step, Step::Done);
  EXPECT_EQ(seq.next().step, Step::Done);
}

TEST(WifiConnectSequence, ScanIsRequestedOnce) {
  SavedSequence seq;
  seq.begin(1, 1);
  EXPECT_EQ(seq.next().step, Step::Scan);
  EXPECT_EQ(seq.next().step, Step::Done);
}

TEST(WifiConnectSequence, LastConnectedSeenByTheScanIsNotOfferedTwice) {
  SavedSequence seq;
  seq.begin(2, 1);
  EXPECT_EQ(drain(seq, {kNone, -40}), (Order{1}));
}

TEST(WifiConnectSequence, NetworksBeyondTheLimitAreIgnored) {
  SavedSequence seq;
  seq.begin(kMaxSaved + 4, kMaxSaved + 4);
  EXPECT_EQ(seq.next().step, Step::Scan);
  for (size_t i = 0; i < kMaxSaved + 4; i++) seq.see(i, -50);
  Order joined;
  for (auto n = seq.next(); n.step == Step::Join; n = seq.next()) joined.push_back(n.index);
  EXPECT_EQ(joined.size(), kMaxSaved);
}

TEST(WifiConnectSequence, BeginRestartsThePass) {
  SavedSequence seq;
  seq.begin(2, 0);
  EXPECT_EQ(seq.next().index, 0u);
  EXPECT_EQ(seq.next().step, Step::Scan);
  seq.see(1, -50);
  seq.begin(2, 0);
  EXPECT_EQ(seq.next().index, 0u);
  EXPECT_EQ(seq.next().step, Step::Scan);
  EXPECT_EQ(seq.next().step, Step::Done);  // the earlier scan is forgotten
}
