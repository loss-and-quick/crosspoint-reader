#include "XtcSeriesFormat.h"

#include <FsHelpers.h>

#include <algorithm>
#include <cstring>

namespace xtc::series {

namespace {

std::string_view stripCr(std::string_view line) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  return line;
}

void putLe32(uint8_t* p, const uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

uint32_t getLe32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

}  // namespace

bool isHeaderLine(std::string_view line) {
  constexpr std::string_view BOM = "\xEF\xBB\xBF";
  if (line.substr(0, BOM.size()) == BOM) line.remove_prefix(BOM.size());
  return stripCr(line) == HEADER;
}

bool parseEntry(std::string_view line, EntryView& out) {
  line = stripCr(line);
  const size_t tab1 = line.find('\t');
  if (tab1 == std::string_view::npos) return false;
  const std::string_view file = line.substr(0, tab1);
  if (!FsHelpers::isSafePathComponent(file) || !FsHelpers::hasXtcExtension(file)) return false;

  std::string_view rest = line.substr(tab1 + 1);
  const size_t tab2 = rest.find('\t');
  const std::string_view pagesField = rest.substr(0, tab2);
  if (pagesField.size() > 9) return false;  // keeps the value inside uint32_t
  uint32_t pages = 0;
  for (const char c : pagesField) {
    if (c < '0' || c > '9') return false;
    pages = pages * 10 + static_cast<uint32_t>(c - '0');
  }

  out.file = file;
  out.pages = pages;
  out.title = tab2 == std::string_view::npos ? std::string_view{} : rest.substr(tab2 + 1);
  return true;
}

std::string_view fileStem(const std::string_view file) {
  const size_t dot = file.rfind('.');
  return dot == std::string_view::npos || dot == 0 ? file : file.substr(0, dot);
}

size_t formatEntry(char* out, const size_t cap, const std::string_view file, const uint32_t pages,
                   const std::string_view title) {
  char pagesBuf[11];
  size_t pagesLen = 0;
  uint32_t v = pages;
  do {
    pagesBuf[pagesLen++] = static_cast<char>('0' + v % 10);
    v /= 10;
  } while (v != 0);

  const size_t total = file.size() + 1 + pagesLen + 1 + title.size() + 1;
  if (total > cap) return 0;
  char* p = out;
  memcpy(p, file.data(), file.size());
  p += file.size();
  *p++ = '\t';
  for (size_t i = 0; i < pagesLen; ++i) *p++ = pagesBuf[pagesLen - 1 - i];
  *p++ = '\t';
  memcpy(p, title.data(), title.size());
  p += title.size();
  *p++ = '\n';
  return total;
}

void sortNatural(std::vector<std::string>& files) { std::sort(files.begin(), files.end(), FsHelpers::naturalLess); }

void LineSplitter::emit() {
  fn(ctx, lineStart, std::string_view(buf, lineLen), truncated);
  lineLen = 0;
  truncated = false;
}

void LineSplitter::feed(const char* data, const size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char c = data[i];
    if (c == '\n') {
      emit();
      lineStart = streamOffset + 1;
    } else if (lineLen < MAX_LINE_BYTES) {
      buf[lineLen++] = c;
    } else {
      truncated = true;
    }
    ++streamOffset;
  }
}

void LineSplitter::finish() {
  if (lineLen > 0 || truncated) emit();
  lineStart = streamOffset;
}

int32_t findAvailable(int64_t from, const int dir, const uint32_t count, const AvailableFn available, void* ctx) {
  for (; from >= 0 && from < static_cast<int64_t>(count); from += dir) {
    if (available(ctx, static_cast<uint32_t>(from))) return static_cast<int32_t>(from);
  }
  return -1;
}

Turn planTurn(const uint32_t chapter, const uint32_t page, const uint32_t pageCount, const bool forward,
              const uint32_t chapterCount, const AvailableFn available, void* ctx) {
  Turn turn;
  if (forward) {
    if (page + 1 < pageCount) {
      turn.kind = Turn::Kind::Page;
      turn.page = page + 1;
      return turn;
    }
    const int32_t next = findAvailable(static_cast<int64_t>(chapter) + 1, 1, chapterCount, available, ctx);
    if (next < 0) {
      turn.kind = Turn::Kind::End;
      turn.chapter = chapter;
      turn.page = pageCount;
      return turn;
    }
    turn.kind = Turn::Kind::Chapter;
    turn.chapter = static_cast<uint32_t>(next);
    return turn;
  }

  if (page > 0) {
    turn.kind = Turn::Kind::Page;
    // From the end-of-series screen (page == pageCount) back to the last page.
    turn.page = std::min(page - 1, pageCount > 0 ? pageCount - 1 : 0);
    return turn;
  }
  const int32_t prev = findAvailable(static_cast<int64_t>(chapter) - 1, -1, chapterCount, available, ctx);
  if (prev < 0) return turn;
  turn.kind = Turn::Kind::Chapter;
  turn.chapter = static_cast<uint32_t>(prev);
  turn.toLastPage = true;
  return turn;
}

uint8_t seriesPercent(const uint32_t chapter, const uint32_t chapterCount, const uint32_t page,
                      const uint32_t pageCount) {
  if (chapterCount == 0) return 0;
  const uint64_t pages = pageCount == 0 ? 1 : pageCount;
  const uint64_t within = std::min<uint64_t>(static_cast<uint64_t>(page) + 1, pages);
  const uint64_t scaled = (static_cast<uint64_t>(chapter) * pages + within) * 100 / (pages * chapterCount);
  return static_cast<uint8_t>(std::min<uint64_t>(scaled, 100));
}

size_t encodeProgress(const Progress& progress, uint8_t* out, const size_t cap) {
  const size_t total = PROGRESS_HEADER_BYTES + progress.file.size();
  if (progress.file.size() > 255 || total > cap) return 0;
  out[0] = PROGRESS_VERSION;
  putLe32(out + 1, progress.chapter);
  putLe32(out + 5, progress.page);
  out[9] = static_cast<uint8_t>(progress.file.size());
  memcpy(out + PROGRESS_HEADER_BYTES, progress.file.data(), progress.file.size());
  return total;
}

bool decodeProgress(const uint8_t* data, const size_t len, Progress& out) {
  if (len < PROGRESS_HEADER_BYTES || data[0] != PROGRESS_VERSION) return false;
  const size_t nameLen = data[9];
  if (len < PROGRESS_HEADER_BYTES + nameLen) return false;
  out.chapter = getLe32(data + 1);
  out.page = getLe32(data + 5);
  out.file = std::string_view(reinterpret_cast<const char*>(data + PROGRESS_HEADER_BYTES), nameLen);
  return true;
}

}  // namespace xtc::series
