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
  xtc::reading::Placement pagePlacement(int pageHeight) const;
  void paintStatusBar(const xtc::reading::Placement& placement) const;
  ChapterPage chapterPage() const;
  void saveProgress() const;
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

  // The XTC reader has no toolbar content yet; it always opens the list menu.
  bool usesToolbarMenu() const override { return false; }

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
