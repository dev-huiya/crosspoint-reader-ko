#include <gtest/gtest.h>

#include "LegacyKoLanguage.h"

TEST(LegacyKoLanguageTest, MigratesKnownIndicesWithoutMisreadingOtherLanguages) {
  EXPECT_EQ(decodeLegacyKoLanguage(1, 0), LegacyKoLanguage::English);
  EXPECT_EQ(decodeLegacyKoLanguage(1, 11), LegacyKoLanguage::Korean);
  EXPECT_EQ(decodeLegacyKoLanguage(2, 0), LegacyKoLanguage::English);
  EXPECT_EQ(decodeLegacyKoLanguage(2, 1), LegacyKoLanguage::Korean);
  EXPECT_EQ(decodeLegacyKoLanguage(1, 12), LegacyKoLanguage::Unknown);
  EXPECT_EQ(decodeLegacyKoLanguage(2, 11), LegacyKoLanguage::Unknown);
  EXPECT_EQ(decodeLegacyKoLanguage(3, 1), LegacyKoLanguage::Unknown);
}
