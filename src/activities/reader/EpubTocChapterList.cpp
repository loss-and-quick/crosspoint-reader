#include "EpubTocChapterList.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>

#include "components/UIScale.h"

bool EpubTocChapterList::open() {
  // The reader underneath pins its page-render glyph arenas while this list is up. clearCache()
  // is heap-adaptive: below the retention floor it frees them (the next page render rebuilds
  // them at normal page-turn cost), giving this list room to keep every row's fallback glyphs
  // resident instead of re-reading them from SD on each repaint.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }
  return epub != nullptr;
}

int EpubTocChapterList::currentIndex() const {
  return initialTocIndex >= 0 && initialTocIndex < count() ? initialTocIndex : 0;
}

// Materializes the label window starting at `start` (clamped) and prewarms its glyphs.
void EpubTocChapterList::refreshWindow(const int start) {
  const int total = count();
  int clamped = start;
  if (clamped > total - TOC_WINDOW) clamped = total - TOC_WINDOW;
  if (clamped < 0) clamped = 0;
  if (clamped == windowStart) return;

  windowCount = total - clamped < TOC_WINDOW ? total - clamped : TOC_WINDOW;
  for (int i = 0; i < windowCount; i++) {
    const auto tocItem = epub->getTocItem(clamped + i);
    std::string indent(tocItem.level > 0 ? (tocItem.level - 1) * 2 : 0, ' ');
    windowLabels[i] = indent + tocItem.title;
  }
  windowStart = clamped;

  struct PrewarmCtx {
    const std::string* labels;
    int count;
  } prewarmCtx{windowLabels, windowCount};
  renderer.prewarmFallbackText(
      uiScaleSpec().bodyFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        const auto* c = static_cast<const PrewarmCtx*>(ctx);
        return i < static_cast<uint32_t>(c->count) ? c->labels[i].c_str() : nullptr;
      },
      &prewarmCtx, static_cast<uint32_t>(windowCount));
}

void EpubTocChapterList::fillRow(const int index, freeink::ui::ListItem& item) {
  if (index < windowStart || index >= windowStart + windowCount) refreshWindow(index);
  item.label = windowLabels[index - windowStart].c_str();
}

bool EpubTocChapterList::choose(const int index, ActivityResult& result) {
  const auto tocItem = epub->getTocItem(index);
  if (tocItem.spineIndex == -1) {
    // An entry that points nowhere closes the list without a choice.
    result.isCancelled = true;
    return true;
  }
  result.data = ChapterResult{tocItem.spineIndex, tocItem.anchor};
  return true;
}
