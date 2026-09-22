#include "TouchZoneLayoutActivity.h"

#include <algorithm>

#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr StrId LABELS[] = {StrId::STR_TOUCH_ZONE_1, StrId::STR_TOUCH_ZONE_2,
                            StrId::STR_TOUCH_ZONE_3, StrId::STR_TOUCH_ZONE_4};
}

void TouchZoneLayoutActivity::onEnter() {
  Activity::onEnter();
  selected = SETTINGS.touchZoneLayout < 4 ? SETTINGS.touchZoneLayout : 0;
  requestUpdate();
}

void TouchZoneLayoutActivity::choose(uint8_t index) {
  if (index >= 4) return;
  selected = index;
  if (SETTINGS.touchZoneLayout != index) {
    SETTINGS.touchZoneLayout = index;
    SETTINGS.saveToFile();
  }
  requestUpdate();
}

void TouchZoneLayoutActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) { finish(); return; }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) { choose(selected); finish(); return; }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
    selected = (selected + 1) % 4;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    selected = (selected + 3) % 4;
    requestUpdate();
  }
  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int top = metrics.topPadding + metrics.headerHeight;
    const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;
    if (y < top || y >= bottom) return;
    const uint8_t row = (y - top) * 2 / (bottom - top);
    const uint8_t col = x < renderer.getScreenWidth() / 2 ? 0 : 1;
    choose(row * 2 + col);
  }
}

void TouchZoneLayoutActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int top = metrics.topPadding + metrics.headerHeight;
  const int bodyHeight = renderer.getScreenHeight() - top - metrics.buttonHintsHeight;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_TOUCH_ZONE_LAYOUT));
  for (uint8_t i = 0; i < 4; ++i) {
    const int col = i % 2, row = i / 2;
    const int cellX = col * width / 2, cellY = top + row * bodyHeight / 2;
    const int cellWidth = width / 2;
    const int cellHeight = bodyHeight / 2;
    const int maxWidth = cellWidth * 4 / 5;
    const int maxHeight = cellHeight * 3 / 4;
    const int previewWidth = std::min(maxWidth, maxHeight * width / renderer.getScreenHeight());
    const int previewHeight = previewWidth * renderer.getScreenHeight() / width;
    const Rect preview{cellX + (cellWidth - previewWidth) / 2, cellY + cellHeight / 20,
                       previewWidth, previewHeight};
    UITheme::drawTouchZonePreview(renderer, preview, i, selected == i);
    UITheme::drawCenteredText(renderer, Rect{cellX, cellY + cellHeight * 4 / 5, cellWidth, cellHeight / 5},
                              UI_10_FONT_ID, cellY + cellHeight * 9 / 10, I18N.get(LABELS[i]));
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
