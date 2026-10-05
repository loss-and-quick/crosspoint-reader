/**
 * XtcSeriesFormat.h
 *
 * Pure (HAL-free) helpers for reading a folder of XTC/XTCH files as one book.
 *
 * A series folder holds a `series.idx` text file:
 *
 *   XSERIES 1
 *   <file>\t<pages>\t<title>
 *   ...
 *
 * one chapter per line, in reading order. `<file>` is a bare file name inside
 * the series folder and may not exist yet (chapters not copied to the card);
 * readers skip such chapters. `<pages>` is informational (0 = unknown). An idx
 * with only the header line opts the folder into natural file-name order.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xtc::series {

constexpr std::string_view HEADER = "XSERIES 1";
// Longest idx line inspected; the tail of a longer line (title) is dropped.
constexpr size_t MAX_LINE_BYTES = 320;
// Chapters indexed per series; each costs 4 bytes of RAM while reading.
constexpr uint32_t MAX_CHAPTERS = 4096;
// Files collected by the natural-order fallback; names are held only while sorting.
constexpr size_t MAX_FALLBACK_FILES = 512;
// Serialized progress: version, chapter, page, file-name length, file name.
constexpr uint8_t PROGRESS_VERSION = 1;
constexpr size_t PROGRESS_HEADER_BYTES = 10;
constexpr size_t MAX_PROGRESS_BYTES = PROGRESS_HEADER_BYTES + 255;

struct EntryView {
  std::string_view file;
  uint32_t pages = 0;
  std::string_view title;
};

// True for the first idx line (tolerates a UTF-8 BOM and a trailing '\r').
bool isHeaderLine(std::string_view line);

// Parses one chapter line. Rejects blank lines, unsafe or non-XTC file names and
// non-numeric page counts. A missing title field yields an empty title.
bool parseEntry(std::string_view line, EntryView& out);

// "ch10.xtch" -> "ch10"
std::string_view fileStem(std::string_view file);

// Writes one "<file>\t<pages>\t<title>\n" line; returns bytes written, 0 if it does not fit.
size_t formatEntry(char* out, size_t cap, std::string_view file, uint32_t pages, std::string_view title);

// Numeric-aware, case-insensitive order ("ch2" before "ch10"), same as the file browser.
void sortNatural(std::vector<std::string>& files);

// Splits a byte stream into lines without holding more than MAX_LINE_BYTES of
// any line. Each completed line is reported with the stream offset of its first
// byte; `truncated` is set when bytes past the buffer were dropped.
class LineSplitter {
 public:
  using LineFn = void (*)(void* ctx, uint32_t offset, std::string_view line, bool truncated);

  // buf must hold at least MAX_LINE_BYTES bytes and outlive the splitter.
  LineSplitter(char* buf, LineFn fn, void* ctx) : buf(buf), fn(fn), ctx(ctx) {}

  void feed(const char* data, size_t len);
  // Reports a final line that has no trailing newline.
  void finish();

 private:
  char* buf;
  LineFn fn;
  void* ctx;
  uint32_t streamOffset = 0;
  uint32_t lineStart = 0;
  size_t lineLen = 0;
  bool truncated = false;

  void emit();
};

// Position-changing decision for a page turn inside a series.
struct Turn {
  enum class Kind : uint8_t {
    None,     // nothing to do (start of series)
    Page,     // stay in chapter, go to `page`
    Chapter,  // open `chapter`; first page, or last page when `toLastPage`
    End,      // past the last available chapter
  };
  Kind kind = Kind::None;
  uint32_t chapter = 0;
  uint32_t page = 0;
  bool toLastPage = false;
};

using AvailableFn = bool (*)(void* ctx, uint32_t chapter);

// Nearest chapter in direction `dir` (+1/-1) starting at `from` (inclusive) for
// which `available` holds; -1 when there is none.
int32_t findAvailable(int64_t from, int dir, uint32_t count, AvailableFn available, void* ctx);

Turn planTurn(uint32_t chapter, uint32_t page, uint32_t pageCount, bool forward, uint32_t chapterCount,
              AvailableFn available, void* ctx);

// Whole-series percentage, weighting every chapter equally (page counts of
// unopened chapters are unknown without opening them).
uint8_t seriesPercent(uint32_t chapter, uint32_t chapterCount, uint32_t page, uint32_t pageCount);

struct Progress {
  uint32_t chapter = 0;
  uint32_t page = 0;
  std::string_view file;  // chapter file name, used to re-find the chapter if the idx changed
};

// Returns bytes written (0 when out is too small or the name exceeds 255 bytes).
size_t encodeProgress(const Progress& progress, uint8_t* out, size_t cap);
// `out.file` views into `data`.
bool decodeProgress(const uint8_t* data, size_t len, Progress& out);

}  // namespace xtc::series
