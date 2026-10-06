#include "WifiConnector.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <algorithm>

#include "WifiCredentialStore.h"

namespace {
constexpr const char* LOG_TAG = "WIFI";
}  // namespace

size_t WifiConnector::savedCount(const bool reload) {
  static bool loaded = false;
  if (reload || !loaded) {
    loaded = true;
    WIFI_STORE.loadFromFile();
  }
  return WIFI_STORE.getCredentialCount();
}

bool WifiConnector::beginSaved(const Options& opts) {
  const size_t count = savedCount();
  cancel();
  if (count == 0) return false;
  options = opts;
  savedMode = true;
  sequence.begin(count, WIFI_STORE.lastConnectedIndex());
  advanceSaved();
  return true;
}

void WifiConnector::beginScan(const uint32_t timeoutMs) {
  currentState = State::Scanning;
  phaseStart = millis();
  phaseTimeoutMs = timeoutMs;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.scanNetworks(true);  // true = async scan; poll() reports when it ends
}

void WifiConnector::advanceSaved() {
  for (;;) {
    const auto next = sequence.next();
    if (next.step == wificonnect::SavedSequence::Step::Scan) {
      beginScan(options.scanTimeoutMs);
      return;
    }
    if (next.step == wificonnect::SavedSequence::Step::Done) {
      currentState = State::Failed;
      return;
    }
    const auto cred = WIFI_STORE.getCredentialAt(next.index);
    if (!cred) continue;
    startJoin(cred->ssid.c_str(), cred->password.c_str(), options.joinTimeoutMs);
    return;
  }
}

void WifiConnector::startJoin(const char* ssid, const char* password, const uint32_t timeoutMs) {
  strlcpy(currentSsid, ssid, sizeof(currentSsid));
  lastError = Error::None;
  currentState = State::Joining;
  phaseStart = millis();
  phaseTimeoutMs = timeoutMs;

  WiFi.persistent(false);  // Credentials are managed by WifiCredentialStore; suppress SDK NVS auto-connect
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);  // Abort any in-progress SDK auto-connect and clear NVS-saved SSID
  delay(100);

  // Scan all channels so networks with multiple APs use the strongest matching
  // BSSID instead of the first match found by the framework's default fast scan.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  // Set hostname so routers show "CrossPoint-Reader-AABBCCDDEEFF" instead of "esp32-XXXXXXXXXXXX"
  uint8_t mac[6] = {};
  const esp_err_t macResult = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (macResult == ESP_OK) {
    char hostname[sizeof("CrossPoint-Reader-") + 12];
    snprintf(hostname, sizeof(hostname), "CrossPoint-Reader-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    WiFi.setHostname(hostname);
  } else {
    LOG_ERR(LOG_TAG, "Failed to read station MAC for hostname (err=%d)", static_cast<int>(macResult));
  }

  if (*password == '\0') {
    WiFi.begin(ssid);
  } else {
    WiFi.begin(ssid, password);
  }
}

WifiConnector::Event WifiConnector::poll() {
  if (currentState == State::Joining) return pollJoin();
  if (currentState != State::Scanning) return Event::None;
  int16_t found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) {
    if (phaseTimeoutMs == 0 || millis() - phaseStart < phaseTimeoutMs) return Event::None;
    found = WIFI_SCAN_FAILED;
  }

  // Strongest signal of each saved network; a screen reads the full list from the driver before finishScan().
  for (int j = 0; savedMode && j < found; j++) {
    sequence.see(WIFI_STORE.indexOf(WiFi.SSID(j).c_str()), WiFi.RSSI(j));
  }
  currentState = State::ScanDone;
  return found >= 0 ? Event::ScanDone : Event::ScanFailed;
}

void WifiConnector::finishScan() {
  if (currentState != State::ScanDone) return;
  WiFi.scanDelete();
  if (savedMode) {
    advanceSaved();
  } else {
    currentState = State::Idle;
  }
}

WifiConnector::Event WifiConnector::pollJoin() {
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    currentState = State::Connected;
    return Event::Connected;
  }
  Error error = Error::ConnectFailed;
  if (status == WL_NO_SSID_AVAIL) {
    error = Error::NetworkNotFound;
  } else if (status != WL_CONNECT_FAILED) {
    if (millis() - phaseStart <= phaseTimeoutMs) return Event::None;
    error = Error::Timeout;
  }
  if (savedMode || error == Error::Timeout) WiFi.disconnect();
  if (savedMode) {
    advanceSaved();
  } else {
    currentState = State::Failed;
  }
  lastError = error;  // advanceSaved() resets it for a new join
  return Event::JoinFailed;
}

bool WifiConnector::waitForConnection(const uint32_t limitMs) {
  const uint32_t start = millis();
  while (currentState == State::Scanning || currentState == State::Joining) {
    if (limitMs != 0 && millis() - start >= limitMs) {
      cancel();
      break;
    }
    const Event event = poll();
    if (event == Event::ScanDone || event == Event::ScanFailed) finishScan();
    if (currentState == State::Scanning || currentState == State::Joining) delay(100);
  }
  return currentState == State::Connected;
}

void WifiConnector::cancel() {
  if (currentState == State::Joining) WiFi.disconnect();
  cancelScan();
  lastError = Error::None;
}

void WifiConnector::cancelScan() {
  WiFi.scanDelete();
  savedMode = false;
  currentState = State::Idle;
}

void WifiConnector::rememberConnected() const { WIFI_STORE.setLastConnectedSsid(currentSsid); }
