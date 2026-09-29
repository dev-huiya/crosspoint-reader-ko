#pragma once

#include <I18n.h>

#include <string>

#include "activities/UiListActivity.h"

// Long-press menu for a book on the home screen. Returns MenuResult{action};
// Back returns cancelled.
class HomeBookMenuActivity final : public UiListActivity {
 public:
  enum class MenuAction { REFRESH_CACHE, DELETE_FILE };

  HomeBookMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title);

  bool handleHomeGesture() override;

 private:
  static constexpr size_t MENU_ITEMS = 2;

  int listCount() const override { return static_cast<int>(MENU_ITEMS); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { closeCancelled(); }
  const char* headerTitle() const override { return title.c_str(); }

  void closeCancelled();

  std::string title;
  freeink::ui::ListItem rowItems[MENU_ITEMS]{};
};
