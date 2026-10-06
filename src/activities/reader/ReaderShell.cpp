// Reader shell: the part of ReaderActivity every reader shares. See ReaderActivity.h.
#include <I18n.h>
#include <Memory.h>

#include <iterator>

#include "CrossPointSettings.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "KOReaderCredentialStore.h"
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "util/ScreenshotUtil.h"

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
