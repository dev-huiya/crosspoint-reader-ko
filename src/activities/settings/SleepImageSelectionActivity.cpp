#include "SleepImageSelectionActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <set>

#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "SleepImageSelectionStore.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
// Same two roots SleepActivity draws from, hidden first.
constexpr const char* SLEEP_DIRS[] = {"/.sleep", "/sleep"};
constexpr size_t MAX_NAME_LEN = 128;
}  // namespace

void SleepImageSelectionActivity::onEnter() {
  UiListActivity::onEnter();
  scanDirs();
  SLEEP_IMAGE_SELECTION.loadFromFile();

  // Drop saved entries whose file is gone so the list reflects the card and a
  // stale path never keeps an image "selected" that can no longer show.
  const std::set<std::string> existing(imagePaths.begin(), imagePaths.end());
  dirty = SLEEP_IMAGE_SELECTION.pruneMissing(existing);

  rowItems.clear();
  rowItems.resize(imagePaths.size());
  for (size_t i = 0; i < imagePaths.size(); ++i) {
    // Labels point into imagePaths, which is not modified after this loop.
    const std::string& path = imagePaths[i];
    const size_t slash = path.rfind('/');
    rowItems[i].label = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

void SleepImageSelectionActivity::onExit() {
  if (dirty) SLEEP_IMAGE_SELECTION.saveToFile();
  Activity::onExit();
}

const char* SleepImageSelectionActivity::headerTitle() const { return tr(STR_SELECT_SLEEP_SCREENS); }

void SleepImageSelectionActivity::scanDirs() {
  imagePaths.clear();
  char name[MAX_NAME_LEN];
  for (const char* dirPath : SLEEP_DIRS) {
    auto dir = Storage.open(dirPath);
    if (!dir || !dir.isDirectory()) continue;
    for (auto file = dir.openNextFile(); file && imagePaths.size() < MAX_IMAGES; file = dir.openNextFile()) {
      if (file.isDirectory()) continue;
      file.getName(name, sizeof(name));
      if (name[0] == '\0' || name[0] == '.' || !FsHelpers::hasBmpExtension(name)) continue;
      // Validate the header: SleepActivity skips unreadable files, so offering
      // one here would let the user select an image that never appears.
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        LOG_DBG("SIS", "Skipping invalid BMP: %s/%s", dirPath, name);
        continue;
      }
      imagePaths.emplace_back(std::string(dirPath) + "/" + name);
    }
  }
  std::sort(imagePaths.begin(), imagePaths.end());
  LOG_DBG("SIS", "Found %u sleep image candidate(s)", static_cast<unsigned>(imagePaths.size()));
}

void SleepImageSelectionActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(imagePaths.size())) return;
  nav.selected = index;
  // The row stays on screen with a new ON/OFF value; a lingering flash would
  // gray an unrelated row on the repaint below.
  app.clearTapFlash();
  // An empty store means every image is in rotation, so the first toggle must
  // start from "all selected" and remove this one — not select only this one.
  if (SLEEP_IMAGE_SELECTION.empty()) {
    for (const auto& path : imagePaths) SLEEP_IMAGE_SELECTION.setSelected(path, true);
  }
  const std::string& path = imagePaths[index];
  SLEEP_IMAGE_SELECTION.setSelected(path, !SLEEP_IMAGE_SELECTION.isSelected(path));
  dirty = true;
  requestUpdate();
}

void SleepImageSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (imagePaths.empty()) {
    screen.centeredText(tr(STR_NO_SLEEP_IMAGES));
    return;
  }

  // An empty store means "all images"; show that state honestly rather than
  // a column of OFF rows that would suggest nothing is going to be shown.
  const bool allSelected = SLEEP_IMAGE_SELECTION.empty();
  for (size_t i = 0; i < imagePaths.size(); ++i) {
    rowItems[i].value =
        (allSelected || SLEEP_IMAGE_SELECTION.isSelected(imagePaths[i])) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
