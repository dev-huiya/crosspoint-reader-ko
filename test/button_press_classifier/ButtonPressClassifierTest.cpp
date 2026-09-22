#include <gtest/gtest.h>

#include "ButtonPressClassifier.h"

using Event = ButtonPressClassifier::Event;

TEST(ButtonPressClassifierTest, ImmediateShortWhenDoubleIsDisabled) {
  ButtonPressClassifier input;
  EXPECT_EQ(input.update(10, true, false, true, false, false), Event::None);
  EXPECT_EQ(input.update(40, false, true, false, false, false), Event::Short);
  EXPECT_EQ(input.update(600, false, false, false, false, false), Event::None);
}

TEST(ButtonPressClassifierTest, DelaysShortOnlyWhenDoubleIsEnabled) {
  ButtonPressClassifier input;
  input.update(10, true, false, true, false, true);
  EXPECT_EQ(input.update(40, false, true, false, false, true), Event::None);
  EXPECT_EQ(input.update(540, false, false, false, false, true), Event::None);
  EXPECT_EQ(input.update(541, false, false, false, false, true), Event::Short);
  EXPECT_EQ(input.update(542, false, false, false, false, true), Event::None);
}

TEST(ButtonPressClassifierTest, DoubleReplacesPendingShort) {
  ButtonPressClassifier input;
  input.update(10, true, false, true, false, true);
  input.update(40, false, true, false, false, true);
  input.update(200, true, false, true, false, true);
  EXPECT_EQ(input.update(230, false, true, false, false, true), Event::Double);
  EXPECT_EQ(input.update(800, false, false, false, false, true), Event::None);
}

TEST(ButtonPressClassifierTest, LongFiresOnceAndSuppressesRelease) {
  ButtonPressClassifier input;
  input.update(10, true, false, true, false, false);
  EXPECT_EQ(input.update(709, false, false, true, false, false), Event::None);
  EXPECT_EQ(input.update(710, false, false, true, false, false), Event::Long);
  EXPECT_EQ(input.update(800, false, false, true, false, false), Event::None);
  EXPECT_EQ(input.update(900, false, true, false, false, false), Event::None);
}

TEST(ButtonPressClassifierTest, NativeLongAndSuppressionDoNotLeakShort) {
  ButtonPressClassifier native;
  native.update(1, true, false, false, false, true);
  EXPECT_EQ(native.update(20, false, false, false, true, true), Event::Long);
  EXPECT_EQ(native.update(30, false, true, false, false, true), Event::None);

  ButtonPressClassifier suppressed;
  suppressed.update(1, true, false, true, false, true);
  suppressed.suppress();
  EXPECT_EQ(suppressed.update(30, false, true, false, false, true), Event::None);
}
