#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "EndOfBookOptions.h"
#include "ReaderMenuActivity.h"
#include "ReaderSession.h"
#include "activities/Activity.h"

class ReaderActivity : public Activity {
  friend class ActivityManager;  // turns pages for a Bluetooth remote (ActivityManager::remoteTurn)

 protected:
  std::string bookPath;
  int pagesUntilFullRefresh = 0;
  bool forcedRefreshPending = false;

  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};
  ReaderSession readerSession;
  std::atomic<bool> pageRendered{false};
  bool bookRemembered = false;
  void markPageRendered() { pageRendered.store(true, std::memory_order_release); }
  void rememberBookOnceRendered();

  explicit ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string bookPath, bool allowFastInitialRefresh);

  virtual bool loadBook() = 0;
  // Called when loadBook() failed. Return true to keep the activity alive
  // (e.g. showing a dialog); false finishes it (the default).
  virtual bool handleLoadFailure() { return false; }
  virtual std::string getBookTitle() const = 0;
  virtual std::string getBookAuthor() const { return ""; }
  virtual std::string getBookThumbBmpPath() const { return ""; }
  // Whole-book progress for the reader.exit plugin event, reusing the
  // per-reader ScreenshotInfo implementations.
  //
  // Upstream added this helper in d3e55c53 with an earlier slice of this
  // feature, then removed it in c1e1fec3 as dead code once no caller remained
  // ("removes unnecessary wrapper structs"). It is reinstated here because this
  // change reintroduces the callers: ReaderActivity.cpp uses it for the progress
  // string and for the session's render-complete basis points, and
  // EpubReaderActivity overrides getProgressBasisPoints() and falls back to it.
  int getProgressPercent() const { return getScreenshotInfo().progressPercent; }
  virtual int getProgressBasisPoints() const { return getProgressPercent() * 100; }

  virtual bool handleFormatInput() { return false; }
  virtual bool pageTurn(bool isForward) = 0;
  virtual bool skipPages(int amount) { return pageTurn(amount > 0); }
  // A Bluetooth remote asks for the reader menu (true) or a new clipping (false).
  virtual bool remoteShortcut(bool /*readerMenu*/) { return false; }
  virtual bool isAtEndOfBook() const = 0;
  virtual void onReturnFromEndOfBook() {}

  virtual void renderBook() = 0;
  virtual void applyInitialOrientation();
  virtual void onEndOfBookRendered() {}

  // ---- Reader shell: behaviour every reader shares, whatever produces its pages.
  // A reader supplies its pages and positions through the virtual hooks above and below; the
  // shell owns go-to-percent, the screenshot request, automatic page turning and the input loop.

  // Moves to `percent` of the book. Called with the render lock not held.
  virtual void jumpToPercent(int percent) {}
  // Opens the reader's menu; go-to-percent returns to it when cancelled.
  virtual void openReaderMenu() {}
  void openGoToPercent(int initialPercent);

  // Rows the reader's menu offers.
  virtual ReaderMenuActivity::Capabilities menuCapabilities() const { return {}; }

  bool pendingScreenshot = false;
  void requestScreenshot();
  void takePendingScreenshot();

  // Automatic page turn. `autoTurnOption` indexes ReaderUtils::PAGE_TURN_RATES (0 = off).
  bool autoTurnActive = false;
  uint8_t autoTurnOption = 0;
  unsigned long autoTurnIntervalMs = 0;
  // Time of the last successful manual or automatic page turn; spaces turns and auto turns.
  unsigned long lastPageTurnTime = 0;
  void setAutoPageTurn(uint8_t option);
  // Turns the page when the interval has elapsed; true when this call consumed the loop pass.
  bool tickAutoPageTurn();

  // ---- Input loop. readerLoop() is the input loop of a reader that runs on the shell; what
  // differs between readers is expressed through the hooks below.
  //
  // Per-pass work of the reader before any input is looked at (background builds, popups,
  // layout drift). Return false to end the pass.
  virtual bool onLoopTick() { return true; }
  // False while the reader has no page to turn or draw on (it is still being laid out).
  virtual bool pageReady() const { return true; }
  virtual bool isRtlBook() const { return false; }
  // Pages a long press of a page button skips in chapter-skip mode.
  virtual int chapterSkipAmount() const { return 10; }
  // Reader-specific input handled before page turning: link taps, footnote Back, extra keys.
  // Return true when the pass was consumed.
  virtual bool handleTapInput() { return false; }
  virtual bool handleBackOverride() { return false; }
  virtual bool handleExtraKeys(bool endOfBookMenuOpen) { return false; }

  // What the reader does with the Confirm long press, the Home key and a rotation request.
  // `guardRepeat`: ignore the request while the previous confirmation popup is still up.
  virtual void bookmarkAction(bool guardRepeat) {}
  virtual void lookupAction(bool guardRepeat) {}
  // True when the sync screen was launched.
  virtual bool syncAction() { return false; }
  virtual void rotateTo(uint8_t orientation) {}
  // The auto page turn just became active.
  virtual void onAutoTurnStarted() {}
  unsigned long confirmLongPressThreshold(const ReaderMenuActivity::Capabilities& caps) const;

  // A page turn requested while the previous one is still drawing waits here (-1 / 0 / +1).
  int8_t pendingManualTurn = 0;
  bool pendingManualTurnTouch = false;

  // Toolbar menu hooks of the reader (see readerLoop).
  virtual bool usesToolbarMenu() const { return false; }
  virtual bool handleMenuOverlay() { return false; }
  virtual bool openToolbarOverlay() { return false; }
  void openMenu();
  void readerLoop();

  bool handleBackNavigation();
  /** True while the end-of-book suggestion menu is on screen and owning input. */
  bool endOfBookMenuActive() const;
  /** True while a toolbar, panel or menu over the page owns the device keys: a remote turn is refused. */
  virtual bool inputOverPage() const { return endOfBookMenuActive(); }
  bool handleEndOfBookMenu(bool suppressConfirmRelease = false);
  bool handleEndOfBookPageTurn(bool prevTriggered, bool nextTriggered);
  void clearEndOfBookOptionsIfNeeded();
  void disableFastInitialRefresh();
  void notePageTurn(bool forward, bool succeeded);
  void flushReaderSession();

 public:
  ~ReaderActivity() override = default;

  static std::unique_ptr<ReaderActivity> create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                std::string path, bool allowFastInitialRefresh);

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  bool isReaderActivity() const final { return true; }
  bool acceptsRemoteInput() const final { return pageRendered.load(std::memory_order_acquire); }
  bool handleForcedRefresh() final;
};
