#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "XtcSeriesFormat.h"

namespace {

using namespace xtc::series;
using namespace std::string_view_literals;

struct Line {
  uint32_t offset;
  std::string text;
  bool truncated;
};

void collect(void* ctx, const uint32_t offset, const std::string_view line, const bool truncated) {
  static_cast<std::vector<Line>*>(ctx)->push_back({offset, std::string(line), truncated});
}

std::vector<Line> split(const std::string& input, const size_t chunk) {
  std::vector<Line> lines;
  std::vector<char> buf(MAX_LINE_BYTES);
  LineSplitter splitter(buf.data(), &collect, &lines);
  for (size_t i = 0; i < input.size(); i += chunk) {
    splitter.feed(input.data() + i, std::min(chunk, input.size() - i));
  }
  splitter.finish();
  return lines;
}

// Availability predicate over a vector<bool>.
bool availableIn(void* ctx, const uint32_t chapter) { return (*static_cast<std::vector<bool>*>(ctx))[chapter]; }

TEST(XtcSeriesHeader, AcceptsExactHeaderWithBomAndCr) {
  EXPECT_TRUE(isHeaderLine("XSERIES 1"sv));
  EXPECT_TRUE(isHeaderLine("XSERIES 1\r"sv));
  EXPECT_TRUE(isHeaderLine("\xEF\xBB\xBFXSERIES 1"sv));
  EXPECT_FALSE(isHeaderLine("XSERIES 2"sv));
  EXPECT_FALSE(isHeaderLine("xseries 1"sv));
  EXPECT_FALSE(isHeaderLine(""sv));
}

TEST(XtcSeriesEntry, ParsesAllFields) {
  EntryView e;
  ASSERT_TRUE(parseEntry("c000123.xtch\t42\tChapter 1: Romance Dawn\r"sv, e));
  EXPECT_EQ(e.file, "c000123.xtch"sv);
  EXPECT_EQ(e.pages, 42u);
  EXPECT_EQ(e.title, "Chapter 1: Romance Dawn"sv);
}

TEST(XtcSeriesEntry, KeepsTabsInsideTitleAndAllowsMissingTitleOrPages) {
  EntryView e;
  ASSERT_TRUE(parseEntry("a.xtc\t0\tPart\tOne"sv, e));
  EXPECT_EQ(e.title, "Part\tOne"sv);
  ASSERT_TRUE(parseEntry("b.XTC\t7"sv, e));
  EXPECT_EQ(e.pages, 7u);
  EXPECT_TRUE(e.title.empty());
  ASSERT_TRUE(parseEntry("c.xtch\t\tTitle"sv, e));
  EXPECT_EQ(e.pages, 0u);
}

TEST(XtcSeriesEntry, RejectsMalformedAndUnsafeLines) {
  EntryView e;
  EXPECT_FALSE(parseEntry(""sv, e));
  EXPECT_FALSE(parseEntry("a.xtc"sv, e));                // no tab
  EXPECT_FALSE(parseEntry("a.epub\t1\tx"sv, e));         // not an XTC file
  EXPECT_FALSE(parseEntry("../a.xtc\t1\tx"sv, e));       // path escape
  EXPECT_FALSE(parseEntry("sub/a.xtc\t1\tx"sv, e));      // no subfolders
  EXPECT_FALSE(parseEntry("a.xtc\t1x\tx"sv, e));         // bad page count
  EXPECT_FALSE(parseEntry("a.xtc\t1234567890\tx"sv, e));  // page count overflow
  EXPECT_FALSE(parseEntry("\t1\tx"sv, e));               // empty file name
}

TEST(XtcSeriesEntry, FormatRoundTripsThroughParse) {
  char buf[64];
  const size_t n = formatEntry(buf, sizeof(buf), "ch10.xtch"sv, 1234, "ch10"sv);
  ASSERT_GT(n, 0u);
  EXPECT_EQ(std::string(buf, n), "ch10.xtch\t1234\tch10\n");
  EntryView e;
  ASSERT_TRUE(parseEntry(std::string_view(buf, n - 1), e));
  EXPECT_EQ(e.file, "ch10.xtch"sv);
  EXPECT_EQ(e.pages, 1234u);
  EXPECT_EQ(formatEntry(buf, 8, "ch10.xtch"sv, 0, ""sv), 0u);
}

TEST(XtcSeriesEntry, FileStem) {
  EXPECT_EQ(fileStem("ch10.xtch"sv), "ch10"sv);
  EXPECT_EQ(fileStem("vol.1.xtc"sv), "vol.1"sv);
  EXPECT_EQ(fileStem("noext"sv), "noext"sv);
}

TEST(XtcSeriesOrder, NaturalSortPutsCh2BeforeCh10) {
  std::vector<std::string> files{"ch10.xtc", "Ch1.xtc", "ch2.xtch", "ch002b.xtc", "ch100.xtc", "ch9.xtc"};
  sortNatural(files);
  EXPECT_EQ(files, (std::vector<std::string>{"Ch1.xtc", "ch2.xtch", "ch002b.xtc", "ch9.xtc", "ch10.xtc", "ch100.xtc"}));
}

TEST(XtcSeriesLines, ReportsOffsetsIndependentOfChunking) {
  const std::string input = "XSERIES 1\na.xtc\t1\tA\r\n\nb.xtc\t2\tB";
  for (size_t chunk : {1u, 3u, 7u, 64u}) {
    const auto lines = split(input, chunk);
    ASSERT_EQ(lines.size(), 4u) << "chunk " << chunk;
    EXPECT_EQ(lines[0].offset, 0u);
    EXPECT_EQ(lines[0].text, "XSERIES 1");
    EXPECT_EQ(lines[1].offset, 10u);
    EXPECT_EQ(lines[1].text, "a.xtc\t1\tA\r");
    EXPECT_EQ(lines[2].offset, 21u);
    EXPECT_EQ(lines[2].text, "");
    EXPECT_EQ(lines[3].offset, 22u);
    EXPECT_EQ(lines[3].text, "b.xtc\t2\tB");
    EXPECT_EQ(input.substr(lines[3].offset, 5), "b.xtc");
  }
}

TEST(XtcSeriesLines, BoundsLongLinesAndKeepsNextOffsetExact) {
  const std::string longTitle(MAX_LINE_BYTES * 3, 'x');
  const std::string input = "a.xtc\t1\t" + longTitle + "\nb.xtc\t2\tB\n";
  const auto lines = split(input, 50);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_TRUE(lines[0].truncated);
  EXPECT_EQ(lines[0].text.size(), MAX_LINE_BYTES);
  EntryView e;
  EXPECT_TRUE(parseEntry(lines[0].text, e));  // file and pages survive truncation
  EXPECT_FALSE(lines[1].truncated);
  EXPECT_EQ(lines[1].offset, input.find("b.xtc"));
}

TEST(XtcSeriesLines, HandlesThousandsOfChaptersStreaming) {
  std::string input = "XSERIES 1\n";
  char buf[64];
  for (uint32_t i = 0; i < 2000; ++i) {
    const std::string file = "c" + std::to_string(i) + ".xtch";
    input.append(buf, formatEntry(buf, sizeof(buf), file, i, "T"));
  }
  const auto lines = split(input, 256);
  ASSERT_EQ(lines.size(), 2001u);
  EntryView e;
  ASSERT_TRUE(parseEntry(lines[1500].text, e));
  EXPECT_EQ(e.file, "c1499.xtch"sv);
  EXPECT_EQ(input.substr(lines[1500].offset, 10), "c1499.xtch");
}

TEST(XtcSeriesTurn, StaysInsideChapter) {
  std::vector<bool> avail{true, true};
  auto t = planTurn(0, 3, 10, true, 2, &availableIn, &avail);
  EXPECT_EQ(t.kind, Turn::Kind::Page);
  EXPECT_EQ(t.page, 4u);
  t = planTurn(0, 3, 10, false, 2, &availableIn, &avail);
  EXPECT_EQ(t.kind, Turn::Kind::Page);
  EXPECT_EQ(t.page, 2u);
}

TEST(XtcSeriesTurn, CrossesChapterBoundariesBothWays) {
  std::vector<bool> avail{true, true, true};
  auto t = planTurn(0, 9, 10, true, 3, &availableIn, &avail);
  EXPECT_EQ(t.kind, Turn::Kind::Chapter);
  EXPECT_EQ(t.chapter, 1u);
  EXPECT_FALSE(t.toLastPage);
  t = planTurn(1, 0, 5, false, 3, &availableIn, &avail);
  EXPECT_EQ(t.kind, Turn::Kind::Chapter);
  EXPECT_EQ(t.chapter, 0u);
  EXPECT_TRUE(t.toLastPage);
}

TEST(XtcSeriesTurn, SkipsMissingChapters) {
  std::vector<bool> avail{true, false, false, true};
  auto t = planTurn(0, 0, 1, true, 4, &availableIn, &avail);
  EXPECT_EQ(t.kind, Turn::Kind::Chapter);
  EXPECT_EQ(t.chapter, 3u);
  t = planTurn(3, 0, 1, false, 4, &availableIn, &avail);
  EXPECT_EQ(t.kind, Turn::Kind::Chapter);
  EXPECT_EQ(t.chapter, 0u);
  EXPECT_EQ(findAvailable(1, 1, 3, &availableIn, &avail), -1);
}

TEST(XtcSeriesTurn, EndAndStartOfSeries) {
  std::vector<bool> avail{true, true, false};
  auto t = planTurn(1, 4, 5, true, 3, &availableIn, &avail);
  EXPECT_EQ(t.kind, Turn::Kind::End);
  EXPECT_EQ(t.page, 5u);
  // Back from the end screen returns to the last page.
  t = planTurn(1, 5, 5, false, 3, &availableIn, &avail);
  EXPECT_EQ(t.kind, Turn::Kind::Page);
  EXPECT_EQ(t.page, 4u);
  t = planTurn(0, 0, 5, false, 3, &availableIn, &avail);
  EXPECT_EQ(t.kind, Turn::Kind::None);
}

TEST(XtcSeriesTurn, EmptyChapterAdvances) {
  std::vector<bool> avail{true, true};
  const auto t = planTurn(0, 0, 0, true, 2, &availableIn, &avail);
  EXPECT_EQ(t.kind, Turn::Kind::Chapter);
  EXPECT_EQ(t.chapter, 1u);
}

TEST(XtcSeriesPercent, WeightsChaptersEqually) {
  EXPECT_EQ(seriesPercent(0, 0, 0, 0), 0);
  EXPECT_EQ(seriesPercent(0, 4, 9, 10), 25);
  EXPECT_EQ(seriesPercent(1, 4, 4, 10), 37);
  EXPECT_EQ(seriesPercent(3, 4, 9, 10), 100);
  EXPECT_EQ(seriesPercent(3, 4, 50, 10), 100);
}

TEST(XtcSeriesProgress, RoundTrips) {
  uint8_t buf[MAX_PROGRESS_BYTES];
  const size_t n = encodeProgress({1234, 56, "c000123.xtch"sv}, buf, sizeof(buf));
  ASSERT_EQ(n, PROGRESS_HEADER_BYTES + 12);
  Progress p;
  ASSERT_TRUE(decodeProgress(buf, n, p));
  EXPECT_EQ(p.chapter, 1234u);
  EXPECT_EQ(p.page, 56u);
  EXPECT_EQ(p.file, "c000123.xtch"sv);
}

TEST(XtcSeriesProgress, RejectsTruncatedOrForeignData) {
  uint8_t buf[MAX_PROGRESS_BYTES];
  const size_t n = encodeProgress({1, 2, "a.xtc"sv}, buf, sizeof(buf));
  Progress p;
  EXPECT_FALSE(decodeProgress(buf, n - 1, p));
  EXPECT_FALSE(decodeProgress(buf, 4, p));  // a plain XTC progress.bin is 4 bytes
  buf[0] = 99;
  EXPECT_FALSE(decodeProgress(buf, n, p));
  EXPECT_EQ(encodeProgress({0, 0, "a.xtc"sv}, buf, PROGRESS_HEADER_BYTES), 0u);
  const std::string tooLong(256, 'a');
  EXPECT_EQ(encodeProgress({0, 0, tooLong}, buf, sizeof(buf)), 0u);
}

}  // namespace
