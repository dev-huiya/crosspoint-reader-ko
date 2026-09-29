#pragma once

#include <I18n.h>

#include <string>

#include "activities/UiListActivity.h"

// Image viewer menu: the reader-menu list screen with the actions that apply
// to a single image file. Returns MenuResult{action}; Back returns cancelled.
class ImageViewerMenuActivity final : public UiListActivity {
 public:
  enum class MenuAction { SET_SLEEP_COVER, DELETE_FILE };

  ImageViewerMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                          bool canSetSleepCover);

  bool handleHomeGesture() override;

 private:
  static constexpr size_t MAX_MENU_ITEMS = 2;

  int listCount() const override { return static_cast<int>(itemCount); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { closeCancelled(); }
  const char* headerTitle() const override { return title.c_str(); }

  void closeCancelled();

  std::string title;
  MenuAction actions[MAX_MENU_ITEMS]{};
  freeink::ui::ListItem rowItems[MAX_MENU_ITEMS]{};
  size_t itemCount = 0;
};
