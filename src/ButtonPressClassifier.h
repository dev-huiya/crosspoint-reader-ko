#pragma once

#include <cstdint>

class ButtonPressClassifier {
 public:
  enum class Event : uint8_t { None, Short, Long, Double };

  static constexpr uint32_t LONG_PRESS_MS = 700;
  static constexpr uint32_t DOUBLE_PRESS_MS = 500;

  // `longBound`: the button has a long-press action. Without one, a hold is
  // still reported as Long (for screens that give it a meaning) and its
  // release then counts as a short press, so a slow press is not lost.
  Event update(uint32_t now, bool pressed, bool released, bool held, bool nativeLong, bool doubleEnabled,
               bool longBound = true) {
    Event event = Event::None;
    if (pendingShort && now - firstClickAt > DOUBLE_PRESS_MS) {
      event = Event::Short;
      pendingShort = false;
    }
    if (pressed) {
      down = true;
      downAt = now;
      longFired = false;
      shortAfterLong = false;
    }
    if (nativeLong || (held && down && !longFired && now - downAt >= LONG_PRESS_MS)) {
      longFired = true;
      shortAfterLong = !longBound;
      pendingShort = false;
      event = Event::Long;
    }
    if (released) {
      down = false;
      if (shortAfterLong) {
        shortAfterLong = false;
        event = Event::Short;
      } else if (!longFired) {
        if (!doubleEnabled) {
          event = Event::Short;
        } else if (pendingShort && now - firstClickAt <= DOUBLE_PRESS_MS) {
          pendingShort = false;
          event = Event::Double;
        } else {
          pendingShort = true;
          firstClickAt = now;
        }
      }
    }
    return event;
  }

  // Drops a pending first click without touching a press in progress.
  void reset() { pendingShort = false; }

  void suppress() {
    pendingShort = false;
    shortAfterLong = false;
    if (down) longFired = true;
  }

 private:
  uint32_t downAt = 0;
  uint32_t firstClickAt = 0;
  bool down = false;
  bool longFired = false;
  bool pendingShort = false;
  bool shortAfterLong = false;
};
