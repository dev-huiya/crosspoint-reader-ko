#pragma once

#include <cstdint>

enum class LegacyKoLanguage : uint8_t { Unknown, English, Korean };

// KO 1.5 stored a version byte followed by a numeric language index in
// /.crosspoint/language.bin. Version 1 predates the Belarusian insertion;
// version 2 uses the KO two-language firmware's EN=0, KOREAN=1 indices.
constexpr LegacyKoLanguage decodeLegacyKoLanguage(uint8_t version, uint8_t index) {
  if (index == 0 && (version == 1 || version == 2)) return LegacyKoLanguage::English;
  if ((version == 1 && index == 11) || (version == 2 && index == 1)) return LegacyKoLanguage::Korean;
  return LegacyKoLanguage::Unknown;
}
