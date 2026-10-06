#pragma once

// Pure window math for a tab bar that scrolls when its tabs do not fit (see UiTabListActivity).
namespace tabwindow {

// Number of equal-width tabs that fit the bar (all of them when they do); at least two so the bar stays a bar.
constexpr int visibleTabCount(const int total, const int slotMin, const int width) {
  if (slotMin <= 0 || slotMin * total <= width) return total;
  const int fit = width / slotMin;
  return fit < 2 ? (total < 2 ? total : 2) : fit;
}

// Clamp the first visible tab of a window of `visible` tabs into [0, total - visible].
constexpr int clampFirst(const int first, const int visible, const int total) {
  const int maxFirst = total - visible;
  const int f = first > maxFirst ? maxFirst : first;
  return f < 0 ? 0 : f;
}

// First visible tab of a window that keeps `active` in view, moving as little as possible.
constexpr int scrollTabWindow(int first, const int visible, const int total, const int active) {
  if (active < first) first = active;
  if (active >= first + visible) first = active - visible + 1;
  return clampFirst(first, visible, total);
}

// Tabs to shift for a horizontal swipe of `dx` pixels over slots of `slotWidth`: the swipe distance in tab widths
// (rounded, at least one, at most one page). Swiping left (dx < 0) reveals later tabs, so the shift is positive.
constexpr int swipeShift(const int dx, const int slotWidth, const int visible) {
  if (dx == 0) return 0;
  const int dist = dx < 0 ? -dx : dx;
  int tabs = slotWidth > 0 ? (dist + slotWidth / 2) / slotWidth : 1;
  if (tabs < 1) tabs = 1;
  if (tabs > visible) tabs = visible;
  return dx < 0 ? tabs : -tabs;
}

// Window position after a manual swipe scroll, clamped; the selected tab is not involved.
constexpr int swipeScroll(const int first, const int dx, const int slotWidth, const int visible, const int total) {
  return clampFirst(first + swipeShift(dx, slotWidth, visible), visible, total);
}

}  // namespace tabwindow
