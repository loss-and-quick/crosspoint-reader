#pragma once

#include <atomic>
#include <cstdint>

// Work in flight that must not be cut short by auto-sleep. Work holds a BusyGuard for exactly as long as it
// runs, so a forgotten "done" cannot leave the device awake and a forgotten "start" cannot be written.

class BusyCounter {
 public:
  void acquire() { count.fetch_add(1, std::memory_order_acq_rel); }
  void release() { count.fetch_sub(1, std::memory_order_acq_rel); }
  bool busy() const { return count.load(std::memory_order_acquire) != 0; }
  uint32_t holders() const { return count.load(std::memory_order_acquire); }

 private:
  std::atomic<uint32_t> count{0};
};

// Movable RAII holder. A default-constructed guard holds nothing; release() ends the hold early.
class BusyGuard {
 public:
  BusyGuard() = default;
  explicit BusyGuard(BusyCounter& counter) : counter(&counter) { counter.acquire(); }
  BusyGuard(BusyGuard&& other) noexcept : counter(other.counter) { other.counter = nullptr; }
  BusyGuard& operator=(BusyGuard&& other) noexcept {
    if (this != &other) {
      release();
      counter = other.counter;
      other.counter = nullptr;
    }
    return *this;
  }
  BusyGuard(const BusyGuard&) = delete;
  BusyGuard& operator=(const BusyGuard&) = delete;
  ~BusyGuard() { release(); }

  bool holding() const { return counter != nullptr; }
  void release() {
    if (counter) {
      counter->release();
      counter = nullptr;
    }
  }

 private:
  BusyCounter* counter = nullptr;
};

namespace idlepolicy {

// A loop() call that blocked this long was doing work, not waiting for input: the idle clock restarts
// when it returns, so the user gets a full timeout to see the result instead of an instant sleep.
constexpr unsigned long kBlockingLoopMs = 2000;

constexpr bool blockedLoopCountsAsActivity(const unsigned long loopMs) { return loopMs >= kBlockingLoopMs; }

enum class TimeoutAction : uint8_t {
  Sleep,
  Defer,  // work in flight: decide again on the next pass, change nothing now
};

// What to do once the idle timeout has elapsed. In-flight work and an exclusive storage owner (USB drive)
// win over sleeping.
constexpr TimeoutAction decideTimeout(const bool busy, const bool exclusiveStorage) {
  return (busy || exclusiveStorage) ? TimeoutAction::Defer : TimeoutAction::Sleep;
}

}  // namespace idlepolicy
