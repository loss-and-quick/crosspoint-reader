/**
 * XtcSeries.h
 *
 * A folder of XTC/XTCH chapter files read as one book, described by its
 * `series.idx` (format in Xtc/XtcSeriesFormat.h).
 *
 * Only a table of idx line offsets (4 bytes per chapter) stays in RAM; chapter
 * names and titles are re-read from the idx on demand, so a 1000-chapter series
 * costs ~4 KB while it is open.
 */

#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Xtc/XtcSeriesFormat.h"

class XtcSeries {
 public:
  static constexpr size_t FILE_NAME_BYTES = 128;
  static constexpr size_t TITLE_BYTES = 128;

  struct Entry {
    char file[FILE_NAME_BYTES];
    char title[TITLE_BYTES];  // falls back to the file stem when the idx has none
    uint32_t pages;
  };

  // indexPath is the series.idx path; cache files go to <cacheDir>/xtcs_<hash>.
  XtcSeries(std::string indexPath, const std::string& cacheDir);
  static std::string cachePathFor(const std::string& indexPath, const std::string& cacheDir);

  // Validates the header and indexes chapter lines. A header-only idx lists the
  // folder's .xtc/.xtch files in natural name order instead.
  bool load();

  uint32_t chapterCount() const { return static_cast<uint32_t>(lineOffsets.size()); }
  const std::string& getTitle() const { return title; }
  const std::string& getCachePath() const { return cachePath; }

  // Reads chapter `index` from the list file; pass an open list file (see
  // openList) to avoid reopening it per call.
  bool readEntry(uint32_t index, Entry& out) const;
  bool readEntry(HalFile& list, uint32_t index, Entry& out) const;
  bool openList(HalFile& file) const;

  std::string chapterPath(const char* file) const;
  bool isChapterAvailable(uint32_t index) const;

  // Context for xtc::series::findAvailable/planTurn: keeps the list file open
  // across a scan over many missing chapters instead of reopening it per chapter.
  struct AvailabilityScan {
    explicit AvailabilityScan(const XtcSeries& series) : series(series) {}
    const XtcSeries& series;
    HalFile list;
    std::unique_ptr<Entry> entry;  // allocated on the first probe only
  };
  static bool isAvailable(void* scan, uint32_t index);

  // Progress (xtc::series::encodeProgress) lives in the cache dir, never in the
  // series folder, which another tool may own or rewrite.
  void setupCacheDir() const;
  // Returns false when there is no usable progress; chapter is re-resolved by file name.
  bool loadProgress(uint32_t& chapter, uint32_t& page) const;

 private:
  std::string indexPath;
  std::string folder;
  std::string cachePath;
  std::string listPath;  // indexPath, or the generated natural-order list
  std::string title;
  std::vector<uint32_t> lineOffsets;
  // Line scratch shared by load() and readEntry(); kept off the task stack.
  mutable char lineBuf[xtc::series::MAX_LINE_BYTES];

  bool indexList(HalFile& file, bool& headerOnly);
  bool writeNaturalOrderList();
};
