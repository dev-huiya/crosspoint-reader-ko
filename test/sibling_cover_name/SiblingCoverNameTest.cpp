#include <gtest/gtest.h>

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
