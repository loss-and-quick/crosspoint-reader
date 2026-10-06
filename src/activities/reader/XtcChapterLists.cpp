#include "XtcChapterLists.h"

#include <I18n.h>

#include <algorithm>

int XtcChapterList::indexForPage(Xtc& xtc, const uint32_t page) {
  const auto& chapters = xtc.getChapters();
  const auto chapter = std::find_if(chapters.begin(), chapters.end(), [page](const auto& candidate) {
    return page >= candidate.startPage && page <= candidate.endPage;
  });
  return chapter != chapters.end() ? static_cast<int>(chapter - chapters.begin()) : 0;
}

int XtcChapterList::currentIndex() const { return xtc ? indexForPage(*xtc, currentPage) : 0; }

void XtcChapterList::fillRow(const int index, freeink::ui::ListItem& item) {
  const auto& chapter = xtc->getChapters()[index];
  item.label = chapter.name.empty() ? tr(STR_UNNAMED) : chapter.name.c_str();
}

bool XtcChapterList::choose(const int index, ActivityResult& result) {
  result.data = PageResult{xtc->getChapters()[index].startPage};
  return true;
}
