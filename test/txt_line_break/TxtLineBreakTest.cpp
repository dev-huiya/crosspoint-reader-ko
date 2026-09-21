// TXT reader line breaking (src/activities/reader/TxtLineBreak.h) with fake
// metrics: 8 px per UTF-8 byte, so a Hangul syllable is 24 px and ASCII 8 px.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "TxtLineBreak.h"

namespace {

int fakeWidth(const std::string& text) { return static_cast<int>(text.size()) * 8; }

// Wrap `text` the way TxtReaderActivity::loadPageAtOffset does: break, skip a
// space at the break, repeat. Returns the segments.
std::vector<std::string> wrap(std::string line, const int width, const bool characterWrap) {
  std::vector<std::string> out;
  while (!line.empty()) {
    size_t breakPos = txt_layout::findBreakPosition(line, width, characterWrap, fakeWidth);
    if (breakPos == 0) breakPos = 1;
    out.push_back(line.substr(0, breakPos));
    size_t skip = breakPos;
    if (breakPos < line.length() && line[breakPos] == ' ') skip++;
    line = line.substr(skip);
  }
  return out;
}

std::string joinWithoutSpaces(const std::vector<std::string>& segments) {
  std::string all;
  for (const auto& s : segments) {
    for (const char c : s) {
      if (c != ' ') all += c;
    }
  }
  return all;
}

std::string withoutSpaces(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (c != ' ') out += c;
  }
  return out;
}

bool onCodepointBoundary(const std::string& s) {
  return s.empty() || (static_cast<unsigned char>(s[0]) & 0xC0) != 0x80;
}

const char* kKorean =
    "봄바람이 불어오는 아침, 오래된 책장을 열었다. 한글 문장과 English words, 12345가 같은 줄에 놓여도 글자가 "
    "빠지지 않아야 한다. 가나다라마바사아자차카타파하가나다라마바사아자차카타파하";

TEST(TxtLineBreak, CharacterWrapKeepsEverySyllableAndFitsTheWidth) {
  const auto lines = wrap(kKorean, 220, true);
  EXPECT_EQ(joinWithoutSpaces(lines), withoutSpaces(kKorean));
  for (const auto& line : lines) {
    EXPECT_LE(fakeWidth(line), 220) << line;
    EXPECT_TRUE(onCodepointBoundary(line)) << line;
    EXPECT_FALSE(line.empty());
  }
  // 220 px holds nine syllables (216 px): character wrap fills lines to that.
  EXPECT_GE(fakeWidth(lines.front()), 200);
}

TEST(TxtLineBreak, WordWrapBreaksAtSpacesButFallsBackForLongWords) {
  const auto lines = wrap(kKorean, 220, false);
  EXPECT_EQ(joinWithoutSpaces(lines), withoutSpaces(kKorean));
  bool sawSpaceBreak = false;
  for (size_t i = 0; i + 1 < lines.size(); ++i) {
    EXPECT_LE(fakeWidth(lines[i]), 220);
    EXPECT_TRUE(onCodepointBoundary(lines[i]));
    EXPECT_NE(lines[i].back(), ' ');
    if (lines[i].find(' ') != std::string::npos) sawSpaceBreak = true;
  }
  EXPECT_TRUE(sawSpaceBreak);
  // The unbreakable 28-syllable run still splits at a character boundary.
  const std::string longRun = "가나다라마바사아자차카타파하가나다라마바사아자차카타파하";
  const auto runLines = wrap(longRun, 220, false);
  EXPECT_GT(runLines.size(), 1u);
  EXPECT_EQ(joinWithoutSpaces(runLines), longRun);
}

TEST(TxtLineBreak, NeverSplitsAUtf8Sequence) {
  const std::string mixed = "a한b글c자d 漢字 ★";
  for (int width = 8; width <= 200; width += 8) {
    const auto lines = wrap(mixed, width, true);
    EXPECT_EQ(joinWithoutSpaces(lines), withoutSpaces(mixed)) << width;
    for (const auto& line : lines) EXPECT_TRUE(onCodepointBoundary(line)) << width << ": " << line;
  }
}

TEST(TxtLineBreak, WholeLineFitsReturnsLength) {
  EXPECT_EQ(txt_layout::findBreakPosition("짧다", 1000, true, fakeWidth), 6u);
  EXPECT_EQ(txt_layout::findBreakPosition("", 1000, true, fakeWidth), 0u);
  // Narrower than one glyph still consumes one whole codepoint.
  EXPECT_EQ(txt_layout::findBreakPosition("가나", 8, true, fakeWidth), 3u);
}

TEST(TxtLineBreak, Utf8LengthCountsCodepoints) {
  EXPECT_EQ(txt_layout::utf8Length("가나다"), 3);
  EXPECT_EQ(txt_layout::utf8Length("abc 가"), 5);
  EXPECT_EQ(txt_layout::utf8Length(""), 0);
}

}  // namespace
