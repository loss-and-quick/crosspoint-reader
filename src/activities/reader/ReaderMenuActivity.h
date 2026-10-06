#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class ReaderMenuActivity final : public UiListActivity {
 public:
  // Menu actions available from the reader menu.
  enum class MenuAction {
    SELECT_CHAPTER,
    FOOTNOTES,
    TEXT_SETTINGS,
    NIGHT_MODE,
    FRONTLIGHT,
    BLUETOOTH,
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    ROTATE_SCREEN,
    BOOKMARKS,
    SAVE_CLIPPING,
    VIEW_CLIPPINGS,
    TOGGLE_BOOKMARK,
    SCREENSHOT,
    DISPLAY_QR,
    GO_HOME,
    SYNC,
    DELETE_CACHE,
    DICTIONARY,
    READER_SETTINGS
  };

  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  // Which rows the reader offers. The reader fills this in from what its pages support; the
  // menu has no knowledge of the reader type.
  struct Capabilities {
    bool chapters = false;
    bool footnotes = false;
    bool bookmarks = false;  // the list of saved bookmarks
    bool toggleBookmark = false;
    bool saveClipping = false;
    bool viewClippings = false;
    bool lookup = false;
    bool rotate = false;
    bool qr = false;
    bool sync = false;
    bool deleteCache = false;
    bool textSettings = false;
    bool readerSettings = false;  // the status bar settings
  };

  static void toggleBluetooth();
  static void buildMenuItems(std::vector<MenuItem>& items, const Capabilities& caps);

  explicit ReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                              int currentPage, int totalPages, int bookProgressPercent, uint8_t currentOrientation,
                              const Capabilities& caps);

  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
  bool acceptsRemoteInput() const override { return routingReady(); }

 private:
  // Row storage: menuItems is at most MAX_MENU_ITEMS, so a
  // fixed-capacity array avoids any heap allocation for the row list. Labels
  // are set once in the constructor (buildMenuRowItems()); buildScreen()
  // only refreshes rows whose values reflect live state.
  static constexpr size_t MAX_MENU_ITEMS = 19;
  freeink::ui::ListItem menuRowItems[MAX_MENU_ITEMS]{};
  void buildMenuRowItems();

  int listCount() const override { return static_cast<int>(menuItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Popup input runs before any button or touch handling.
  bool handleCustomInput() override;
  // Back closes on RELEASE and Confirm activates on RELEASE; everything else
  // (row navigation, page jumps) falls through to the base handler.
  bool handleButtons() override;
  // Header via GUI.drawHeader inside the safe area for the battery indicator.
  void drawChrome() override;

  void closeCancelled();

  // Fixed menu layout
  std::vector<MenuItem> menuItems;

  OptionPopup optionPopup;
  std::string title = "Reader Menu";
  uint8_t pendingOrientation = 0;
  uint8_t selectedPageTurnOption = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
