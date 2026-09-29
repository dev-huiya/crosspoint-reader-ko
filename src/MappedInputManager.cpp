#include "MappedInputManager.h"

#include <BoardConfig.h>
#include <FreeInkUICore.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>

#include <algorithm>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "components/HeaderBackTapTarget.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;
static_assert(static_cast<uint8_t>(CrossPointSettings::ButtonAction::Count) <= 32);

void MappedInputManager::update(const bool deferCommandActions) const {
  gpio.update();
  actionEvents = 0;
  unboundHoldEvents = 0;
  confirmHeld = false;
  const unsigned long now = millis();
  const auto& pins = BoardConfig::ACTIVE.input;
  const int8_t physicalPins[] = {pins.back, pins.confirm, pins.left, pins.right, pins.up, pins.down, pins.power};
  for (uint8_t button = 0; button < CrossPointSettings::BUTTON_COUNT; ++button) {
    if (button == 7 && !gpio.hasHomeKey()) continue;
    if (button < 7 && physicalPins[button] == BoardConfig::PIN_UNASSIGNED &&
        !(button == HalGPIO::BTN_POWER && BoardConfig::isPaperMono())) continue;
    if (button == HalGPIO::BTN_CONFIRM &&
        BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::XteinkAdcLadder &&
        BoardConfig::ACTIVE.input.confirm != BoardConfig::PIN_UNASSIGNED &&
        BoardConfig::ACTIVE.input.confirm == BoardConfig::ACTIVE.input.power) continue;
    const bool home = button == 7;
    const bool pressed = home ? gpio.wasHomeKeyPressed() : gpio.wasPressed(button);
    const bool released = home ? gpio.wasHomeKeyTapped() : gpio.wasReleased(button);
    const bool held = home ? false : gpio.isPressed(button);
    const bool doubleEnabled = SETTINGS.buttonAction(button, CrossPointSettings::DOUBLE) !=
                               CrossPointSettings::ButtonAction::None;
    // A screen that claims the Confirm hold (Home's book menu) takes it from
    // the button's own long-press binding.
    const bool claimedHold = confirmHoldClaimed && SETTINGS.buttonAction(button, CrossPointSettings::SHORT) ==
                                                       CrossPointSettings::ButtonAction::Confirm;
    const bool longBound =
        claimedHold ||
        SETTINGS.buttonAction(button, CrossPointSettings::LONG) != CrossPointSettings::ButtonAction::None;
    switch (pressState[button].update(now, pressed, released, held, home && gpio.wasHomeKeyLongPressed(),
                                      doubleEnabled, longBound)) {
      case ButtonPressClassifier::Event::Short:
        emitButtonAction(button, CrossPointSettings::SHORT);
        break;
      case ButtonPressClassifier::Event::Long:
        if (claimedHold) {
          confirmHeld = true;
          break;
        }
        emitButtonAction(button, CrossPointSettings::LONG);
        if (!longBound) {
          unboundHoldEvents |= uint32_t{1} << SETTINGS.buttonBindings[button][CrossPointSettings::SHORT];
        }
        break;
      case ButtonPressClassifier::Event::Double:
        emitButtonAction(button, CrossPointSettings::DOUBLE);
        break;
      default:
        break;
    }
  }
  // Command actions (chapter skip onward) seen while a blocking transfer pumps
  // input run on the next main-loop pass; navigation and Home stay live so the
  // transfer can cancel promptly.
  constexpr uint32_t COMMAND_ACTIONS = ~((uint32_t{1} << static_cast<uint8_t>(CrossPointSettings::ButtonAction::ChapterBack)) - 1);
  if (deferCommandActions) {
    deferredActionEvents |= actionEvents & COMMAND_ACTIONS;
  } else if (deferredActionEvents != 0) {
    actionEvents |= deferredActionEvents;
    deferredActionEvents = 0;
  }
  for (uint8_t value = 0; value <= static_cast<uint8_t>(Button::ScreenDown); ++value) {
    if (!isPressed(static_cast<Button>(value))) longPressFiredButtons &= ~(1u << value);
  }
}

void MappedInputManager::resetHomeButtonInput() const {
  pressState[CrossPointSettings::HOME_BUTTON].reset();
  deferredActionEvents = 0;
}

void MappedInputManager::emitButtonAction(uint8_t button, CrossPointSettings::PressKind kind) const {
  const uint8_t action = SETTINGS.buttonBindings[button][kind];
  if (action > 0 && action < static_cast<uint8_t>(CrossPointSettings::ButtonAction::Count))
    actionEvents |= uint32_t{1} << action;
}

bool MappedInputManager::wasAction(CrossPointSettings::ButtonAction action) const {
  return (actionEvents & (uint32_t{1} << static_cast<uint8_t>(action))) != 0;
}

bool MappedInputManager::wasUnboundHold(CrossPointSettings::ButtonAction shortAction) const {
  return (unboundHoldEvents & (uint32_t{1} << static_cast<uint8_t>(shortAction))) != 0;
}

void MappedInputManager::suppressActions() const {
  actionEvents = 0;
  unboundHoldEvents = 0;
  for (auto& state : pressState) state.suppress();
}

bool MappedInputManager::isNavDirectionSwapped() const {
  // Touch boards always follow the rendered orientation; button-only boards keep the user toggle.
  // Home and settings render in portrait, so neither path swaps them.
  const auto orientation = renderer.getOrientation();
  return (gpio.hasTouch() || SETTINGS.frontButtonFollowOrientation) &&
         (orientation == GfxRenderer::PortraitInverted || orientation == GfxRenderer::LandscapeCounterClockwise);
}

MappedInputManager::Button MappedInputManager::mapScreenDirection(const Button button) const {
  // Rows follow GfxRenderer::Orientation's declared order.
  static constexpr Button directions[][4] = {
      {Button::Left, Button::Right, Button::Up, Button::Down},
      {Button::Down, Button::Up, Button::Left, Button::Right},
      {Button::Right, Button::Left, Button::Down, Button::Up},
      {Button::Up, Button::Down, Button::Right, Button::Left},
  };

  uint8_t direction = 0;
  switch (button) {
    case Button::ScreenLeft:
      direction = 0;
      break;
    case Button::ScreenRight:
      direction = 1;
      break;
    case Button::ScreenUp:
      direction = 2;
      break;
    case Button::ScreenDown:
      direction = 3;
      break;
    default:
      return button;
  }

  const uint8_t orientation =
      SETTINGS.frontButtonFollowOrientation ? static_cast<uint8_t>(renderer.getOrientation()) : 0;
  return directions[orientation][direction];
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const, const bool physical) const {
  if (!physical && SETTINGS.buttonBindingsReady && (fn == &HalGPIO::wasPressed || fn == &HalGPIO::wasReleased)) {
    using A = CrossPointSettings::ButtonAction;
    switch (button) {
      case Button::Back: return wasAction(A::Back);
      case Button::Confirm: return wasAction(A::Confirm);
      case Button::Left: return wasAction(A::Left) || wasAction(A::PageBack);
      case Button::Right: return wasAction(A::Right) || wasAction(A::PageForward);
      case Button::Up: return wasAction(A::Up) || wasAction(A::PageBack);
      case Button::Down: return wasAction(A::Down) || wasAction(A::PageForward);
      case Button::PageBack: return wasAction(A::PageBack);
      case Button::PageForward: return wasAction(A::PageForward);
      case Button::Power: return false;
      case Button::NavNext: return wasAction(A::Down) || wasAction(A::Right) || wasAction(A::PageForward);
      case Button::NavPrevious: return wasAction(A::Up) || wasAction(A::Left) || wasAction(A::PageBack);
      case Button::ScreenLeft: return wasAction(A::Left);
      case Button::ScreenRight: return wasAction(A::Right);
      case Button::ScreenUp: return wasAction(A::Up);
      case Button::ScreenDown: return wasAction(A::Down);
    }
  }
  const auto sideLayout = SETTINGS.sideButtonLayout;

  switch (button) {
    case Button::Back:
      // Logical Back maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonBack);
    case Button::Confirm:
      // Logical Confirm maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonConfirm);
    case Button::Left:
      // Logical Left maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonLeft);
    case Button::Right:
      // Logical Right maps to user-configured front button.
      return (gpio.*fn)(SETTINGS.frontButtonRight);
    case Button::Up:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return (gpio.*fn)(isNavDirectionSwapped() ? HalGPIO::BTN_DOWN : HalGPIO::BTN_UP);
        case CrossPointSettings::NEXT_PREV:
          return (gpio.*fn)(isNavDirectionSwapped() ? HalGPIO::BTN_UP : HalGPIO::BTN_DOWN);
        case CrossPointSettings::PREV_PREV:
          return (gpio.*fn)(HalGPIO::BTN_UP) || (gpio.*fn)(HalGPIO::BTN_DOWN);
        case CrossPointSettings::NEXT_NEXT:
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return (gpio.*fn)(isNavDirectionSwapped() ? HalGPIO::BTN_UP : HalGPIO::BTN_DOWN);
        case CrossPointSettings::NEXT_PREV:
          return (gpio.*fn)(isNavDirectionSwapped() ? HalGPIO::BTN_DOWN : HalGPIO::BTN_UP);
        case CrossPointSettings::NEXT_NEXT:
          return (gpio.*fn)(HalGPIO::BTN_UP) || (gpio.*fn)(HalGPIO::BTN_DOWN);
        case CrossPointSettings::PREV_PREV:
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::NavNext:
      // Logical "next item" navigation: side Down + front Right, with the control axis flipped in
      // INVERTED / LANDSCAPE_CCW under the live orientation policy, matching the rotated hint labels.
      return isNavDirectionSwapped() ? (mapButton(Button::Up, fn, physical) || mapButton(Button::Left, fn, physical))
                                     : (mapButton(Button::Down, fn, physical) || mapButton(Button::Right, fn, physical));
    case Button::NavPrevious:
      // Logical "previous item" navigation: side Up + front Left, axis-flipped in the same orientations.
      return isNavDirectionSwapped() ? (mapButton(Button::Down, fn, physical) || mapButton(Button::Right, fn, physical))
                                     : (mapButton(Button::Up, fn, physical) || mapButton(Button::Left, fn, physical));
    case Button::ScreenLeft:
    case Button::ScreenRight:
    case Button::ScreenUp:
    case Button::ScreenDown:
      return mapButton(mapScreenDirection(button), fn, physical);
  }

  return false;
}

namespace {
constexpr unsigned long TOUCH_DOWN_SELECT_DELAY_MS = 90;
constexpr unsigned long TOUCH_HELD_OVERRIDE_WINDOW_MS = 250;
}  // namespace

bool MappedInputManager::hasTouch() const { return gpio.hasTouch(); }

void MappedInputManager::rememberTouchHeldTime() const {
  touchHeldOverrideValid = true;
  touchHeldOverrideMs = gpio.lastTouchHeldMs();
  touchHeldOverrideAt = millis();
}

bool MappedInputManager::wasScreenTapped(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.wasTouchTap(nx, ny)) return false;
  renderer.tapToLogical(nx, ny, x, y);
  rememberTouchHeldTime();
  return true;
}

bool MappedInputManager::wasScreenTouchDown(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  unsigned long heldMs = 0;
  if (!gpio.isTouchTapCandidate(nx, ny, heldMs)) return false;
  if (heldMs < TOUCH_DOWN_SELECT_DELAY_MS) return false;
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::wasScreenLongPress(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.wasTouchLongPress(nx, ny)) return false;
  // Consuming the long-press implies acting on it: suppress the rest of the
  // contact so the finger lift can't also tap whatever the action opened.
  gpio.suppressTouchContact();
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::isScreenTouchHeld(int& x, int& y) const {
  // Live contact position while the finger is down (no tap-slop gate) — drag tracking.
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.isTouchHeldAt(nx, ny)) return false;
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::wasScreenTouchReleased() const { return gpio.wasTouchReleased(); }

bool MappedInputManager::wasTapInRect(const int x, const int y, const int width, const int height) const {
  int tx = 0;
  int ty = 0;
  return wasScreenTapped(tx, ty) && tx >= x && tx < x + width && ty >= y && ty < y + height;
}

MappedInputManager::RowTouch MappedInputManager::rowTouch(int& row, const int top, const int rowStep,
                                                          const int rowCount, const int xStart, const int xEnd,
                                                          const int rowHeight) const {
  if (rowStep <= 0 || rowCount <= 0) return RowTouch::None;
  const auto hit = [&](const int x, const int y) {
    if (x < xStart || x >= xEnd || y < top) return false;
    const int r = (y - top) / rowStep;
    if (r >= rowCount) return false;
    if (rowHeight > 0 && (y - top) % rowStep >= rowHeight) return false;
    row = r;
    return true;
  };
  int x = 0;
  int y = 0;
  if (wasScreenTouchDown(x, y) && hit(x, y)) return RowTouch::Down;
  if (wasScreenTapped(x, y) && hit(x, y)) return RowTouch::Tap;
  return RowTouch::None;
}

MappedInputManager::RowTouch MappedInputManager::colTouch(int& col, const int left, const int colStep,
                                                          const int colCount, const int yStart, const int yEnd,
                                                          const int colWidth) const {
  if (colStep <= 0 || colCount <= 0) return RowTouch::None;
  const auto hit = [&](const int x, const int y) {
    if (y < yStart || y >= yEnd || x < left) return false;
    const int c = (x - left) / colStep;
    if (c >= colCount) return false;
    if (colWidth > 0 && (x - left) % colStep >= colWidth) return false;
    col = c;
    return true;
  };
  int x = 0;
  int y = 0;
  if (wasScreenTouchDown(x, y) && hit(x, y)) return RowTouch::Down;
  if (wasScreenTapped(x, y) && hit(x, y)) return RowTouch::Tap;
  return RowTouch::None;
}

bool MappedInputManager::decodeSwipe(int& sx, int& sy, int& ex, int& ey) const {
  float nxs = 0.0f;
  float nys = 0.0f;
  float nxe = 0.0f;
  float nye = 0.0f;
  if (!gpio.wasSwipe(nxs, nys, nxe, nye)) return false;
  renderer.tapToLogical(nxs, nys, sx, sy);
  renderer.tapToLogical(nxe, nye, ex, ey);
  return true;
}

MappedInputManager::SwipeDir MappedInputManager::wasSwipe() const {
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return SwipeDir::None;
  switch (fui::swipeDirection(sx, sy, ex, ey)) {
    case fui::SwipeDir::Left:
      return SwipeDir::Left;
    case fui::SwipeDir::Right:
      return SwipeDir::Right;
    case fui::SwipeDir::Up:
      return SwipeDir::Up;
    case fui::SwipeDir::Down:
      return SwipeDir::Down;
    default:
      return SwipeDir::None;
  }
}

// Edge classification (which swipe counts as an edge gesture) lives in the
// SDK; only the MEANING of each edge — back, menu, home, light panel, and the
// home-key remap — is decided here.
bool MappedInputManager::wasEdgeSwipe(const freeink::ui::ScreenEdge edge) const {
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return false;
  const bool hit = fui::edgeSwipe(edge, sx, sy, ex, ey, renderer.getScreenWidth(), renderer.getScreenHeight());
  if (hit) rememberTouchHeldTime();
  return hit;
}

bool MappedInputManager::wasBackGesture() const {
  // Tap on the header back button (rect recorded by BaseTheme::drawHeader;
  // empty on screens without one). Folded into Button::Back alongside the
  // swipe so every activity's existing Back handling picks it up.
  int tapX = 0;
  int tapY = 0;
  if (wasScreenTapped(tapX, tapY) && HeaderBackTapTarget::contains(tapX, tapY)) {
    rememberTouchHeldTime();
    return true;
  }
  // Back = left-to-right swipe starting near the left edge. Edge-anchored so that
  // mid-screen horizontal swipes stay available to activities that consume
  // SwipeDir::Left/Right (e.g. percent selection, image viewer).
  return wasEdgeSwipe(fui::ScreenEdge::Left);
}

bool MappedInputManager::wasTopEdgeDownSwipe() const { return wasEdgeSwipe(fui::ScreenEdge::Top); }

bool MappedInputManager::wasBottomEdgeUpSwipe() const { return wasEdgeSwipe(fui::ScreenEdge::Bottom); }

bool MappedInputManager::wasMenuGesture() const { return wasTopEdgeDownSwipe(); }

bool MappedInputManager::wasReaderMenuSwipeUp() const { return gpio.hasHomeKey() && wasBottomEdgeUpSwipe(); }

bool MappedInputManager::wasHomeGesture() const {
  return wasAction(CrossPointSettings::ButtonAction::Home) ||
         (!gpio.hasHomeKey() && wasBottomEdgeUpSwipe());
}

bool MappedInputManager::wasLightPanelGesture() const {
  // On lightless boards the same edge remains available to the reader menu.
  return Frontlight.present() && wasTopEdgeDownSwipe();
}

#if FREEINK_CAP_TOUCH
bool MappedInputManager::wasPowerConfirmClick() const {
  if (!gpio.hasTouch() || SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::PWR_CONFIRM) return false;
  // Wait out the X4 Pro's frontlight double-click window before treating its
  // first release as Confirm. With the shortcut disabled, and on other touch
  // boards, the release counts directly.
  if (BoardConfig::isX4Pro() && SETTINGS.doubleClickPwrLight) return powerConfirmClickFrame;
  return gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() <= SETTINGS.getPowerButtonDuration();
}
#endif

bool MappedInputManager::wasPressed(const Button button) const {
  if (button == Button::Back && wasBackGesture()) return true;
#if FREEINK_CAP_TOUCH
  if (!SETTINGS.buttonBindingsReady && button == Button::Confirm && wasPowerConfirmClick()) return true;
#endif
  return mapButton(button, &HalGPIO::wasPressed);
}

bool MappedInputManager::wasReleased(const Button button) const {
  if (button == Button::Back && wasBackGesture()) return true;
#if FREEINK_CAP_TOUCH
  if (!SETTINGS.buttonBindingsReady && button == Button::Confirm && wasPowerConfirmClick()) return true;
#endif
  return mapButton(button, &HalGPIO::wasReleased);
}

bool MappedInputManager::wasLongPressed(const Button button, const unsigned long thresholdMs) const {
  if (!isPressed(button)) return false;
  // A held button with its own long-press binding runs that instead.
  if (SETTINGS.buttonBindingsReady && heldButtonHasLongBinding()) return false;
  const uint16_t bit = 1u << static_cast<uint8_t>(button);
  if ((longPressFiredButtons & bit) != 0 || getHeldTime() < thresholdMs) return false;
  longPressFiredButtons |= bit;
  suppressNextRelease(button);
  if (SETTINGS.buttonBindingsReady) {
    // The screen consumed this hold: no bound action on its release either.
    for (uint8_t hw = 0; hw < CrossPointSettings::HOME_BUTTON; ++hw) {
      if (gpio.isPressed(hw)) pressState[hw].suppress();
    }
  }
  return true;
}

bool MappedInputManager::heldButtonHasLongBinding() const {
  for (uint8_t hw = 0; hw < CrossPointSettings::HOME_BUTTON; ++hw) {
    if (gpio.isPressed(hw) &&
        SETTINGS.buttonAction(hw, CrossPointSettings::LONG) != CrossPointSettings::ButtonAction::None) {
      return true;
    }
  }
  return false;
}

void MappedInputManager::suppressNextRelease(const Button button) const {
  suppressedReleaseButtons |= 1u << static_cast<uint8_t>(button);
}

bool MappedInputManager::consumeSuppressedRelease() const {
  uint16_t released = 0;
  for (uint8_t value = 0; value <= static_cast<uint8_t>(Button::ScreenDown); ++value) {
    const uint16_t bit = 1u << value;
    if ((suppressedReleaseButtons & bit) != 0 && mapButton(static_cast<Button>(value), &HalGPIO::wasReleased, true)) {
      released |= bit;
    }
  }
  suppressedReleaseButtons &= ~released;
  return released != 0;
}

// Physical state through the front-button and side-layout mapping, also with
// bindings: holds and auto-repeat need it, and bound actions never read it.
bool MappedInputManager::isPressed(const Button button) const { return mapButton(button, &HalGPIO::isPressed); }

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

bool MappedInputManager::wasTouchActivity() const { return gpio.wasTouchActivity(); }

unsigned long MappedInputManager::getHeldTime() const {
  // A bound action has its own meaning, independent of the contact duration.
  if (actionEvents != 0) return 0;
  if (!gpio.wasAnyPressed() && !gpio.wasAnyReleased() && touchHeldOverrideValid &&
      millis() - touchHeldOverrideAt <= TOUCH_HELD_OVERRIDE_WINDOW_MS) {
    return touchHeldOverrideMs;
  }
  touchHeldOverrideValid = false;
  return gpio.getHeldTime();
}

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  // Swap previous/next labels to match the page turn direction swap in INVERTED and LANDSCAPE_CCW.
  const bool swapLabels = isNavDirectionSwapped();
  const char* leftLabel = swapLabels ? next : previous;
  const char* rightLabel = swapLabels ? previous : next;

  return mapFrontLabels(back, confirm, leftLabel, rightLabel);
}

MappedInputManager::Labels MappedInputManager::mapDirectionalLabels(const char* back, const char* confirm,
                                                                    const char* left, const char* right, const char* up,
                                                                    const char* down) const {
  const auto labelForButton = [&](const Button rawButton) {
    if (mapScreenDirection(Button::ScreenLeft) == rawButton) return left;
    if (mapScreenDirection(Button::ScreenRight) == rawButton) return right;
    if (mapScreenDirection(Button::ScreenUp) == rawButton) return up;
    if (mapScreenDirection(Button::ScreenDown) == rawButton) return down;
    return "";
  };
  return mapFrontLabels(back, confirm, labelForButton(Button::Left), labelForButton(Button::Right));
}

MappedInputManager::Labels MappedInputManager::mapFrontLabels(const char* back, const char* confirm, const char* left,
                                                              const char* right) const {
  // Build the label order based on the configured hardware mapping.
  auto labelForHardware = [&](uint8_t hw) -> const char* {
    // Compare against configured logical roles and return the matching label.
    if (hw == SETTINGS.frontButtonBack) {
      return back;
    }
    if (hw == SETTINGS.frontButtonConfirm) {
      return confirm;
    }
    if (hw == SETTINGS.frontButtonLeft) {
      return left;
    }
    if (hw == SETTINGS.frontButtonRight) {
      return right;
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}
