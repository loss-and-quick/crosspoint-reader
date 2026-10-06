#pragma once

#include <Epub.h>

#include <memory>
#include <string>

#include "ReaderChapterListActivity.h"

// Table of contents of an EPUB. TOC entries are SD-backed (BookMetadataCache LUT reads), so only
// a window of rows around the viewport is materialized: a several-hundred-entry TOC built up
// front cost ~60KB of labels and ListItems for rows that were never drawn. Refreshing the window
// also batch-prewarms its fallback glyphs, so each page of the list pays one bounded SD pass and
// repaints stay RAM-only.
class EpubTocChapterList final : public ChapterListSource {
  static constexpr int TOC_WINDOW = 24;

  GfxRenderer& renderer;
  std::shared_ptr<Epub> epub;
  int initialTocIndex;
  std::string windowLabels[TOC_WINDOW];
  int windowStart = -1;
  int windowCount = 0;

  void refreshWindow(int start);

 public:
  EpubTocChapterList(GfxRenderer& renderer, std::shared_ptr<Epub> epub, int initialTocIndex)
      : renderer(renderer), epub(std::move(epub)), initialTocIndex(initialTocIndex) {}

  bool open() override;
  int count() const override { return epub ? epub->getTocItemsCount() : 0; }
  int currentIndex() const override;
  void fillRow(int index, freeink::ui::ListItem& item) override;
  bool choose(int index, ActivityResult& result) override;
};
