#pragma once

#include <cstddef>
#include <cstdint>

// Pure bookkeeping for cross-session HTTP download resume (RFC 9110
// Range/If-Range). Within one call, freeink::fetchResumable already resumes a
// dropped connection; this layer lets a later call continue a ".part" file left
// by an earlier one. No I/O and no platform headers, so it is host-testable;
// HttpDownloader::downloadToFileResumable() drives it against the SD card.
namespace http_resume {

// Longest validator (strong ETag or Last-Modified) kept in the sidecar. Typical
// ETags are 20-40 chars and an HTTP-date is 29; longer ones are not stored and
// the resume then goes without If-Range.
constexpr size_t MAX_VALIDATOR_LEN = 63;
// Upper bound for the serialized sidecar (header line + validator line).
constexpr size_t MAX_META_LEN = 128;

// Contents of the "<dest>.part.meta" sidecar. It is what makes a ".part"
// resumable: a ".part" without a matching sidecar is discarded.
struct Meta {
  uint32_t urlHash = 0;
  uint64_t total = 0;  // full entity length, 0 = unknown
  char validator[MAX_VALIDATOR_LEN + 1] = {};
};

// FNV-1a over the request URL; ties a ".part" to the URL that produced it.
uint32_t urlHash(const char* url);

// Writes the sidecar text into out (NUL-terminated). Returns the length, or 0
// if it does not fit.
size_t formatMeta(const Meta& meta, char* out, size_t outSize);
// Parses sidecar text. Returns false on any malformed input.
bool parseMeta(const char* data, size_t len, Meta& out);

// Picks the If-Range validator from a response: a strong ETag, else
// Last-Modified. Weak ETags ("W/...") are never valid in If-Range. Either
// argument may be null. Returns an empty string when none fits.
void selectValidator(const char* etag, const char* lastModified, char* out, size_t outSize);

struct LocalState {
  uint64_t partSize = 0;      // bytes already in "<dest>.part"
  bool metaValid = false;     // sidecar present, parsed, and same URL hash
  uint64_t metaTotal = 0;     // Meta::total when metaValid
  uint64_t expectedSize = 0;  // caller-supplied, 0 = unknown
};

struct RequestPlan {
  bool resume = false;      // send "Range: bytes=<offset>-"
  uint64_t offset = 0;      // resume point (== partSize when resuming)
  uint64_t knownTotal = 0;  // best local knowledge of the entity length
};

// Decides whether the existing ".part" can be resumed. When resume is false
// the caller must discard ".part" and its sidecar before the request.
RequestPlan planRequest(const LocalState& local);

// True when a 416 for a ranged request means the ".part" already holds the whole
// entity: the sidecar total equals the part size (and the caller's expected size,
// if any). With If-Range sent, a changed entity would have answered 200, not 416.
bool partIsComplete(uint64_t partSize, uint64_t metaTotal, uint64_t expectedSize);

enum class Verdict : uint8_t {
  Ok,
  Empty,         // nothing received
  Incomplete,    // shorter than the known total; keep ".part" for resume
  SizeMismatch,  // longer than the known total, or not the expected size
  CrcMismatch,
};

// Checks a finished transfer before ".part" is renamed into place.
Verdict verifyComplete(uint64_t size, uint64_t total, uint64_t expectedSize, bool checkCrc, uint32_t crc,
                       uint32_t expectedCrc);

}  // namespace http_resume
