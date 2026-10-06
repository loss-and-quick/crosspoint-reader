#include "WifiConnectPolicy.h"

namespace wificonnect {

void SavedSequence::begin(const size_t savedCount, const size_t firstIndex) {
  count = std::min(savedCount, kMaxSaved);
  first = firstIndex;
  tried = 0;
  started = scanRequested = false;
  std::fill(signal, signal + kMaxSaved, INT32_MIN);  // not seen
}

SavedSequence::Next SavedSequence::next() {
  Next out;
  if (!started) {
    started = true;
    if (first < count) {
      tried |= 1u << first;
      out.step = Step::Join;
      out.index = first;
      return out;
    }
  }
  if (!scanRequested) {
    scanRequested = true;
    out.step = Step::Scan;
    return out;
  }
  bool found = false;
  for (size_t i = 0; i < count; i++) {
    if (signal[i] != INT32_MIN && (tried >> i & 1u) == 0 && (!found || signal[i] > signal[out.index])) {
      out.index = i;
      found = true;
    }
  }
  if (found) {
    tried |= 1u << out.index;
    out.step = Step::Join;
  }
  return out;  // Done when nothing in range is left
}

void SavedSequence::see(const size_t index, const int32_t rssi) {
  if (index < count) signal[index] = std::max(signal[index], rssi);
}

}  // namespace wificonnect
