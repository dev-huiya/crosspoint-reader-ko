#include "ImageViewerMenuActivity.h"

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

ImageViewerMenuActivity::ImageViewerMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 std::string title, const bool canSetSleepCover)
    : UiListActivity("ImageViewerMenu", renderer, mappedInput), title(std::move(title)) {
  const auto addItem = [this](const MenuAction action, const StrId labelId) {
    fui::ListItem item;
    item.label = I18N.get(labelId);
    item.actionValue = static_cast<int16_t>(itemCount);
    actions[itemCount] = action;
    rowItems[itemCount] = item;
    itemCount++;
  };
  if (canSetSleepCover) addItem(MenuAction::SET_SLEEP_COVER, StrId::STR_SET_SLEEP_COVER);
  addItem(MenuAction::DELETE_FILE, StrId::STR_DELETE);
}

void ImageViewerMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

bool ImageViewerMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void ImageViewerMenuActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  // The activated row leaves this screen; a lingering flash would gray an
  // unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;

  MenuResult menu;
  menu.action = static_cast<int>(actions[index]);
  setResult(std::move(menu));
  finish();
}

void ImageViewerMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(itemCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
