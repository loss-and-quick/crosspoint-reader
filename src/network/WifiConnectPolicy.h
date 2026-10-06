#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Pure decision behind WifiConnector (no Arduino, no WiFi): which saved network to join next. Kept apart so the
// ordering is unit-tested on the host.
namespace wificonnect {

// Saved networks are addressed by their index in the credential store; the store keeps far fewer than this.
constexpr size_t kMaxSaved = 16;

// The order in which saved networks are tried: the last connected one, then (after one scan) the other saved
// networks in range, strongest first (equal signals: saved order). Each network is offered once.
class SavedSequence {
 public:
  enum class Step : uint8_t { Join, Scan, Done };
  struct Next {
    Step step = Step::Done;
    size_t index = 0;  // Join only: index of the saved network
  };

  // count saved networks; first is the index of the last connected one, or count (or more) when there is none.
  void begin(size_t count, size_t first);
  Next next();
  // Reports a signal the scan saw for saved network index (the strongest counts); other indexes are ignored.
  void see(size_t index, int32_t rssi);

 private:
  size_t count = 0;
  size_t first = 0;
  uint32_t tried = 0;
  bool started = false;
  bool scanRequested = false;
  int32_t signal[kMaxSaved] = {};  // strongest signal seen per saved network; INT32_MIN = not seen
};

}  // namespace wificonnect
