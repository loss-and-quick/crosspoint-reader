#pragma once

#include <memory>

#include "activities/ActivityResult.h"
#include "activities/UiListActivity.h"

class GfxRenderer;

// Where a chapter list gets its rows from (an EPUB table of contents, for one). The list screen is the
// same whatever the source.
class ChapterListSource {
 public:
  virtual ~ChapterListSource() = default;
  // Called when the list opens; false leaves the list empty.
  virtual bool open() { return true; }
  virtual void close() {}
  virtual int count() const = 0;
  // Row selected when the list opens.
  virtual int currentIndex() const = 0;
  // Fills row `index`. Strings written into `item` need to stay valid only until the next call.
  virtual void fillRow(int index, freeink::ui::ListItem& item) = 0;
  // Result for choosing row `index`; false when the row cannot be chosen (the list stays open
  // unless `result.isCancelled` is set, which closes it as cancelled).
  virtual bool choose(int index, ActivityResult& result) = 0;
};

// The chapter list screen shared by every reader.
class ReaderChapterListActivity final : public UiListActivity {
  std::unique_ptr<ChapterListSource> source;
  bool opened = false;

  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);
  int listCount() const override { return opened ? source->count() : 0; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  void drawChrome() override;

 public:
  ReaderChapterListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            std::unique_ptr<ChapterListSource> source);
  void onEnter() override;
  void onExit() override;
};
