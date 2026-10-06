// Reader shell: the part of ReaderActivity every reader shares. See ReaderActivity.h.
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "KOReaderCredentialStore.h"
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "activities/settings/StatusBarSettingsActivity.h"
#include "components/UITheme.h"
#include "util/ScreenshotUtil.h"

void ReaderActivity::drawStatusBar(const StatusBarContent& content) const {
  GUI.drawStatusBar(renderer, content.bookProgress, content.page, content.pageCount, content.title,
                    content.paddingBottom, content.textYOffset, content.fillMargin, content.bookmarked,
                    content.pageCountEstimated);
}

std::string ReaderActivity::statusBarTitle(const char* bookTitle, const char* chapterTitle) const {
  if (autoTurnActive) {
    char text[48];
    snprintf(text, sizeof(text), "%s%lu", tr(STR_AUTO_TURN_ENABLED), 60UL * 1000UL / autoTurnIntervalMs);
    return text;
  }
  switch (SETTINGS.statusBarSpec().titleMode) {
    case CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE:
      return chapterTitle;  // a page outside any chapter leaves the slot empty
    case CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE:
      return bookTitle;
    default:
      return {};
  }
}

void ReaderActivity::openGoToPercent(const int initialPercent) {
  auto dialog = makeUniqueNoThrow<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent);
  if (!dialog) {
    LOG_ERR("READER", "OOM: go-to-percent");
    return;
  }
  startActivityForResult(std::move(dialog), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      openReaderMenu();
    } else {
      jumpToPercent(std::get<PercentResult>(result.data).percent);
    }
  });
}

void ReaderActivity::requestScreenshot() {
  {
    RenderLock lock(*this);
    pendingScreenshot = true;
  }
  requestUpdate();
}

void ReaderActivity::takePendingScreenshot() {
  if (!pendingScreenshot) return;
  pendingScreenshot = false;
  ScreenshotUtil::takeScreenshot(renderer);
}

void ReaderActivity::setAutoPageTurn(const uint8_t option) {
  autoTurnOption = option;
  if (option == 0 || option >= std::size(ReaderUtils::PAGE_TURN_RATES)) {
    autoTurnActive = false;
    return;
  }
  autoTurnIntervalMs = 60UL * 1000UL / ReaderUtils::PAGE_TURN_RATES[option];
  lastPageTurnTime = millis();
  autoTurnActive = true;
  onAutoTurnStarted();
}

bool ReaderActivity::tickAutoPageTurn() {
  if (!autoTurnActive) return false;
  const bool touchStops = ReaderUtils::isTouchMenuGesture(renderer, mappedInput);
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back) || touchStops) {
    autoTurnActive = false;
    if (touchStops) haptic_feedback::touchAction();
    requestUpdate();
    return true;
  }
  if (!pageReady()) {
    requestUpdate();
    return true;
  }
  if (RenderLock::peek()) {
    lastPageTurnTime = millis();
    return true;
  }
  if (millis() - lastPageTurnTime < autoTurnIntervalMs) return false;
  const bool succeeded = pageTurn(true);
  if (succeeded) lastPageTurnTime = millis();
  notePageTurn(true, succeeded);
  requestUpdate();
  return true;
}

unsigned long ReaderActivity::confirmLongPressThreshold(const ReaderMenuActivity::Capabilities& caps) const {
  switch (SETTINGS.longPressMenuFunction) {
    case CrossPointSettings::LP_MENU_BOOKMARK:
      return caps.toggleBookmark ? ReaderUtils::BOOKMARK_HOLD_MS : 0;
    case CrossPointSettings::LP_MENU_DICTIONARY:
      return caps.lookup ? ReaderUtils::BOOKMARK_HOLD_MS : 0;
    case CrossPointSettings::LP_MENU_KOSYNC:
      return caps.sync && KOREADER_STORE.hasCredentials() ? ReaderUtils::GO_HOME_MS : 0;
    case CrossPointSettings::LP_MENU_READER_MENU:
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return 0;
  }
}

// Opens the reader menu the user picked: the toolbar over the page when the reader has one on
// screen, else the list menu.
void ReaderActivity::openMenu() {
  pendingManualTurn = 0;
  if (usesToolbarMenu() && pageReady()) {
    openOverlay(Overlay::Toolbar);
    return;
  }
  openReaderMenu();
}

void ReaderActivity::openReaderMenu() {
  pendingManualTurn = 0;
  if (usesToolbarMenu()) {
    // Reached from a child screen's result handler (go-to-percent, status bar settings... cancelled
    // back to the menu), so the framebuffer holds that screen, not the page: re-render the page
    // and let the reader put the toolbar on top. The in-reader fast path is openOverlay().
    overlay = Overlay::Toolbar;
    focusedTool = 0;
    panelHoldJumped = false;
    panelCursorShown = !mappedInput.hasTouch();
    if (!toolbarUi) toolbarUi = std::make_unique<ReaderToolbarUi>(renderer);
    toolbarUi->begin();
    discardOverlayPage();
    requestUpdate();
    return;
  }
  const MenuState state = menuState();
  auto menu = makeUniqueNoThrow<ReaderMenuActivity>(renderer, mappedInput, state.title, state.page, state.pageCount,
                                                    state.percent, SETTINGS.orientation, menuCapabilities());
  if (!menu) {
    LOG_ERR("READER", "OOM: reader menu");
    return;
  }
  startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
    const auto& menuResult = std::get<MenuResult>(result.data);
    if (menuCapabilities().rotate && SETTINGS.orientation != menuResult.orientation) {
      rotateTo(menuResult.orientation);
    }
    setAutoPageTurn(menuResult.pageTurnOption);
    if (!result.isCancelled) runMenuAction(static_cast<ReaderMenuActivity::MenuAction>(menuResult.action));
  });
}

// A menu row chosen from the list menu or the toolbar's More panel.
void ReaderActivity::runMenuAction(const ReaderMenuActivity::MenuAction action) {
  using Action = ReaderMenuActivity::MenuAction;
  switch (action) {
    case Action::SELECT_CHAPTER:
      openChapterList();
      break;
    case Action::GO_TO_PERCENT:
      openGoToPercent(currentPercent());
      break;
    case Action::GO_HOME:
      onGoHome();
      break;
    case Action::SCREENSHOT:
      requestScreenshot();
      break;
    case Action::READER_SETTINGS:
      if (auto settings = makeUniqueNoThrow<StatusBarSettingsActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(settings), [this](const ActivityResult&) { openReaderMenu(); });
      }
      break;
    case Action::NIGHT_MODE:
    case Action::FRONTLIGHT:
      // Toggled in place by the menu that offers them.
      break;
    default:
      onReaderMenuAction(action);
      break;
  }
}
