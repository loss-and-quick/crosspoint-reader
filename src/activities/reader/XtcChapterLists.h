#pragma once

#include <Xtc.h>

#include <memory>

#include "ReaderChapterListActivity.h"

// Chapters stored inside one XTC file; choosing one jumps to its first page.
class XtcChapterList final : public ChapterListSource {
  std::shared_ptr<Xtc> xtc;
  uint32_t currentPage;

 public:
  XtcChapterList(std::shared_ptr<Xtc> xtc, const uint32_t currentPage)
      : xtc(std::move(xtc)), currentPage(currentPage) {}

  // Index of the chapter holding `page`; 0 when none does.
  static int indexForPage(Xtc& xtc, uint32_t page);
  int count() const override { return xtc ? static_cast<int>(xtc->getChapters().size()) : 0; }
  int currentIndex() const override;
  void fillRow(int index, freeink::ui::ListItem& item) override;
  bool choose(int index, ActivityResult& result) override;
};
