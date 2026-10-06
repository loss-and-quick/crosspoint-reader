#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "EndOfBookOptions.h"
#include "ReaderMenuActivity.h"
#include "ReaderSession.h"
#include "ReaderToolbarUi.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

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
  // A reader supplies its pages and positions through the virtual hooks above and below;
  // the shell owns the status bar, the reader menu, go-to-percent, the screenshot request,
  // automatic page turning and the Home-key menu action.
  struct StatusBarContent {
    float bookProgress = 0;
    int page = 0;  // 1-based page in the chapter
    int pageCount = 0;
    std::string title;
    int paddingBottom = 0;
    int textYOffset = 0;
    bool fillMargin = true;
    bool bookmarked = false;
    bool pageCountEstimated = false;
  };
  // Draws the shared status bar with the shared status-bar settings.
  void drawStatusBar(const StatusBarContent& content) const;
  // The title lane text for the configured title mode (book, chapter or none).
  std::string statusBarTitle(const char* bookTitle, const char* chapterTitle) const;

  // Whole-book position as a percentage; the default reads the progress of the screenshot info.
  virtual int currentPercent() const { return getProgressPercent(); }
  // Moves to `percent` of the book. Called with the render lock not held.
  virtual void jumpToPercent(int percent) {}
  void openGoToPercent(int initialPercent);

  // Reader menu. The default is the shared menu of fixed-layout readers; a reader with its
  // own menu chrome overrides openReaderMenu().
  struct MenuState {
    std::string title;
    int page = 0;  // 1-based page in the chapter, 0 when unknown
    int pageCount = 0;
    int percent = 0;
  };
  virtual void openReaderMenu();
  virtual MenuState menuState() const { return {getBookTitle(), 0, 0, currentPercent()}; }
  // Rows the menu offers; the shell adds the ones every reader has.
  virtual ReaderMenuActivity::Capabilities menuCapabilities() const { return {}; }
  virtual void openChapterList() {}
  // Menu rows after the shared ones are handled; the reader reacts to anything else here.
  virtual void onReaderMenuAction(ReaderMenuActivity::MenuAction action) {}
  void runMenuAction(ReaderMenuActivity::MenuAction action);

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

  // ---- Input loop. loop() is the one input loop of every reader; what differs between readers
  // is expressed through the hooks below.
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

  // What the reader can do with the Confirm long press and the Home key.
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

  void openMenu();

  // ---- Toolbar reader menu (SETTINGS.readerMenuStyle == READER_MENU_TOOLBAR): a toolbar and
  // bottom-sheet panels drawn over the page that is on screen. The shell owns the state and the
  // input; the reader provides the content below.
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::None;
  virtual bool usesToolbarMenu() const;
  void openOverlay(Overlay target);
  void closeOverlayToPage();
  void discardOverlayPage();
  void handleOverlayInput();
  void renderOverlay();
  void paintOverlayPopup();
  // Repaints the open overlay over the page the reader just drew. Call at the end of a page render.
  void paintOverlayOnPage();
  bool handleMenuOverlay();

  struct ToolbarInfo {
    std::string chapterTitle;
    char pageInfo[32] = {};  // "12/40   51%"
    int progressPermille = 0;
  };
  virtual void toolbarInfo(ToolbarInfo& info) const {}
  static void formatPageInfo(ToolbarInfo& info, int page, int pageCount, int percent);
  // Chapter scrub of the toolbar: the unit is chapters, or pages when the book has no chapters.
  virtual int scrubCount() const { return 0; }
  virtual int scrubPosition() const { return 0; }
  virtual void scrubTo(int index) {}
  // Contents panel.
  virtual int contentsCount() const { return 0; }
  virtual int contentsCurrentIndex() const { return 0; }
  virtual std::string contentsRowText(int index) const { return {}; }
  virtual void activateContentsItem(int index) {}
  // Text panel; only readers that lay text out have one.
  virtual int textPanelCount() const { return 0; }
  virtual std::string textRowName(int row) const { return {}; }
  virtual std::string textRowValue(int row) const { return {}; }
  virtual void textRowCheckbox(int row, freeink::ui::ListItem& item) const {}
  virtual void activateTextRow(int row) {}

  int focusedTool = 0;  // toolbar tool focus: 0=Contents, 1=Text, 2=More
  int panelIndex = 0;   // selected row within the active panel
  // A tap steps one row; a hold jumps PANEL_HOLD_STEP rows once (a contents list runs to
  // hundreds of chapters). True once a hold has jumped, so the release that ends it is swallowed.
  static constexpr unsigned long PANEL_HOLD_MS = 1500;
  static constexpr int PANEL_HOLD_STEP = 10;
  bool panelHoldJumped = false;
  // Whether the panel draws its cursor row. Button boards always do; touch boards only once a
  // button has moved it, so a tapped row is not left inverted.
  bool panelCursorShown = false;
  // FreeInkUI chrome and tap targets; created when the overlay opens, released when it closes.
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  // Modal option picker over a panel, for enum rows.
  OptionPopup overlayPopup;
  // True while a clean-page snapshot (renderer.storeBwBuffer) backs the open overlay, letting
  // panel to toolbar steps restore the page without a re-render.
  bool overlayPageStored = false;
  // True while a deferred overlay refresh may still be running on the panel; settle it before the
  // framebuffer is touched or another differential refresh is pushed.
  bool overlayRefreshPending = false;
  // A background step lent the framebuffer: it came back white while the panel still shows the
  // page. Nothing may be painted onto it until the reader redraws.
  std::atomic<bool> pageBufferStale{false};
  void pushOverlayRefresh();
  void settleOverlayRefresh();
  // More panel: the list menu rows without the ones that have their own tool.
  std::vector<ReaderMenuActivity::MenuItem> moreItems;
  void buildMoreActions();
  std::string moreRowName(int row) const;
  std::string moreRowValue(int row) const;
  void activateMoreRow(int row);

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
  void loop() final;
  void render(RenderLock&& lock) override;

  bool isReaderActivity() const final { return true; }
  bool acceptsRemoteInput() const final { return pageRendered.load(std::memory_order_acquire); }
  bool handleForcedRefresh() final;
};
