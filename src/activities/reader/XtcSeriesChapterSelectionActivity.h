#pragma once
#include <HalStorage.h>
#include <XtcSeries.h>

#include <memory>

#include "activities/UiListActivity.h"

// Chapter list of an XTC series. Rows are read from the series index on demand
// (rowProvider), so the list costs one Entry of RAM regardless of chapter count.
// Chapters whose file is not on the card are shown disabled.
class XtcSeriesChapterSelectionActivity final : public UiListActivity {
  std::shared_ptr<XtcSeries> series;
  uint32_t currentChapter;
  // Index file held open while the list is shown; closed in onExit().
  HalFile list;
  XtcSeries::Entry rowEntry{};
  char rowValueBuf[12]{};
  char rowTitleBuf[128]{};

  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);
  int listCount() const override { return series ? static_cast<int>(series->chapterCount()) : 0; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  void drawChrome() override;

 public:
  explicit XtcSeriesChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::shared_ptr<XtcSeries> series, uint32_t currentChapter);
  void onEnter() override;
  void onExit() override;
};
