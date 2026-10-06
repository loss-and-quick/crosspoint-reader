#include "ReaderChapterListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

ReaderChapterListActivity::ReaderChapterListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     std::unique_ptr<ChapterListSource> source)
    : UiListActivity("ReaderChapterList", renderer, mappedInput), source(std::move(source)) {}

void ReaderChapterListActivity::onEnter() {
  UiListActivity::onEnter();
  opened = source && source->open();
  // The first screen build pulls the viewport to the selection (ListNav follow-on-build).
  if (opened) nav.selected = source->currentIndex();
}

void ReaderChapterListActivity::onExit() {
  if (source) source->close();
  opened = false;
  UiListActivity::onExit();
}

void ReaderChapterListActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  item.actionValue = static_cast<int16_t>(index);
  static_cast<ReaderChapterListActivity*>(ctx)->source->fillRow(index, item);
}

void ReaderChapterListActivity::activateIndex(const int index) {
  if (!opened || index < 0 || index >= source->count()) return;
  ActivityResult result;
  if (!source->choose(index, result)) return;
  // The activated row leaves this screen (finish); a lingering flash would gray an unrelated
  // element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  setResult(std::move(result));
  finish();
}

bool ReaderChapterListActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }
  if (!opened) return true;  // nothing else to route this pass
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }
  return false;
}

void ReaderChapterListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band drawChrome paints the title in.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (!opened) return;
  if (source->count() == 0) {
    screen.centeredText(tr(STR_NO_CHAPTERS), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.rowProvider = &ReaderChapterListActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(source->count());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

// Header is drawn inside the safe area (not full-width like the base).
void ReaderChapterListActivity::drawChrome() {
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), tr(STR_SELECT_CHAPTER));
}
