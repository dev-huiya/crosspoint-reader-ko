#pragma once
#include "DesktopBoardProfile.h"
namespace BoardConfig {
struct ViewableInsets {
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
};
struct BoardProfile {
  ViewableInsets viewableInsets;
};
inline BoardProfile ACTIVE{{DESKTOP_INSET_TOP, DESKTOP_INSET_RIGHT, DESKTOP_INSET_BOTTOM, DESKTOP_INSET_LEFT}};
}  // namespace BoardConfig
