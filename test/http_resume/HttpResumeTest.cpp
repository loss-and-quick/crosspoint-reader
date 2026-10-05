#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "HttpResume.h"

using namespace http_resume;

namespace {

// Bitwise zlib CRC-32, same polynomial and chaining as esp_rom_crc32_le.
uint32_t crc32(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

}  // namespace

// --- Sidecar ---

TEST(HttpResumeMeta, RoundTrips) {
  Meta meta;
  meta.urlHash = 0x0badf00d;
  meta.total = 123456789012ull;
  strcpy(meta.validator, "Wed, 21 Oct 2015 07:28:00 GMT");
  char buf[MAX_META_LEN];
  const size_t n = formatMeta(meta, buf, sizeof(buf));
  ASSERT_GT(n, 0u);
  Meta parsed;
  ASSERT_TRUE(parseMeta(buf, n, parsed));
  EXPECT_EQ(parsed.urlHash, meta.urlHash);
  EXPECT_EQ(parsed.total, meta.total);
  EXPECT_STREQ(parsed.validator, meta.validator);
}

TEST(HttpResumeMeta, MaxValidatorFitsBuffer) {
  Meta meta;
  meta.urlHash = 0xffffffffu;
  meta.total = UINT64_MAX;
  memset(meta.validator, 'x', MAX_VALIDATOR_LEN);
  meta.validator[MAX_VALIDATOR_LEN] = '\0';
  char buf[MAX_META_LEN];
  const size_t n = formatMeta(meta, buf, sizeof(buf));
  ASSERT_GT(n, 0u);
  Meta parsed;
  ASSERT_TRUE(parseMeta(buf, n, parsed));
  EXPECT_EQ(parsed.total, UINT64_MAX);
}

TEST(HttpResumeMeta, EmptyValidatorRoundTrips) {
  Meta meta;
  meta.urlHash = 1;
  char buf[MAX_META_LEN];
  const size_t n = formatMeta(meta, buf, sizeof(buf));
  Meta parsed;
  ASSERT_TRUE(parseMeta(buf, n, parsed));
  EXPECT_EQ(parsed.total, 0u);
  EXPECT_STREQ(parsed.validator, "");
}

TEST(HttpResumeMeta, RejectsCorruptOrTruncated) {
  Meta parsed;
  const std::string good = "CPRESUME1 0000abcd 42\n\"etag\"\n";
  ASSERT_TRUE(parseMeta(good.data(), good.size(), parsed));
  for (size_t cut = 0; cut < good.size(); ++cut) {
    EXPECT_FALSE(parseMeta(good.data(), cut, parsed)) << "cut at " << cut;
  }
  const std::string badMagic = "CPRESUME2 0000abcd 42\n\n";
  EXPECT_FALSE(parseMeta(badMagic.data(), badMagic.size(), parsed));
  const std::string badHash = "CPRESUME1 0000ABCD 42\n\n";
  EXPECT_FALSE(parseMeta(badHash.data(), badHash.size(), parsed));
  const std::string badTotal = "CPRESUME1 0000abcd -4\n\n";
  EXPECT_FALSE(parseMeta(badTotal.data(), badTotal.size(), parsed));
  const std::string embeddedNul("CPRESUME1 0000abcd 42\n\"a\0b\"\n", 29);
  EXPECT_FALSE(parseMeta(embeddedNul.data(), embeddedNul.size(), parsed));
  EXPECT_FALSE(parseMeta(nullptr, 0, parsed));
}

TEST(HttpResumeMeta, UrlHashDistinguishesUrls) {
  EXPECT_EQ(urlHash("http://a/b"), urlHash("http://a/b"));
  EXPECT_NE(urlHash("http://a/b"), urlHash("http://a/c"));
  EXPECT_EQ(urlHash(""), 2166136261u);
}

TEST(HttpResumeMeta, SelectsStrongEtagThenLastModified) {
  char out[MAX_VALIDATOR_LEN + 1];
  selectValidator("\"abc\"", "Wed, 21 Oct 2015 07:28:00 GMT", out, sizeof(out));
  EXPECT_STREQ(out, "\"abc\"");
  selectValidator("W/\"abc\"", "Wed, 21 Oct 2015 07:28:00 GMT", out, sizeof(out));
  EXPECT_STREQ(out, "Wed, 21 Oct 2015 07:28:00 GMT");
  selectValidator("W/\"abc\"", nullptr, out, sizeof(out));
  EXPECT_STREQ(out, "");
  const std::string longEtag = "\"" + std::string(MAX_VALIDATOR_LEN, 'e') + "\"";
  selectValidator(longEtag.c_str(), "", out, sizeof(out));
  EXPECT_STREQ(out, "");
  selectValidator("\"a\nb\"", nullptr, out, sizeof(out));
  EXPECT_STREQ(out, "");
}

// --- Request planning ---

TEST(HttpResumePlan, NoPartFileStartsFresh) {
  LocalState local;
  local.expectedSize = 500;
  const RequestPlan plan = planRequest(local);
  EXPECT_FALSE(plan.resume);
  EXPECT_EQ(plan.knownTotal, 500u);
}

TEST(HttpResumePlan, ZeroLengthPartStartsFresh) {
  LocalState local;
  local.partSize = 0;
  local.metaValid = true;
  local.metaTotal = 1000;
  EXPECT_FALSE(planRequest(local).resume);
}

TEST(HttpResumePlan, PartWithoutSidecarStartsFresh) {
  LocalState local;
  local.partSize = 400;
  local.metaValid = false;
  EXPECT_FALSE(planRequest(local).resume);
}

TEST(HttpResumePlan, ResumesFromPartSize) {
  LocalState local;
  local.partSize = 400;
  local.metaValid = true;
  local.metaTotal = 1000;
  const RequestPlan plan = planRequest(local);
  EXPECT_TRUE(plan.resume);
  EXPECT_EQ(plan.offset, 400u);
  EXPECT_EQ(plan.knownTotal, 1000u);
}

TEST(HttpResumePlan, ResumesWithUnknownTotal) {
  LocalState local;
  local.partSize = 400;
  local.metaValid = true;
  const RequestPlan plan = planRequest(local);
  EXPECT_TRUE(plan.resume);
  EXPECT_EQ(plan.knownTotal, 0u);
}

TEST(HttpResumePlan, PartLargerThanTotalStartsFresh) {
  LocalState local;
  local.partSize = 1001;
  local.metaValid = true;
  local.metaTotal = 1000;
  EXPECT_FALSE(planRequest(local).resume);

  local.metaTotal = 0;
  local.expectedSize = 1000;
  EXPECT_FALSE(planRequest(local).resume);
}

TEST(HttpResumePlan, SidecarTotalConflictingWithExpectedSizeStartsFresh) {
  LocalState local;
  local.partSize = 10;
  local.metaValid = true;
  local.metaTotal = 1000;
  local.expectedSize = 2000;
  const RequestPlan plan = planRequest(local);
  EXPECT_FALSE(plan.resume);
  EXPECT_EQ(plan.knownTotal, 2000u);
}

TEST(HttpResumePlan, CompletePartStillAsksServer) {
  LocalState local;
  local.partSize = 1000;
  local.metaValid = true;
  local.metaTotal = 1000;
  const RequestPlan plan = planRequest(local);
  EXPECT_TRUE(plan.resume);
  EXPECT_EQ(plan.offset, 1000u);
}

// --- 416 handling ---

TEST(HttpResumePartComplete, SidecarTotalEqualsPartSize) {
  EXPECT_TRUE(partIsComplete(1000, 1000, 0));
  EXPECT_TRUE(partIsComplete(1000, 1000, 1000));
}

TEST(HttpResumePartComplete, RejectsUnknownOrDifferentTotals) {
  EXPECT_FALSE(partIsComplete(0, 0, 0));
  EXPECT_FALSE(partIsComplete(1000, 0, 0));
  EXPECT_FALSE(partIsComplete(1000, 2000, 0));
  EXPECT_FALSE(partIsComplete(1000, 1000, 900));
}

// --- Completion ---

TEST(HttpResumeVerify, AcceptsExactSize) {
  EXPECT_EQ(verifyComplete(1000, 1000, 0, false, 0, 0), Verdict::Ok);
  EXPECT_EQ(verifyComplete(1000, 0, 0, false, 0, 0), Verdict::Ok);
  EXPECT_EQ(verifyComplete(1000, 1000, 1000, true, 0x1234u, 0x1234u), Verdict::Ok);
}

TEST(HttpResumeVerify, EmptyIsRejected) { EXPECT_EQ(verifyComplete(0, 0, 0, false, 0, 0), Verdict::Empty); }

TEST(HttpResumeVerify, ShortIsIncomplete) {
  EXPECT_EQ(verifyComplete(999, 1000, 0, false, 0, 0), Verdict::Incomplete);
  EXPECT_EQ(verifyComplete(999, 0, 1000, false, 0, 0), Verdict::Incomplete);
}

TEST(HttpResumeVerify, LongOrUnexpectedIsSizeMismatch) {
  EXPECT_EQ(verifyComplete(1001, 1000, 0, false, 0, 0), Verdict::SizeMismatch);
  EXPECT_EQ(verifyComplete(1000, 1000, 900, false, 0, 0), Verdict::SizeMismatch);
}

TEST(HttpResumeVerify, CrcMismatch) {
  EXPECT_EQ(verifyComplete(1000, 1000, 0, true, 0xdeadbeefu, 0xcafebabeu), Verdict::CrcMismatch);
}

// The resume path seeds the running CRC with the ".part" prefix, then chains
// the appended body into the same accumulator.
TEST(HttpResumeVerify, ChainedCrcOverPrefixMatchesWholeFile) {
  uint8_t data[3000];
  for (size_t i = 0; i < sizeof(data); ++i) data[i] = static_cast<uint8_t>(i * 131 + 7);
  const uint32_t whole = crc32(0, data, sizeof(data));

  uint32_t crc = 0;
  constexpr size_t PREFIX = 1234;
  constexpr size_t CHUNK = 512;
  for (size_t pos = 0; pos < PREFIX; pos += CHUNK) {
    crc = crc32(crc, data + pos, PREFIX - pos < CHUNK ? PREFIX - pos : CHUNK);
  }
  crc = crc32(crc, data + PREFIX, sizeof(data) - PREFIX);
  EXPECT_EQ(verifyComplete(sizeof(data), sizeof(data), 0, true, crc, whole), Verdict::Ok);
  EXPECT_EQ(crc32(0, reinterpret_cast<const uint8_t*>("123456789"), 9), 0xCBF43926u);
}
