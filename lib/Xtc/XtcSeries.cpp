#include "XtcSeries.h"

#include <FsHelpers.h>
#include <Logging.h>

#include <cstring>
#include <functional>

#include "../Memory/Memory.h"

namespace series = xtc::series;

namespace {

constexpr size_t READ_CHUNK_BYTES = 512;

// Copies at most cap-1 bytes without splitting a UTF-8 sequence.
void copyTruncated(char* out, const size_t cap, std::string_view in) {
  size_t n = in.size() < cap - 1 ? in.size() : cap - 1;
  if (n < in.size()) {
    while (n > 0 && (static_cast<unsigned char>(in[n]) & 0xC0) == 0x80) --n;
  }
  memcpy(out, in.data(), n);
  out[n] = '\0';
}

struct IndexState {
  std::vector<uint32_t>* offsets;
  bool sawHeader = false;
  bool headerOk = false;
  uint32_t skipped = 0;
};

void onIndexLine(void* ctx, const uint32_t offset, const std::string_view line, bool /*truncated*/) {
  auto* state = static_cast<IndexState*>(ctx);
  if (!state->sawHeader) {
    state->sawHeader = true;
    state->headerOk = series::isHeaderLine(line);
    return;
  }
  if (!state->headerOk) return;
  series::EntryView entry;
  if (!series::parseEntry(line, entry)) {
    if (!line.empty() && line != "\r") ++state->skipped;
    return;
  }
  if (state->offsets->size() >= series::MAX_CHAPTERS) {
    ++state->skipped;
    return;
  }
  state->offsets->push_back(offset);
}

}  // namespace

XtcSeries::XtcSeries(std::string indexPath, const std::string& cacheDir)
    : indexPath(std::move(indexPath)), lineBuf{} {
  folder = FsHelpers::extractFolderPath(this->indexPath);
  const size_t slash = folder.find_last_of('/');
  title = slash == std::string::npos ? folder : folder.substr(slash + 1);
  cachePath = cachePathFor(this->indexPath, cacheDir);
  listPath = this->indexPath;
}

std::string XtcSeries::cachePathFor(const std::string& indexPath, const std::string& cacheDir) {
  return cacheDir + "/xtcs_" + std::to_string(std::hash<std::string>{}(indexPath));
}

void XtcSeries::setupCacheDir() const {
  if (!Storage.exists(cachePath.c_str())) Storage.mkdir(cachePath.c_str());
}

bool XtcSeries::indexList(HalFile& file, bool& headerOnly) {
  auto chunk = makeUniqueNoThrow<char[]>(READ_CHUNK_BYTES);
  if (!chunk) {
    LOG_ERR("XTS", "OOM: idx read chunk");
    return false;
  }

  lineOffsets.clear();
  // A chapter line is rarely shorter than ~24 bytes ("c1.xtch\t12\tChapter 1\n").
  size_t estimate = file.fileSize() / 24 + 1;
  if (estimate > series::MAX_CHAPTERS) estimate = series::MAX_CHAPTERS;
  lineOffsets.reserve(estimate);

  IndexState state{&lineOffsets};
  series::LineSplitter splitter(lineBuf, &onIndexLine, &state);
  for (;;) {
    const int n = file.read(chunk.get(), READ_CHUNK_BYTES);
    if (n <= 0) break;
    splitter.feed(chunk.get(), static_cast<size_t>(n));
    if (state.sawHeader && !state.headerOk) break;
  }
  splitter.finish();

  if (!state.headerOk) {
    LOG_ERR("XTS", "Not a series index: %s", listPath.c_str());
    return false;
  }
  if (state.skipped > 0) LOG_ERR("XTS", "Skipped %lu invalid idx lines", static_cast<unsigned long>(state.skipped));
  headerOnly = lineOffsets.empty();
  lineOffsets.shrink_to_fit();
  return true;
}

bool XtcSeries::writeNaturalOrderList() {
  auto dir = Storage.open(folder.empty() ? "/" : folder.c_str());
  if (!dir || !dir.isDirectory()) {
    LOG_ERR("XTS", "Cannot open series folder: %s", folder.c_str());
    return false;
  }

  size_t candidates = 0;
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) ++candidates;
  dir.rewindDirectory();
  if (candidates > series::MAX_FALLBACK_FILES) candidates = series::MAX_FALLBACK_FILES;

  // Names are needed together only to sort them; freed before reading starts.
  std::vector<std::string> names;
  names.reserve(candidates);
  for (auto file = dir.openNextFile(); file && names.size() < series::MAX_FALLBACK_FILES;
       file = dir.openNextFile()) {
    file.getName(lineBuf, sizeof(lineBuf));
    if (file.isDirectory() || lineBuf[0] == '.' || !FsHelpers::hasXtcExtension(std::string_view(lineBuf))) continue;
    names.emplace_back(lineBuf);
  }
  dir.close();
  series::sortNatural(names);

  setupCacheDir();
  const std::string generated = cachePath + "/order.idx";
  HalFile out;
  if (!Storage.openFileForWrite("XTS", generated, out)) return false;
  out.write(series::HEADER.data(), series::HEADER.size());
  out.write("\n", 1);
  for (const auto& name : names) {
    const size_t n = series::formatEntry(lineBuf, sizeof(lineBuf), name, 0, series::fileStem(name));
    if (n > 0 && out.write(lineBuf, n) != n) {
      LOG_ERR("XTS", "Short write: %s", generated.c_str());
      return false;
    }
  }
  listPath = generated;
  LOG_DBG("XTS", "Natural order list: %u files", static_cast<unsigned>(names.size()));
  return true;
}

bool XtcSeries::load() {
  listPath = indexPath;
  bool headerOnly = false;
  {
    HalFile file;
    if (!Storage.openFileForRead("XTS", indexPath, file) || !indexList(file, headerOnly)) return false;
  }

  if (headerOnly) {
    HalFile file;
    if (!writeNaturalOrderList() || !Storage.openFileForRead("XTS", listPath, file) ||
        !indexList(file, headerOnly)) {
      return false;
    }
  }

  LOG_DBG("XTS", "Loaded series %s: %lu chapters", title.c_str(), static_cast<unsigned long>(chapterCount()));
  return chapterCount() > 0;
}

bool XtcSeries::openList(HalFile& file) const { return Storage.openFileForRead("XTS", listPath, file); }

bool XtcSeries::readEntry(HalFile& list, const uint32_t index, Entry& out) const {
  if (index >= lineOffsets.size() || !list.seek(lineOffsets[index])) return false;
  const int n = list.read(lineBuf, sizeof(lineBuf));
  if (n <= 0) return false;
  std::string_view line(lineBuf, static_cast<size_t>(n));
  const size_t newline = line.find('\n');
  if (newline != std::string_view::npos) line = line.substr(0, newline);

  series::EntryView entry;
  if (!series::parseEntry(line, entry) || entry.file.size() >= sizeof(out.file)) return false;
  copyTruncated(out.file, sizeof(out.file), entry.file);
  copyTruncated(out.title, sizeof(out.title), entry.title.empty() ? series::fileStem(entry.file) : entry.title);
  out.pages = entry.pages;
  return true;
}

bool XtcSeries::readEntry(const uint32_t index, Entry& out) const {
  HalFile list;
  return openList(list) && readEntry(list, index, out);
}

std::string XtcSeries::chapterPath(const char* file) const {
  return folder == "/" ? "/" + std::string(file) : folder + "/" + file;
}

bool XtcSeries::isChapterAvailable(const uint32_t index) const {
  auto entry = makeUniqueNoThrow<Entry>();
  return entry && readEntry(index, *entry) && Storage.exists(chapterPath(entry->file).c_str());
}

bool XtcSeries::loadProgress(uint32_t& chapter, uint32_t& page) const {
  if (chapterCount() == 0) return false;
  char savedFile[FILE_NAME_BYTES];
  series::Progress progress;
  {
    HalFile f;
    if (!Storage.openFileForRead("XTS", cachePath + "/progress.bin", f)) return false;
    static_assert(sizeof(lineBuf) >= series::MAX_PROGRESS_BYTES, "progress must fit the line scratch");
    auto* data = reinterpret_cast<uint8_t*>(lineBuf);
    const int n = f.read(data, series::MAX_PROGRESS_BYTES);
    if (n <= 0 || !series::decodeProgress(data, static_cast<size_t>(n), progress)) return false;
    if (progress.file.size() >= sizeof(savedFile)) return false;
    copyTruncated(savedFile, sizeof(savedFile), progress.file);
  }

  auto entry = makeUniqueNoThrow<Entry>();
  HalFile list;
  if (!entry || !openList(list)) return false;
  page = progress.page;
  if (progress.chapter < chapterCount() && readEntry(list, progress.chapter, *entry) &&
      strcmp(entry->file, savedFile) == 0) {
    chapter = progress.chapter;
    return true;
  }
  // The idx was rewritten (chapters inserted or removed): find the file again.
  for (uint32_t i = 0; i < chapterCount(); ++i) {
    if (readEntry(list, i, *entry) && strcmp(entry->file, savedFile) == 0) {
      chapter = i;
      return true;
    }
  }
  chapter = progress.chapter < chapterCount() ? progress.chapter : chapterCount() - 1;
  page = 0;
  return true;
}
