#include "HttpResume.h"

#include <cstdio>
#include <cstring>

namespace http_resume {

namespace {

constexpr char META_MAGIC[] = "CPRESUME1";

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Strict unsigned decimal parse of [p, end). Rejects empty input and overflow.
bool parseU64(const char* p, const char* end, uint64_t& out) {
  if (p >= end) return false;
  uint64_t value = 0;
  for (; p < end; ++p) {
    if (!isDigit(*p)) return false;
    const uint64_t digit = static_cast<uint64_t>(*p - '0');
    if (value > (UINT64_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  }
  out = value;
  return true;
}

bool parseHex32(const char* p, const char* end, uint32_t& out) {
  if (end - p != 8) return false;
  uint32_t value = 0;
  for (; p < end; ++p) {
    const char c = *p;
    uint32_t nibble;
    if (isDigit(c)) {
      nibble = static_cast<uint32_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      nibble = static_cast<uint32_t>(c - 'a' + 10);
    } else {
      return false;
    }
    value = (value << 4) | nibble;
  }
  out = value;
  return true;
}

bool isEmpty(const char* s) { return s == nullptr || *s == '\0'; }

bool isStrongEtag(const char* etag) { return !isEmpty(etag) && etag[0] == '"'; }

}  // namespace

uint32_t urlHash(const char* url) {
  uint32_t hash = 2166136261u;
  for (const char* p = url; p && *p; ++p) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= 16777619u;
  }
  return hash;
}

size_t formatMeta(const Meta& meta, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) return 0;
  // Decimal by hand: newlib-nano printf builds lack %llu.
  char totalText[21];
  size_t pos = sizeof(totalText) - 1;
  totalText[pos] = '\0';
  uint64_t total = meta.total;
  do {
    totalText[--pos] = static_cast<char>('0' + total % 10);
    total /= 10;
  } while (total != 0);
  const int n = snprintf(out, outSize, "%s %08lx %s\n%s\n", META_MAGIC, static_cast<unsigned long>(meta.urlHash),
                         totalText + pos, meta.validator);
  if (n < 0 || static_cast<size_t>(n) >= outSize) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

bool parseMeta(const char* data, size_t len, Meta& out) {
  if (data == nullptr) return false;
  const char* p = data;
  const char* end = data + len;

  const size_t magicLen = sizeof(META_MAGIC) - 1;
  if (len < magicLen + 1 || memcmp(p, META_MAGIC, magicLen) != 0 || p[magicLen] != ' ') return false;
  p += magicLen + 1;

  const char* hashEnd = static_cast<const char*>(memchr(p, ' ', end - p));
  if (hashEnd == nullptr) return false;
  Meta meta;
  if (!parseHex32(p, hashEnd, meta.urlHash)) return false;
  p = hashEnd + 1;

  const char* totalEnd = static_cast<const char*>(memchr(p, '\n', end - p));
  if (totalEnd == nullptr || !parseU64(p, totalEnd, meta.total)) return false;
  p = totalEnd + 1;

  const char* validatorEnd = static_cast<const char*>(memchr(p, '\n', end - p));
  if (validatorEnd == nullptr) return false;
  const size_t validatorLen = static_cast<size_t>(validatorEnd - p);
  if (validatorLen > MAX_VALIDATOR_LEN || memchr(p, '\0', validatorLen) != nullptr) return false;
  memcpy(meta.validator, p, validatorLen);
  meta.validator[validatorLen] = '\0';

  out = meta;
  return true;
}

void selectValidator(const char* etag, const char* lastModified, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  out[0] = '\0';
  const size_t limit = outSize - 1 < MAX_VALIDATOR_LEN ? outSize - 1 : MAX_VALIDATOR_LEN;
  const auto usable = [limit](const char* v) {
    return !isEmpty(v) && strlen(v) <= limit && strpbrk(v, "\r\n") == nullptr;
  };
  const char* chosen = nullptr;
  if (isStrongEtag(etag) && usable(etag)) {
    chosen = etag;
  } else if (usable(lastModified)) {
    chosen = lastModified;
  }
  if (chosen != nullptr) memcpy(out, chosen, strlen(chosen) + 1);
}

RequestPlan planRequest(const LocalState& local) {
  RequestPlan plan;
  plan.knownTotal = local.expectedSize;
  // A sidecar total that disagrees with the caller's expected size describes
  // a different version of the entity.
  const bool metaConflicts =
      local.metaValid && local.metaTotal != 0 && local.expectedSize != 0 && local.metaTotal != local.expectedSize;
  if (!local.metaValid || metaConflicts || local.partSize == 0) return plan;
  const uint64_t known = local.expectedSize != 0 ? local.expectedSize : local.metaTotal;
  if (known != 0 && local.partSize > known) return plan;
  plan.resume = true;
  plan.offset = local.partSize;
  plan.knownTotal = known;
  return plan;
}

bool partIsComplete(uint64_t partSize, uint64_t metaTotal, uint64_t expectedSize) {
  return partSize != 0 && metaTotal == partSize && (expectedSize == 0 || expectedSize == partSize);
}

Verdict verifyComplete(uint64_t size, uint64_t total, uint64_t expectedSize, bool checkCrc, uint32_t crc,
                       uint32_t expectedCrc) {
  if (size == 0) return Verdict::Empty;
  if (expectedSize != 0 && total != 0 && total != expectedSize) return Verdict::SizeMismatch;
  const uint64_t known = expectedSize != 0 ? expectedSize : total;
  if (known != 0 && size < known) return Verdict::Incomplete;
  if (known != 0 && size > known) return Verdict::SizeMismatch;
  if (checkCrc && crc != expectedCrc) return Verdict::CrcMismatch;
  return Verdict::Ok;
}

}  // namespace http_resume
