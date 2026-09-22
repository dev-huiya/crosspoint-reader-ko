#include <gtest/gtest.h>

#include "ReaderTouchZones.h"

TEST(ReaderTouchZonesTest, FourPresetsSeparateMenuAndPageTurns) {
  EXPECT_EQ(readerTapZone(300, 600, 20, 300, 0), ReaderTapZone::Previous);
  EXPECT_EQ(readerTapZone(300, 600, 150, 300, 0), ReaderTapZone::Menu);
  EXPECT_EQ(readerTapZone(300, 600, 280, 300, 0), ReaderTapZone::Next);

  EXPECT_EQ(readerTapZone(300, 600, 150, 100, 1), ReaderTapZone::Previous);
  EXPECT_EQ(readerTapZone(300, 600, 150, 300, 1), ReaderTapZone::Menu);
  EXPECT_EQ(readerTapZone(300, 600, 150, 550, 1), ReaderTapZone::Next);

  EXPECT_EQ(readerTapZone(300, 600, 150, 250, 2), ReaderTapZone::Previous);
  EXPECT_EQ(readerTapZone(300, 600, 150, 350, 2), ReaderTapZone::Menu);
  EXPECT_EQ(readerTapZone(300, 600, 150, 550, 2), ReaderTapZone::Next);

  EXPECT_EQ(readerTapZone(300, 600, 50, 100, 3), ReaderTapZone::Previous);
  EXPECT_EQ(readerTapZone(300, 600, 250, 100, 3), ReaderTapZone::Next);
  EXPECT_EQ(readerTapZone(300, 600, 250, 590, 3), ReaderTapZone::Menu);
}

TEST(ReaderTouchZonesTest, LandscapeAndOutOfBounds) {
  EXPECT_EQ(readerTapZone(600, 300, 300, 150, 1), ReaderTapZone::Menu);
  EXPECT_EQ(readerTapZone(600, 300, 30, 30, 3), ReaderTapZone::Previous);
  EXPECT_EQ(readerTapZone(600, 300, 570, 30, 3), ReaderTapZone::Next);
  EXPECT_EQ(readerTapZone(600, 300, -1, 10, 0), ReaderTapZone::None);
  EXPECT_EQ(readerTapZone(600, 300, 600, 10, 0), ReaderTapZone::None);
}
