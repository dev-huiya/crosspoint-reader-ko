#pragma once

#include <FreeInkUI.h>
#include <I18n.h>

#include <cstdint>

namespace keyboard_layouts {

struct LayoutInfo {
  freeink::ui::KeyboardLayoutId id;
  const char* languageCode;
  const char* displayName;
};

// Table position is the persisted bit assignment. Keep existing rows in place
// and append new layouts so SDK enum changes cannot reinterpret saved masks.
inline constexpr LayoutInfo ALL[] = {
    {freeink::ui::KeyboardLayoutId::QwertyEn, "en", "English"},
    {freeink::ui::KeyboardLayoutId::AzertyFr, "fr", "Français"},
    {freeink::ui::KeyboardLayoutId::QwertzDe, "de", "Deutsch"},
    {freeink::ui::KeyboardLayoutId::SpanishEs, "es", "Español"},
    {freeink::ui::KeyboardLayoutId::CyrillicRu, "ru", "Русский"},
    {freeink::ui::KeyboardLayoutId::CyrillicUk, "uk", "Українська"},
    {freeink::ui::KeyboardLayoutId::CyrillicBe, "be", "Беларуская"},
    {freeink::ui::KeyboardLayoutId::CyrillicKk, "kk", "Қазақша"},
    {freeink::ui::KeyboardLayoutId::HebrewIl, "he", "עברית"},
};
inline constexpr uint8_t COUNT = sizeof(ALL) / sizeof(ALL[0]);
static_assert(COUNT <= 16, "keyboard layout mask is uint16_t");

inline constexpr uint16_t bitAt(const uint8_t i) { return static_cast<uint16_t>(1u << i); }
// Symbol layers have no Latin letters, so credentials and URLs require at
// least one of these layouts to remain enabled.
inline constexpr uint16_t LATIN_BITS = bitAt(0) | bitAt(1) | bitAt(2) | bitAt(3);

uint16_t enabled();
freeink::ui::KeyboardLayoutId startingLayout();
freeink::ui::KeyboardLayoutId next(freeink::ui::KeyboardLayoutId current);

}  // namespace keyboard_layouts
