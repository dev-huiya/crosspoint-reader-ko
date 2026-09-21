#include <gtest/gtest.h>

#include <builtinFonts/kopub_14_regular.h>
#include <builtinFonts/pretendard_10_regular.h>

#include "EpdFont.h"

TEST(KoBuiltinFonts, BodyAndUiCoverModernHangul) {
  const EpdFont body(&kopub_14_regular);
  const EpdFont ui(&pretendard_10_regular);
  for (const uint32_t cp : {0xAC00u, 0xD7A3u, 0xD7A2u, 0x3131u}) {
    EXPECT_TRUE(body.hasCodepoint(cp)) << cp;
    EXPECT_TRUE(ui.hasCodepoint(cp)) << cp;
  }
}

TEST(KoBuiltinFonts, BodyCoversHanjaMissingFromUi) {
  const EpdFont body(&kopub_14_regular);
  const EpdFont ui(&pretendard_10_regular);
  EXPECT_TRUE(body.hasCodepoint(0x6F22));  // 漢
  EXPECT_FALSE(ui.hasCodepoint(0x6F22));
}
