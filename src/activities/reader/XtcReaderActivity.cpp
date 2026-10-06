#include "XtcReaderActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "ProgressFile.h"
#include "ReaderActivity.h"
#include "ReaderChapterListActivity.h"
#include "ReaderUtils.h"
#include "XtcChapterLists.h"
#include "components/UITheme.h"
#include "fontIds.h"

bool XtcReaderActivity::loadBook() {
  auto loadedXtc = makeUniqueNoThrow<Xtc>(bookPath, "/.crosspoint");
  if (!loadedXtc) {
    LOG_ERR("XTR", "Failed to allocate XTC object");
    return false;
  }
  if (!loadedXtc->load()) {
    LOG_ERR("XTR", "Failed to load XTC");
    return false;
  }
  xtc = std::move(loadedXtc);
  xtc->setupCacheDir();
  loadProgress();
  return true;
}

ReaderMenuActivity::Capabilities XtcReaderActivity::menuCapabilities() const {
  return {.chapters = xtc && xtc->hasChapters() && !xtc->getChapters().empty(), .readerSettings = true};
}

void XtcReaderActivity::openChapterList() {
  if (!menuCapabilities().chapters) return;
  auto source = makeUniqueNoThrow<XtcChapterList>(xtc, currentPage);
  auto list = source ? makeUniqueNoThrow<ReaderChapterListActivity>(renderer, mappedInput, std::move(source)) : nullptr;
  if (!list) {
    LOG_ERR("XTR", "OOM: chapter list");
    return;
  }
  startActivityForResult(std::move(list), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      openReaderMenu();
      return;
    }
    currentPage = std::get<PageResult>(result.data).page;
    requestUpdate();
  });
}

void XtcReaderActivity::applyInitialOrientation() { renderer.setOrientation(GfxRenderer::Orientation::Portrait); }

void XtcReaderActivity::renderBook() {
  if (!xtc) {
    return;
  }

  renderPage();
  takePendingScreenshot();
  saveProgress();
}

int XtcReaderActivity::currentPercent() const {
  return xtc ? xtc::reading::percentForPage(xtc->getPageCount(), currentPage) : 0;
}

void XtcReaderActivity::jumpToPercent(const int percent) {
  if (!xtc) return;
  {
    RenderLock lock(*this);
    currentPage = xtc::reading::pageForPercent(xtc->getPageCount(), percent);
  }
  requestUpdate();
}

XtcReaderActivity::ChapterPage XtcReaderActivity::chapterPage() const {
  ChapterPage info;
  info.page = static_cast<int>(currentPage) + 1;
  info.pageCount = static_cast<int>(xtc->getPageCount());
  if (!xtc->hasChapters()) return info;

  const auto& chapters = xtc->getChapters();
  const auto chapterIt = std::find_if(chapters.begin(), chapters.end(), [this](const xtc::ChapterInfo& chapter) {
    return currentPage >= chapter.startPage && currentPage <= chapter.endPage;
  });
  if (chapterIt == chapters.end() || chapterIt->endPage < chapterIt->startPage) return info;

  info.title = chapterIt->name.empty() ? tr(STR_UNNAMED) : chapterIt->name.c_str();
  info.page = static_cast<int>(currentPage - chapterIt->startPage) + 1;
  info.pageCount = static_cast<int>(chapterIt->endPage - chapterIt->startPage) + 1;
  return info;
}

ReaderActivity::MenuState XtcReaderActivity::menuState() const {
  if (!xtc) return {};
  const ChapterPage info = chapterPage();
  return {getBookTitle(), info.page, info.pageCount, currentPercent()};
}

void XtcReaderActivity::toolbarInfo(ToolbarInfo& info) const {
  if (!xtc) return;
  const ChapterPage page = chapterPage();
  const int percent = currentPercent();
  info.chapterTitle = page.title[0] != '\0' ? std::string(page.title) : getBookTitle();
  formatPageInfo(info, page.page, page.pageCount, percent);
  info.progressPermille = percent * 10;
}

int XtcReaderActivity::scrubCount() const {
  if (!xtc) return 0;
  return static_cast<int>(hasInnerChapters() ? xtc->getChapters().size() : xtc->getPageCount());
}

int XtcReaderActivity::scrubPosition() const {
  return hasInnerChapters() ? XtcChapterList::indexForPage(*xtc, currentPage) : static_cast<int>(currentPage);
}

void XtcReaderActivity::scrubTo(const int index) {
  RenderLock lock(*this);
  currentPage = hasInnerChapters() ? xtc->getChapters()[index].startPage : static_cast<uint32_t>(index);
}

int XtcReaderActivity::contentsCount() const {
  return hasInnerChapters() ? static_cast<int>(xtc->getChapters().size()) : 0;
}

int XtcReaderActivity::contentsCurrentIndex() const { return scrubPosition(); }

std::string XtcReaderActivity::contentsRowText(const int index) const {
  const auto& name = xtc->getChapters()[index].name;
  return name.empty() ? tr(STR_UNNAMED) : name;
}

void XtcReaderActivity::activateContentsItem(const int index) {
  RenderLock lock(*this);
  currentPage = xtc->getChapters()[index].startPage;
}

xtc::reading::Placement XtcReaderActivity::pagePlacement(const int pageHeight) const {
  using xtc::reading::BarPosition;
  BarPosition position = BarPosition::Hide;
  switch (SETTINGS.statusBarSpec().xtcMode) {
    case CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_BOTTOM:
      position = BarPosition::Bottom;
      break;
    case CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP:
      position = BarPosition::Top;
      break;
    default:
      break;
  }
  const int barHeight = UITheme::getInstance().getStatusBarHeight();
  if (barHeight <= 0) return {};
  int insetTop, insetRight, insetBottom, insetLeft;
  renderer.getOrientedViewableTRBL(&insetTop, &insetRight, &insetBottom, &insetLeft);
  return xtc::reading::planPlacement(pageHeight, renderer.getScreenHeight(), insetTop, insetBottom, barHeight + 4,
                                     position);
}

void XtcReaderActivity::paintStatusBar(const xtc::reading::Placement& placement) const {
  if (placement.stripH <= 0) return;
  renderer.fillRect(0, placement.stripY, renderer.getScreenWidth(), placement.stripH, false);

  const ChapterPage info = chapterPage();
  StatusBarContent content;
  const int bookPageCount = static_cast<int>(xtc->getPageCount());
  content.bookProgress = bookPageCount > 0 ? (static_cast<float>(currentPage + 1) * 100.0f) / bookPageCount : 0.0f;
  content.page = info.page;
  content.pageCount = info.pageCount;
  content.title = statusBarTitle(getBookTitle().c_str(), info.title);
  if (SETTINGS.statusBarSpec().xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP) {
    // The status bar draws bottom-up; lift it to the top strip.
    int insetTop, insetRight, insetBottom, insetLeft;
    renderer.getOrientedViewableTRBL(&insetTop, &insetRight, &insetBottom, &insetLeft);
    content.paddingBottom =
        renderer.getScreenHeight() - UITheme::getInstance().getStatusBarHeight() - insetBottom - insetTop - 4;
  }
  drawStatusBar(content);
}

// A page that cannot be shown still gets the reader's chrome: the status bar and the toolbar menu, so the
// reader can be left or moved on from the error.
void XtcReaderActivity::renderErrorScreen(const StrId message) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 300, I18N.get(message), true, EpdFontFamily::BOLD);
  paintStatusBar(pagePlacement(0));
  renderer.displayBuffer();
  paintOverlayOnPage();
}

void XtcReaderActivity::renderPage() {
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  size_t pageBufferSize;
  if (bitDepth == 2) {
    pageBufferSize = static_cast<size_t>(pageWidth) * ((static_cast<size_t>(pageHeight) + 7) / 8) * 2;
  } else {
    pageBufferSize = ((pageWidth + 7) / 8) * pageHeight;
  }

  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(pageBufferSize));
  if (!pageBuffer) {
    LOG_ERR("XTR", "Failed to allocate page buffer (%lu bytes)", pageBufferSize);
    renderErrorScreen(StrId::STR_MEMORY_ERROR);
    return;
  }

  size_t bytesRead = xtc->loadPage(currentPage, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    LOG_ERR("XTR", "Failed to load page %lu: bufferSize=%lu bitDepth=%u error=%s", currentPage, pageBufferSize,
            bitDepth, xtc::errorToString(xtc->getLastError()));
    free(pageBuffer);
    renderErrorScreen(StrId::STR_PAGE_LOAD_ERROR);
    return;
  }

  const auto placement = pagePlacement(pageHeight);
  const int pageY = placement.pageY;
  // A strip drawn over the page keeps the page's pixels out from under it, in every pass.
  const auto underStrip = [&](const uint16_t y) {
    return placement.overlay && pageY + y >= placement.stripY && pageY + y < placement.stripY + placement.stripH;
  };

  renderer.clearScreen();

  if (bitDepth == 2) {
    const size_t planeSize = static_cast<size_t>(pageWidth) * ((static_cast<size_t>(pageHeight) + 7) / 8);
    const uint8_t* plane1 = pageBuffer;
    const uint8_t* plane2 = pageBuffer + planeSize;
    const size_t colBytes = (pageHeight + 7) / 8;

    auto getPixelValue = [&](uint16_t x, uint16_t y) -> uint8_t {
      const size_t colIndex = pageWidth - 1 - x;
      const size_t byteInCol = y / 8;
      const size_t bitInByte = 7 - (y % 8);
      const size_t byteOffset = colIndex * colBytes + byteInCol;
      const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
      const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
      return (bit1 << 1) | bit2;
    };

    const auto paintBwPage = [&] {
      for (uint16_t y = 0; y < pageHeight; y++) {
        if (underStrip(y)) continue;
        for (uint16_t x = 0; x < pageWidth; x++) {
          if (getPixelValue(x, y) >= 1) {
            renderer.drawPixel(x, pageY + y, true);
          }
        }
      }
      paintStatusBar(placement);
    };
    paintBwPage();

    if (pagesUntilFullRefresh <= 1) {
      // Periodic ghost cleanup: scrub via the normal path, then run the
      // settle flavor of the grayscale base pass (DTM planes are equal after
      // the display sync, so only the gentle reinforcement cells fire).
      // Combined-base panels (Paper Mono) instead defer the base so the gray
      // planes below join it in one waveform.
      if (renderer.grayscaleCapabilities().base == HalDisplay::GrayscaleBase::Combined) {
        renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
      } else {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        renderer.preconditionGrayscale();
      }
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      pagesUntilFullRefresh--;
    }

    renderer.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      if (underStrip(y)) continue;
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) == 1) {
          renderer.drawPixel(x, pageY + y, false);
        }
      }
    }
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      if (underStrip(y)) continue;
      for (uint16_t x = 0; x < pageWidth; x++) {
        const uint8_t pv = getPixelValue(x, y);
        if (pv == 1 || pv == 2) {
          renderer.drawPixel(x, pageY + y, false);
        }
      }
    }
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();

    renderer.clearScreen();
    paintBwPage();

    renderer.cleanupGrayscaleWithFrameBuffer();

    free(pageBuffer);

    LOG_DBG("XTR", "Rendered page %lu/%lu (2-bit grayscale)", currentPage + 1, xtc->getPageCount());
    paintOverlayOnPage();
    markPageRendered();
    return;
  }

  const size_t srcRowBytes = (pageWidth + 7) / 8;
  for (uint16_t srcY = 0; srcY < pageHeight; srcY++) {
    const size_t srcRowStart = srcY * srcRowBytes;

    for (uint16_t srcX = 0; srcX < pageWidth; srcX++) {
      const size_t srcByte = srcRowStart + srcX / 8;
      const size_t srcBit = 7 - (srcX % 8);
      const bool isBlack = !((pageBuffer[srcByte] >> srcBit) & 1);

      if (isBlack) {
        renderer.drawPixel(srcX, pageY + srcY, true);
      }
    }
  }

  free(pageBuffer);

  paintStatusBar(placement);

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  LOG_DBG("XTR", "Rendered page %lu/%lu (%u-bit)", currentPage + 1, xtc->getPageCount(), bitDepth);
  paintOverlayOnPage();
  markPageRendered();
}
bool XtcReaderActivity::pageTurn(bool isForward) {
  if (!xtc) return false;
  if (isForward) {
    if (currentPage < xtc->getPageCount()) {
      currentPage++;
      return true;
    }
  } else {
    if (currentPage > 0) {
      currentPage--;
      return true;
    }
  }
  return false;
}

bool XtcReaderActivity::skipPages(int amount) {
  if (!xtc) return false;
  int newPage = static_cast<int>(currentPage) + amount;
  if (newPage < 0) newPage = 0;
  if (newPage > static_cast<int>(xtc->getPageCount())) newPage = static_cast<int>(xtc->getPageCount());
  if (newPage != static_cast<int>(currentPage)) {
    currentPage = static_cast<uint32_t>(newPage);
    return true;
  }
  return false;
}

bool XtcReaderActivity::isAtEndOfBook() const { return xtc && currentPage >= xtc->getPageCount(); }

void XtcReaderActivity::onReturnFromEndOfBook() {
  if (xtc && xtc->getPageCount() > 0) {
    currentPage = xtc->getPageCount() - 1;
  } else {
    currentPage = 0;
  }
}

void XtcReaderActivity::saveProgress() const {
  if (!xtc) return;
  uint8_t data[4];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = (currentPage >> 16) & 0xFF;
  data[3] = (currentPage >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(xtc->getCachePath(), data, sizeof(data))) {
    LOG_ERR("XTC", "Failed to save progress: page %lu", currentPage);
  }
}

void XtcReaderActivity::loadProgress() {
  if (!xtc) return;
  HalFile f;
  if (Storage.openFileForRead("XTC", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      if (currentPage >= xtc->getPageCount() && xtc->getPageCount() > 0) {
        currentPage = xtc->getPageCount() - 1;
      }
      LOG_DBG("XTC", "Loaded progress: page %lu/%lu", currentPage + 1, xtc->getPageCount());
    }
  }
}

ScreenshotInfo XtcReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Xtc;
  if (xtc) {
    const std::string t = xtc->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
    const uint32_t pageCount = xtc->getPageCount();
    info.totalPages = pageCount;
    uint32_t clampedPage = (pageCount > 0 && currentPage >= pageCount) ? pageCount - 1 : currentPage;
    info.progressPercent = pageCount > 0 ? xtc->calculateProgress(clampedPage) : 0;
    info.currentPage = static_cast<int>(clampedPage) + 1;
  } else {
    info.currentPage = currentPage + 1;
  }
  return info;
}
