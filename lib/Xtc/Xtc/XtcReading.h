/**
 * XtcReading.h
 *
 * Pure (HAL-free) decisions of the XTC/XTCH reader, kept apart from the activity so
 * they can be tested on the host: night-mode polarity, 2-bit level mirroring, where the page and the status-bar strip
 * sit, and go-to-percent mapping.
 */

#pragma once

#include <algorithm>
#include <cstdint>

namespace xtc::reading {

// XTH pixel levels: 0 = white, 1 = dark grey, 2 = light grey, 3 = black.
// The negative of a level is its mirror in darkness: white<->black, dark<->light.
constexpr uint8_t mirrorLevel(const uint8_t level) { return static_cast<uint8_t>(3 - (level & 3)); }

// How one page is put on the panel for the current night-mode state.
//
// The panel's night-mode polarity inverts the whole frame at output and skips the
// grayscale pass, so a 2-bit page cannot go through it without turning into a
// posterized black-and-white negative. 2-bit pages therefore keep the panel in
// normal polarity (grays work) and the reader paints the night look itself:
// mirrored levels for the page when comics are inverted, and an inverted chrome
// (status bar strip, margins). 1-bit pages use the panel polarity as everything
// else does, and counter-invert the page when it should keep its original look.
struct Polarity {
  bool displayInverted = false;    // panel output polarity while this page is shown
  bool mirrorLevels = false;       // draw 2-bit levels as 3 - level (a real negative with 4 grays)
  bool counterInvertPage = false;  // 1-bit: flip the page region back so the panel inversion cancels
  bool invertChrome = false;       // 2-bit: flip non-page areas (status bar, margins) to the night look
};

constexpr Polarity planPolarity(const bool nightMode, const bool invertComics, const uint8_t bitDepth,
                                const bool pageShown) {
  Polarity p;
  if (!nightMode) return p;
  if (pageShown && bitDepth == 2) {
    p.mirrorLevels = invertComics;
    p.invertChrome = true;
    return p;
  }
  p.displayInverted = true;
  p.counterInvertPage = pageShown && !invertComics;
  return p;
}

enum class BarPosition : uint8_t { Hide, Bottom, Top };

// Vertical layout of a page and its status-bar strip on a screen.
struct Placement {
  int pageY = 0;         // top edge of the page
  int stripY = 0;        // top edge of the strip (valid when stripH > 0)
  int stripH = 0;        // 0 when no status bar is drawn
  bool overlay = false;  // the strip covers part of the page
};

// A page that fits next to the strip is never scaled or covered: it sits at the top (bar at
// the bottom) or below the strip (bar at the top). A taller page keeps its place and the
// strip is drawn over it. `insetTop`/`insetBottom` are the unusable bezel rows.
constexpr Placement planPlacement(const int pageHeight, const int screenHeight, const int insetTop,
                                  const int insetBottom, const int stripHeight, const BarPosition position) {
  Placement p;
  if (position == BarPosition::Hide || stripHeight <= 0) return p;
  p.stripH = stripHeight;
  if (position == BarPosition::Bottom) {
    p.stripY = std::max(0, screenHeight - insetBottom - stripHeight);
    p.overlay = pageHeight > p.stripY;
    return p;
  }
  p.stripY = insetTop;
  const int belowStrip = insetTop + stripHeight;
  p.overlay = belowStrip + pageHeight > screenHeight;
  p.pageY = p.overlay ? 0 : belowStrip;
  return p;
}

// Page index for a go-to-percent target; inverse of the status bar's (page + 1) * 100 / count.
constexpr uint32_t pageForPercent(const uint32_t pageCount, const int percent) {
  if (pageCount == 0) return 0;
  const uint64_t p = static_cast<uint64_t>(std::clamp(percent, 0, 100));
  const uint64_t pages = (p * pageCount + 99) / 100;  // ceil
  return static_cast<uint32_t>(pages == 0 ? 0 : std::min<uint64_t>(pages - 1, pageCount - 1));
}

// Percent shown for page `page` of `pageCount`.
constexpr int percentForPage(const uint32_t pageCount, const uint32_t page) {
  if (pageCount == 0) return 0;
  const uint64_t within = std::min<uint64_t>(static_cast<uint64_t>(page) + 1, pageCount);
  return static_cast<int>(within * 100 / pageCount);
}

}  // namespace xtc::reading
