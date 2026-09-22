#pragma once

#include "activities/Activity.h"

class TouchZoneLayoutActivity final : public Activity {
 public:
  TouchZoneLayoutActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("TouchZoneLayout", renderer, input) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  uint8_t selected = 0;
  void choose(uint8_t index);
};
