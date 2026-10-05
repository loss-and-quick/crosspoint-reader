#pragma once

#include <string>
#include <string_view>

// std::string-only subset used by LibraryBuilder, plus declarations of the real
// FsHelpers.cpp functions (linked in) that XtcSeriesFormat needs.
namespace FsHelpers {
inline bool checkFileExtension(const std::string& path, const char* extension) { return path.ends_with(extension); }
inline bool hasEpubExtension(const std::string& path) { return checkFileExtension(path, ".epub"); }

bool isSafePathComponent(std::string_view name);
bool hasBmpExtension(std::string_view fileName);
bool hasXtcExtension(std::string_view fileName);
bool naturalLess(const std::string& str1, const std::string& str2);
}  // namespace FsHelpers
