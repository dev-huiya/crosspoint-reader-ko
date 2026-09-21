// Golden snapshots of Korean paragraph layout (§50 of the KO 1.6 work order).
//
// Each case parses fixed XHTML through the real ChapterHtmlSlimParser with the
// stub renderer metrics (8 px per UTF-8 byte, 4 px space, 16 px line height)
// and records every emitted line as "x:word" pairs. The snapshot therefore pins
// the break positions, justification and indentation the layout engine
// produces — not glyph shapes. Framebuffer-level goldens live with the desktop
// emulator.
//
// The KO 1.5 layout cannot serve as the baseline: upstream 1.6 replaced its
// token model (every CJK character is now its own token), so these snapshots
// were approved from the first 1.6 KO implementation. Regenerate deliberately:
//
//   CROSSPOINT_UPDATE_GOLDEN=1 ChapterHtmlSlimParserTest --gtest_filter='KoreanLayoutGolden*'
//
// and review the diff before committing it.

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "LayoutCapture.h"

#ifndef KOREAN_GOLDEN_DIR
#error "KOREAN_GOLDEN_DIR must point at test/chapter_html_slim_parser/golden"
#endif

namespace {

constexpr uint16_t kViewportWidth = 220;
constexpr uint16_t kViewportHeight = 800;

struct LayoutOptions {
  const char* name;
  bool characterWrap;
  bool paragraphIndent;
  bool extraParagraphSpacing;
};

// Representative setting combinations (§52), not the full product.
constexpr LayoutOptions kOptions[] = {
    {"ko-default", true, true, false},  // KO defaults: wrap + indent, no paragraph spacing
    {"ko-spaced", true, false, true},   // wrap only, upstream paragraph spacing
    {"ko-full", true, true, true},      // wrap + indent + paragraph spacing
    {"upstream", false, false, false},  // upstream behaviour with the KO flags off
};

struct Fixture {
  const char* name;
  const char* bodyHtml;
};

std::string longWord() {
  std::string word;
  for (int i = 0; i < 40; ++i) word += "아주";
  return word + "긴문자열";
}

std::string longParagraph() {
  static const char* const kPool[] = {"가", "나", "다", "라", "마", "바", "사",
                                      "아", "자", "차", "카", "타", "파", "하"};
  std::string text;
  size_t wordLength = 1;
  for (size_t emitted = 0; emitted < 500;) {
    if (!text.empty()) text += ' ';
    for (size_t i = 0; i < wordLength && emitted < 500; ++i, ++emitted) text += kPool[emitted % 14];
    wordLength = wordLength % 4 + 1;
  }
  return text;
}

std::vector<Fixture> fixtures() {
  static const std::string kLongWord = "<p>" + longWord() + "</p>";
  static const std::string kLongParagraph = "<p>" + longParagraph() + "</p>";
  return {
      {"basic", "<p>오늘은 날씨가 좋았다.</p><p>나는 집을 나와 천천히 길을 걸었다.</p>"},
      {"long-word", kLongWord.c_str()},
      {"mixed", "<p>CrossPoint에서 한글 EPUB 1.6.0을 테스트합니다.</p>"},
      {"hanja", "<p>小說 作家 韓國語 漢字</p>"},
      {"punctuation", "<p>“안녕하세요.” 그녀가 말했다.</p><p>(테스트), [테스트], &lt;테스트&gt;</p>"},
      {"css-text-indent", "<p style=\"text-indent: 3em\">문단 들여쓰기는 CSS 값이 우선한다.</p>"},
      {"long-paragraph", kLongParagraph.c_str()},
  };
}

class KoreanLayoutGoldenTest : public ::testing::TestWithParam<LayoutOptions> {
 protected:
  std::string filepath = (std::filesystem::temp_directory_path() / "korean-layout-golden.xhtml").generic_string();
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};

  void TearDown() override { std::remove(filepath.c_str()); }

  std::string layoutSnapshot(const Fixture& fixture, const LayoutOptions& options) {
    {
      std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
      out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<html><body>" << fixture.bodyHtml << "</body></html>\n";
    }
    capturedLayoutLines.clear();
    size_t pages = 0;
    ChapterHtmlSlimParser parser(
        nullptr, filepath, renderer, 0, 1.0f, options.extraParagraphSpacing, /*paragraphAlignment=*/0, kViewportWidth,
        kViewportHeight, /*hyphenationEnabled=*/false, /*focusReadingEnabled=*/false,
        [&pages](std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t) { pages++; },
        /*embeddedStyle=*/true, "", "", 0, {}, nullptr, &cssParser, options.characterWrap, options.paragraphIndent);
    EXPECT_TRUE(parser.parseAndBuildPages()) << fixture.name;

    std::ostringstream out;
    out << "## " << fixture.name << " (lines=" << capturedLayoutLines.size() << " pages=" << pages << ")\n";
    for (const auto& line : capturedLayoutLines) {
      for (size_t i = 0; i < line.words.size(); ++i) {
        if (i) out << '\t';
        out << line.x[i] << ':' << line.words[i];
      }
      out << '\n';
    }
    return out.str();
  }
};

TEST_P(KoreanLayoutGoldenTest, MatchesApprovedSnapshot) {
  const LayoutOptions& options = GetParam();
  std::string actual = "# viewport " + std::to_string(kViewportWidth) + "x" + std::to_string(kViewportHeight) +
                       " characterWrap=" + (options.characterWrap ? "1" : "0") +
                       " paragraphIndent=" + (options.paragraphIndent ? "1" : "0") +
                       " extraParagraphSpacing=" + (options.extraParagraphSpacing ? "1" : "0") + "\n";
  for (const Fixture& fixture : fixtures()) actual += layoutSnapshot(fixture, options);

  const std::filesystem::path goldenPath =
      std::filesystem::path(KOREAN_GOLDEN_DIR) / (std::string(options.name) + ".txt");
  if (std::getenv("CROSSPOINT_UPDATE_GOLDEN") != nullptr) {
    std::ofstream out(goldenPath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << "cannot write " << goldenPath.generic_string();
    out << actual;
    out.close();
    ASSERT_TRUE(out.good()) << "write failed " << goldenPath.generic_string();
    GTEST_SKIP() << "golden updated: " << goldenPath.generic_string();
  }
  std::ifstream in(goldenPath, std::ios::binary);
  ASSERT_TRUE(in.is_open()) << "missing golden " << goldenPath.generic_string()
                            << " (set CROSSPOINT_UPDATE_GOLDEN=1 to create it)";
  std::stringstream expected;
  expected << in.rdbuf();
  // A checkout with core.autocrlf rewrites the golden with CRLF; compare on LF.
  std::string expectedText = expected.str();
  expectedText.erase(std::remove(expectedText.begin(), expectedText.end(), ''), expectedText.end());
  EXPECT_EQ(actual, expectedText) << "layout drifted from " << goldenPath.generic_string();
}

INSTANTIATE_TEST_SUITE_P(Settings, KoreanLayoutGoldenTest, ::testing::ValuesIn(kOptions),
                         [](const ::testing::TestParamInfo<LayoutOptions>& info) {
                           std::string name = info.param.name;
                           for (char& c : name) {
                             if (c == '-') c = '_';
                           }
                           return name;
                         });

}  // namespace
