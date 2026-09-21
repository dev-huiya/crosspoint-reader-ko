#pragma once
#include <I18n.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Reader menu for TXT files: the EPUB menu screen with the action list cut
// down to what a flat byte-offset reader can do (no chapters, footnotes,
// bookmarks or sync targets) plus the long-press page-jump step.
class TxtReaderMenuActivity final : public UiListActivity {
 public:
  // Menu actions available from the reader menu.
  enum class MenuAction {
    TEXT_SETTINGS,
    NIGHT_MODE,
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    PAGE_JUMP_STEP,
    ROTATE_SCREEN,
    SCREENSHOT,
    GO_HOME,
    RESET_READING_TIMER
  };

  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static void buildMenuItems(std::vector<MenuItem>& items);

  explicit TxtReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                 const int currentPage, const int totalPages, const int bookProgressPercent,
                                 const uint8_t currentOrientation, const uint8_t currentPageTurnOption,
                                 const uint8_t currentPageJumpOption, uint32_t totalReadingSeconds);

  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  // Row storage: menuItems is at most MAX_MENU_ITEMS, so a
  // fixed-capacity array avoids any heap allocation for the row list. Labels
  // are set once in the constructor (buildMenuRowItems()); buildScreen()
  // only refreshes rows whose values reflect live state.
  // The TXT menu is a fixed list of nine rows.
  static constexpr size_t MAX_MENU_ITEMS = 9;
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
  uint8_t selectedPageJumpOption = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  // Auto-turn rates match the EPUB menu so both readers behave the same.
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
  // Long-press multi-page jump step. Index 0 = OFF (single-page navigation
  // unchanged); the others are the page count jumped when a nav button is held.
  const std::vector<const char*> pageJumpLabels = {I18N.get(StrId::STR_STATE_OFF), "10", "20", "50", "100"};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
  uint32_t totalReadingSeconds = 0;
};
