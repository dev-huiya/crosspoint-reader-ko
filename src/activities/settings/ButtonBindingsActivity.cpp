#include "ButtonBindingsActivity.h"

#include <BoardConfig.h>
#include <HalFrontlight.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <iterator>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;
namespace {

constexpr StrId BUTTON_LABELS[] = {
    StrId::STR_BUTTON_BACK, StrId::STR_BUTTON_CONFIRM, StrId::STR_BUTTON_LEFT, StrId::STR_BUTTON_RIGHT,
    StrId::STR_BUTTON_SIDE_UP, StrId::STR_BUTTON_SIDE_DOWN, StrId::STR_BUTTON_POWER, StrId::STR_BUTTON_HOME};
constexpr StrId PRESS_LABELS[] = {
    StrId::STR_BUTTON_SHORT, StrId::STR_BUTTON_LONG, StrId::STR_BUTTON_DOUBLE};
constexpr StrId ACTION_LABELS[] = {
    StrId::STR_IGNORE, StrId::STR_BACK, StrId::STR_CONFIRM, StrId::STR_DIR_UP, StrId::STR_DIR_DOWN,
    StrId::STR_DIR_LEFT, StrId::STR_DIR_RIGHT, StrId::STR_HOME, StrId::STR_BUTTON_PAGE_BACK,
    StrId::STR_BUTTON_PAGE_FORWARD, StrId::STR_BUTTON_CHAPTER_BACK, StrId::STR_BUTTON_CHAPTER_FORWARD,
    StrId::STR_READER_MENU, StrId::STR_BUTTON_ROTATE, StrId::STR_SLEEP, StrId::STR_BUTTON_LIGHT_TOGGLE,
    StrId::STR_FORCE_REFRESH, StrId::STR_FOOTNOTES, StrId::STR_BOOKMARK_OPTION, StrId::STR_DICTIONARY,
    StrId::STR_KOREADER_SYNC, StrId::STR_BROWSE_FILES};
static_assert(std::size(ACTION_LABELS) == static_cast<size_t>(CrossPointSettings::ButtonAction::Count));

}  // namespace

StrId buttonBindingLabel(uint8_t button) { return BUTTON_LABELS[button]; }

bool buttonBindingAvailable(uint8_t button) {
  if (button == 7) return BoardConfig::hasHomeKey();
  if (button == 6 && BoardConfig::isPaperMono()) return true;
  const auto& pins = BoardConfig::ACTIVE.input;
  const int8_t ids[] = {pins.back, pins.confirm, pins.left, pins.right, pins.up, pins.down, pins.power};
  if (button != 6 && BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::XteinkAdcLadder &&
      ids[button] == pins.power) return false;
  return ids[button] != BoardConfig::PIN_UNASSIGNED;
}

ButtonBindingDetailActivity::ButtonBindingDetailActivity(GfxRenderer& renderer, MappedInputManager& input,
                                                         uint8_t button)
    : UiListActivity("ButtonBindingDetail", renderer, input), button(button) {}

void ButtonBindingDetailActivity::onEnter() {
  UiListActivity::onEnter();
  for (int i = 0; i < listCount(); ++i) {
    rows[i].label = I18N.get(PRESS_LABELS[i == 1 && button == 6 && BoardConfig::isPaperMono() ? 2 : i]);
    rows[i].actionValue = i;
  }
  requestUpdate();
}

int ButtonBindingDetailActivity::listCount() const {
  return button == 6 && BoardConfig::isPaperMono() ? 2 : 3;
}

const char* ButtonBindingDetailActivity::headerTitle() const { return I18N.get(BUTTON_LABELS[button]); }

void ButtonBindingDetailActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
  const auto kind = static_cast<CrossPointSettings::PressKind>(
      index == 1 && button == 6 && BoardConfig::isPaperMono() ? CrossPointSettings::DOUBLE : index);
  app.clearTapFlash();
  auto activity = makeUniqueNoThrow<ButtonActionSelectActivity>(renderer, mappedInput, button, kind);
  if (activity) startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
  else LOG_ERR("BUTTON", "OOM: ButtonActionSelectActivity");
}

void ButtonBindingDetailActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  for (int i = 0; i < listCount(); ++i) {
    const auto kind = static_cast<CrossPointSettings::PressKind>(
        i == 1 && button == 6 && BoardConfig::isPaperMono() ? CrossPointSettings::DOUBLE : i);
    rows[i].value = I18N.get(ACTION_LABELS[SETTINGS.buttonBindings[button][kind]]);
  }
  fui::ListProps props;
  props.items = rows;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

ButtonActionSelectActivity::ButtonActionSelectActivity(GfxRenderer& renderer, MappedInputManager& input,
                                                       uint8_t button, CrossPointSettings::PressKind kind)
    : UiListActivity("ButtonActionSelect", renderer, input), button(button), kind(kind) {}

void ButtonActionSelectActivity::onEnter() {
  UiListActivity::onEnter();
  count = 0;
  for (uint8_t action = 0; action < std::size(ACTION_LABELS); ++action) {
    if (action == static_cast<uint8_t>(CrossPointSettings::ButtonAction::LightToggle) && !Frontlight.present()) continue;
    actions[count] = action;
    rows[count].label = I18N.get(ACTION_LABELS[action]);
    rows[count].actionValue = count;
    if (SETTINGS.buttonBindings[button][kind] == action) nav.selected = count;
    ++count;
  }
  requestUpdate();
}

const char* ButtonActionSelectActivity::headerTitle() const { return I18N.get(PRESS_LABELS[kind]); }

void ButtonActionSelectActivity::activateIndex(int index) {
  if (index < 0 || index >= count) return;
  const uint8_t action = actions[index];
  if (SETTINGS.buttonBindings[button][kind] != action) {
    SETTINGS.buttonBindings[button][kind] = action;
    SETTINGS.saveToFile();
  }
  app.clearTapFlash();
  finish();
}

void ButtonActionSelectActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  fui::ListProps props;
  props.items = rows;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
