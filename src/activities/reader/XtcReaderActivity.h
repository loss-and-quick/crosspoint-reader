#pragma once

#include <Xtc.h>
#include <XtcSeries.h>

#include <memory>
#include <string>

#include "ReaderActivity.h"

class XtcReaderActivity final : public ReaderActivity {
  std::shared_ptr<Xtc> xtc;
  uint32_t currentPage = 0;

  // Series mode (bookPath is a series.idx): `xtc` holds only the current
  // chapter file, reopened at chapter boundaries.
  std::shared_ptr<XtcSeries> series;
  std::unique_ptr<XtcSeries::Entry> seriesEntry;  // current chapter's file and title
  uint32_t seriesChapter = 0;

  enum class StatusBarOverlayPosition { Bottom, Top };
  struct StatusBarInfo {
    int currentPage;
    int pageCount;
    std::string title;
  };

  void renderPage();
  void openChapterSelection();
  void renderStatusBarOverlay(GfxRenderer& renderer, StatusBarOverlayPosition position) const;
  StatusBarInfo getStatusBarInfo() const;
  void saveProgress() const;
  void loadProgress();
  bool loadSeries();
  bool openSeriesChapter(uint32_t chapter, bool atLastPage);
  bool changeSeriesChapter(uint32_t chapter, bool atLastPage);
  bool seriesPageTurn(bool isForward);

  bool loadBook() override;
  std::string getBookTitle() const override;
  std::string getBookAuthor() const override { return xtc && !series ? xtc->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return xtc && !series ? xtc->getThumbBmpPath() : ""; }
  std::string getEndOfBookAnchorPath() const override;
  bool handleFormatInput() override;
  void renderBook() override;
  void applyInitialOrientation() override;

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("XtcReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~XtcReaderActivity() override = default;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
