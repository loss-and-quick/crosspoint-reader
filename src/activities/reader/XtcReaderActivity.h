#pragma once

#include <Xtc.h>
#include <Xtc/XtcReading.h>

#include <memory>
#include <string>

#include "ReaderActivity.h"

// Reader of XTC/XTCH pages. Everything that is not the page itself (status bar, menu,
// go-to-percent, auto turn, buttons) comes from ReaderActivity.
class XtcReaderActivity final : public ReaderActivity {
  std::shared_ptr<Xtc> xtc;
  uint32_t currentPage = 0;

  // Position of the page inside its chapter, for the status bar and the menu.
  struct ChapterPage {
    int page = 0;  // 1-based
    int pageCount = 0;
    const char* title = "";  // empty when the page is outside any chapter; points into the book
  };

  void renderPage();
  void renderErrorScreen(StrId message);
  xtc::reading::Placement pagePlacement(int pageHeight) const;
  void paintStatusBar(const xtc::reading::Placement& placement) const;
  void paintNightChrome(const xtc::reading::Placement& placement, int pageWidth, int pageHeight) const;
  ChapterPage chapterPage() const;
  void saveProgress() const;
  bool pageShown() const { return xtc && currentPage < xtc->getPageCount(); }
  void loadProgress();

  bool loadBook() override;
  std::string getBookTitle() const override { return xtc ? xtc->getTitle() : ""; }
  std::string getBookAuthor() const override { return xtc ? xtc->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return xtc ? xtc->getThumbBmpPath() : ""; }
  void renderBook() override;
  void applyInitialOrientation() override;

  int currentPercent() const override;
  void jumpToPercent(int percent) override;
  MenuState menuState() const override;
  ReaderMenuActivity::Capabilities menuCapabilities() const override;
  void openChapterList() override;

  // Toolbar menu content. The scrub and the Contents panel run over the chapters inside the file,
  // or (scrub only) over pages when the book has none; the Text tile is hidden.
  bool hasInnerChapters() const { return xtc && xtc->hasChapters() && !xtc->getChapters().empty(); }
  void toolbarInfo(ToolbarInfo& info) const override;
  int scrubCount() const override;
  int scrubPosition() const override;
  void scrubTo(int index) override;
  int contentsCount() const override;
  int contentsCurrentIndex() const override;
  std::string contentsRowText(int index) const override;
  void activateContentsItem(int index) override;

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("XtcReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~XtcReaderActivity() override = default;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;
  bool wantsInvertedDisplay(bool nightMode) const override;
  bool overlayFramePolarityFlipped() const override;

  ScreenshotInfo getScreenshotInfo() const override;
};
