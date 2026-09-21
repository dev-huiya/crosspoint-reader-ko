#pragma once

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class MappedInputManager;

// Chooses which BMPs under /.sleep and /sleep take part in the random sleep
// screen rotation (see SleepImageSelectionStore for the rules). KO 1.5 showed
// each image full screen; on 1.6 this is a plain FUI list of file names with an
// ON/OFF value so it matches the other multi-select settings screens.
class SleepImageSelectionActivity final : public UiListActivity {
 public:
  explicit SleepImageSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("SleepImageSelection", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  // Bounded so a card with hundreds of wallpapers cannot exhaust the heap:
  // each row keeps its path and a ListItem.
  static constexpr size_t MAX_IMAGES = 64;

  int listCount() const override { return static_cast<int>(imagePaths.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Walks /.sleep then /sleep, keeps readable BMPs (sorted for a stable
  // order), and prunes saved selections whose file is gone.
  void scanDirs();

  std::vector<std::string> imagePaths;  // full paths, e.g. "/.sleep/foo.bmp"
  std::vector<freeink::ui::ListItem> rowItems;
  bool dirty = false;
};
