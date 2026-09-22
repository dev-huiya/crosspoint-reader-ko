#pragma once

#include "activities/UiListActivity.h"

bool buttonBindingAvailable(uint8_t button);
StrId buttonBindingLabel(uint8_t button);

class ButtonBindingDetailActivity final : public UiListActivity {
 public:
  ButtonBindingDetailActivity(GfxRenderer& renderer, MappedInputManager& input, uint8_t button);
  void onEnter() override;

 private:
  uint8_t button;
  freeink::ui::ListItem rows[3]{};
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
};

class ButtonActionSelectActivity final : public UiListActivity {
 public:
  ButtonActionSelectActivity(GfxRenderer& renderer, MappedInputManager& input, uint8_t button,
                             CrossPointSettings::PressKind kind);
  void onEnter() override;

 private:
  uint8_t button;
  CrossPointSettings::PressKind kind;
  uint8_t actions[static_cast<uint8_t>(CrossPointSettings::ButtonAction::Count)]{};
  freeink::ui::ListItem rows[static_cast<uint8_t>(CrossPointSettings::ButtonAction::Count)]{};
  int count = 0;
  int listCount() const override { return count; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
};
