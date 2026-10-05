#include "XtcSeriesChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

XtcSeriesChapterSelectionActivity::XtcSeriesChapterSelectionActivity(GfxRenderer& renderer,
                                                                     MappedInputManager& mappedInput,
                                                                     std::shared_ptr<XtcSeries> series,
                                                                     const uint32_t currentChapter)
    : UiListActivity("XtcSeriesChapterSelection", renderer, mappedInput),
      series(std::move(series)),
      currentChapter(currentChapter) {}

void XtcSeriesChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();
  if (!series || !series->openList(list)) {
    series.reset();
    return;
  }
  nav.selected = static_cast<int>(currentChapter);
}

void XtcSeriesChapterSelectionActivity::onExit() {
  list.close();
  UiListActivity::onExit();
}

void XtcSeriesChapterSelectionActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<XtcSeriesChapterSelectionActivity*>(ctx);
  item.actionValue = static_cast<int16_t>(index);
  if (!self->series->readEntry(self->list, index, self->rowEntry)) {
    item.label = tr(STR_UNNAMED);
    item.enabled = false;
    return;
  }
  snprintf(self->rowTitleBuf, sizeof(self->rowTitleBuf), "%u. %s", static_cast<unsigned int>(index + 1),
           self->rowEntry.title[0] != '\0' ? self->rowEntry.title : tr(STR_UNNAMED));
  item.label = self->rowTitleBuf;
  if (!Storage.exists(self->series->chapterPath(self->rowEntry.file).c_str())) {
    item.enabled = false;
    item.value = tr(STR_FILE_MISSING);
  } else if (self->rowEntry.pages > 0) {
    snprintf(self->rowValueBuf, sizeof(self->rowValueBuf), "%lu", static_cast<unsigned long>(self->rowEntry.pages));
    item.value = self->rowValueBuf;
  }
}

void XtcSeriesChapterSelectionActivity::activateIndex(const int index) {
  if (!series || index < 0 || index >= listCount() || !series->isChapterAvailable(static_cast<uint32_t>(index))) {
    return;
  }
  app.clearTapFlash();
  nav.selected = index;
  setResult(PageResult{static_cast<uint32_t>(index)});
  finish();
}

bool XtcSeriesChapterSelectionActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }
  if (!series) {
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }
  return false;
}

void XtcSeriesChapterSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (!series || series->chapterCount() == 0) {
    screen.centeredText(tr(STR_NO_CHAPTERS), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.rowProvider = &XtcSeriesChapterSelectionActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(series->chapterCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void XtcSeriesChapterSelectionActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const char* title = tr(STR_SELECT_CHAPTER);
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD);
  const int titleX = safe.x + (safe.width - titleWidth) / 2;
  const int titleY = safe.y + metrics.topPadding + (metrics.headerHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, titleY, title, true, EpdFontFamily::BOLD);
}
