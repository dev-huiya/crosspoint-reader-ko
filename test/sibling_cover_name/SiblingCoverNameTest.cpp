#include <gtest/gtest.h>

#include "SiblingCoverMatch.h"
#include "SiblingCoverName.h"

using sibling_cover::ImageType;
using sibling_cover::imageTypeOf;
using sibling_cover::isSiblingCoverName;

TEST(SiblingCoverNameTest, MatchesSameStemImages) {
  EXPECT_TRUE(isSiblingCoverName("foo.jpg", "foo.epub"));
  EXPECT_TRUE(isSiblingCoverName("foo.jpeg", "foo.xtc"));
  EXPECT_TRUE(isSiblingCoverName("foo.png", "foo.txt"));
  EXPECT_TRUE(isSiblingCoverName("foo.bmp", "foo.epub"));
}

TEST(SiblingCoverNameTest, IgnoresAsciiCase) {
  EXPECT_TRUE(isSiblingCoverName("foo.JPG", "foo.epub"));
  EXPECT_TRUE(isSiblingCoverName("Foo.Png", "foo.EPUB"));
  EXPECT_EQ(imageTypeOf("cover.JPEG"), ImageType::Jpeg);
  EXPECT_EQ(imageTypeOf("cover.BmP"), ImageType::Bmp);
}

TEST(SiblingCoverNameTest, RejectsOtherNamesAndTypes) {
  EXPECT_FALSE(isSiblingCoverName("foo2.jpg", "foo.epub"));
  EXPECT_FALSE(isSiblingCoverName("fo.jpg", "foo.epub"));
  EXPECT_FALSE(isSiblingCoverName("foo.gif", "foo.epub"));
  EXPECT_FALSE(isSiblingCoverName("foo.epub", "foo.epub"));
  EXPECT_FALSE(isSiblingCoverName("foo", "foo.epub"));
  EXPECT_FALSE(isSiblingCoverName("cover.jpg", "foo.epub"));
  EXPECT_FALSE(isSiblingCoverName("foo.jpg.bak", "foo.epub"));
}

TEST(SiblingCoverNameTest, UsesTheLastExtensionAndUtf8Names) {
  EXPECT_TRUE(isSiblingCoverName("my.book.jpg", "my.book.epub"));
  EXPECT_FALSE(isSiblingCoverName("my.jpg", "my.book.epub"));
  EXPECT_TRUE(isSiblingCoverName("\xEC\xB1\x85.png", "\xEC\xB1\x85.txt"));  // "책"
  EXPECT_EQ(sibling_cover::fileNameOf("/books/a/foo.epub"), "foo.epub");
  EXPECT_EQ(sibling_cover::stemOf("foo"), "foo");
}

TEST(SiblingCoverNameTest, MatchesAcrossNfdAndNfc) {
  // "책" composed (U+CC45) and decomposed (U+110E U+1162 U+11A8), as macOS writes it.
  const std::string nfc = "\xEC\xB1\x85";
  const std::string nfd = "\xE1\x84\x8E\xE1\x85\xA2\xE1\x86\xA8";
  EXPECT_FALSE(isSiblingCoverName(nfd + ".jpg", nfc + ".epub"));
  EXPECT_TRUE(sibling_cover::isSiblingCoverNameNfc(nfd + ".jpg", nfc + ".epub"));
  EXPECT_TRUE(sibling_cover::isSiblingCoverNameNfc(nfc + ".png", nfd + ".txt"));
  EXPECT_TRUE(sibling_cover::isSiblingCoverNameNfc(nfd + ".bmp", nfd + ".xtc"));
  EXPECT_FALSE(sibling_cover::isSiblingCoverNameNfc(nfd + "2.jpg", nfc + ".epub"));
}
