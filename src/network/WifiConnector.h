#pragma once

#include <cstdint>

#include "WifiConnectPolicy.h"

// Joins Wi-Fi networks without any UI, polled from a loop (or blocking for headless callers). It owns the station
// setup, the join timeout, the scan and the order of saved networks (see wificonnect::SavedSequence); screens only
// render what it reports. Saved networks come from WIFI_STORE.
class WifiConnector {
 public:
  enum class State : uint8_t { Idle, Scanning, ScanDone, Joining, Connected, Failed };
  // What the last poll() finished; the state says what the connector does next.
  enum class Event : uint8_t { None, ScanDone, ScanFailed, Connected, JoinFailed };
  enum class Error : uint8_t { None, ConnectFailed, NetworkNotFound, Timeout };

  struct Options {
    uint32_t joinTimeoutMs = 15000;  // per network
    uint32_t scanTimeoutMs = 0;      // 0 = until the scan ends
  };

  // Number of saved networks. Nothing reads the credential store at boot, so the first call loads it from the
  // card; reload forces a fresh read. Takes the SD card: hold the render lock where a screen draws.
  static size_t savedCount(bool reload = false);

  // Saved networks: the last connected one first, then (after one scan) the other saved ones in range, strongest
  // first. False when no network is saved. The first join or scan has started when this returns true.
  bool beginSaved(const Options& options);
  // One network. An empty password joins an open network.
  void join(const char* ssid, const char* password, const uint32_t timeoutMs) {
    savedMode = false;
    startJoin(ssid, password, timeoutMs);
  }
  // Plain scan; poll() reports ScanDone and the screen reads the results from the driver, then calls finishScan().
  void startScan() {
    savedMode = false;
    beginScan(0);
  }

  Event poll();
  // Ends a scan reported by poll(): frees the driver's results and, in a saved-network sequence, starts the next
  // join.
  void finishScan();
  // Blocks until the saved-network sequence or the join ends, or limitMs (0 = no limit) passed and it was
  // stopped; true when connected.
  bool waitForConnection(uint32_t limitMs = 0);

  // Stops joining (and any saved-network sequence) and drops the link.
  void cancel();
  // Stops a running scan and frees its results in the driver.
  void cancelScan();
  // A running saved-network scan still finishes, but no network is joined after it.
  void stopSavedSequence() { savedMode = false; }

  State state() const { return currentState; }
  Error error() const { return lastError; }
  // The network being joined, or the one that connected.
  const char* ssid() const { return currentSsid; }
  // Records the connected network as the last connected one (writes the card when it changed).
  void rememberConnected() const;

 private:
  void startJoin(const char* ssid, const char* password, uint32_t timeoutMs);
  void advanceSaved();
  void beginScan(uint32_t timeoutMs);
  Event pollJoin();

  State currentState = State::Idle;
  Error lastError = Error::None;
  bool savedMode = false;
  Options options;
  wificonnect::SavedSequence sequence;
  char currentSsid[33] = {};
  uint32_t phaseStart = 0;
  uint32_t phaseTimeoutMs = 0;
};
