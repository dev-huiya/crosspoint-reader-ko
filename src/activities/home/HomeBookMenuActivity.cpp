#include "HomeBookMenuActivity.h"

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId LABELS[] = {StrId::STR_REFRESH_COVER_CACHE, StrId::STR_DELETE_FILE};
}  // namespace

HomeBookMenuActivity::HomeBookMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title)
    : UiListActivity("HomeBookMenu", renderer, mappedInput), title(std::move(title)) {
  for (size_t i = 0; i < MENU_ITEMS; ++i) {
    rowItems[i].label = I18N.get(LABELS[i]);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

void HomeBookMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

bool HomeBookMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void HomeBookMenuActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  MenuResult menu;
  menu.action = index;  // MenuAction order matches LABELS
  setResult(std::move(menu));
  finish();
}

void HomeBookMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(MENU_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
