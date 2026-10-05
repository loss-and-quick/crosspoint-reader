#include <gtest/gtest.h>

#include <string>

#include "OpdsParser.h"

namespace {

// Feeds a whole document through the same Print interface the firmware streams into.
bool parseFeed(OpdsParser& parser, const std::string& xml) {
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();
  return !parser.error();
}

constexpr const char* FEED_OPEN = R"(<?xml version="1.0" encoding="UTF-8"?><feed xmlns="http://www.w3.org/2005/Atom">)";

}  // namespace

TEST(OpdsParser, SubsectionLinkBecomesNavigationEntry) {
  OpdsParser parser;
  const std::string xml = std::string(FEED_OPEN) +
                          R"(<entry><title>Authors</title><id>urn:authors</id>
        <link rel="subsection" type="application/atom+xml;profile=opds-catalog" href="/opds/authors"/>
      </entry></feed>)";
  ASSERT_TRUE(parseFeed(parser, xml));
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].type, OpdsEntryType::NAVIGATION);
  EXPECT_EQ(parser.getEntries()[0].href, "/opds/authors");
}

TEST(OpdsParser, SearchLinkInsideEntryIsNotNavigationTarget) {
  OpdsParser parser;
  const std::string xml = std::string(FEED_OPEN) +
                          R"(<entry><title>Authors</title><id>urn:authors</id>
        <link rel="search" type="application/atom+xml" href="/opds/search/{searchTerms}"/>
        <link rel="subsection" type="application/atom+xml;profile=opds-catalog" href="/opds/authors"/>
      </entry></feed>)";
  ASSERT_TRUE(parseFeed(parser, xml));
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].type, OpdsEntryType::NAVIGATION);
  EXPECT_EQ(parser.getEntries()[0].href, "/opds/authors");
}

TEST(OpdsParser, SearchLinkAfterSubsectionDoesNotReplaceTarget) {
  OpdsParser parser;
  const std::string xml = std::string(FEED_OPEN) +
                          R"(<entry><title>Authors</title><id>urn:authors</id>
        <link rel="subsection" type="application/atom+xml;profile=opds-catalog" href="/opds/authors"/>
        <link rel="search" type="application/atom+xml" href="/opds/search/{searchTerms}"/>
      </entry></feed>)";
  ASSERT_TRUE(parseFeed(parser, xml));
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].href, "/opds/authors");
}

TEST(OpdsParser, EntryWithOnlySearchLinkIsDropped) {
  OpdsParser parser;
  const std::string xml = std::string(FEED_OPEN) +
                          R"(<entry><title>Search</title><id>urn:search</id>
        <link rel="search" type="application/atom+xml" href="/opds/search/{searchTerms}"/>
      </entry></feed>)";
  ASSERT_TRUE(parseFeed(parser, xml));
  EXPECT_TRUE(parser.getEntries().empty());
}

TEST(OpdsParser, FeedLevelSearchTemplateIsStillCaptured) {
  OpdsParser parser;
  const std::string xml = std::string(FEED_OPEN) +
                          R"(<link rel="search" type="application/atom+xml" href="/opds/search/{searchTerms}"/>
      <entry><title>Authors</title><id>urn:authors</id>
        <link rel="subsection" type="application/atom+xml" href="/opds/authors"/>
      </entry></feed>)";
  ASSERT_TRUE(parseFeed(parser, xml));
  EXPECT_EQ(parser.getSearchTemplate(), "/opds/search/{searchTerms}");
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].href, "/opds/authors");
}

TEST(OpdsParser, AcquisitionLinkStillWinsOverNavigation) {
  OpdsParser parser;
  const std::string xml = std::string(FEED_OPEN) +
                          R"(<entry><title>Book</title><id>urn:book</id>
        <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/book.epub"/>
        <link rel="search" type="application/atom+xml" href="/opds/search/{searchTerms}"/>
      </entry></feed>)";
  ASSERT_TRUE(parseFeed(parser, xml));
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].type, OpdsEntryType::BOOK);
  EXPECT_EQ(parser.getEntries()[0].href, "/book.epub");
}
