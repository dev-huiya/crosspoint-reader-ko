// Korean layout regression tests that drive the real ChapterHtmlSlimParser
// (not ParsedText directly), so the soft-flush trigger in characterData(), the
// deferred last line, paragraph indent and the character-wrap gap bound are all
// exercised on the same path the firmware uses.
//
// The GfxRenderer stub measures 8 px per UTF-8 byte, so a Hangul syllable is
// 24 px wide, ASCII 8 px, a space 4 px, and U+3000 (the paragraph indent) 24 px.

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "LayoutCapture.h"
#include "ParserTestAccess.h"

namespace {

constexpr uint16_t kViewportWidth = 220;
constexpr uint16_t kViewportHeight = 800;
constexpr int kSpaceWidth = 4;    // stub getSpaceWidth()
constexpr int kIndentWidth = 24;  // U+3000 under the stub metrics

// Repeatable Korean text: `syllables` Hangul syllables grouped into 1..4-syllable
// words. Returns the text with spaces, which is what the XHTML would contain.
std::string makeKoreanText(const size_t syllables) {
  static const char* const kPool[] = {"가", "나", "다", "라", "마", "바", "사",
                                      "아", "자", "차", "카", "타", "파", "하"};
  constexpr size_t kPoolSize = sizeof(kPool) / sizeof(kPool[0]);
  std::string text;
  size_t emitted = 0;
  size_t wordLength = 1;
  while (emitted < syllables) {
    if (!text.empty()) text += ' ';
    for (size_t i = 0; i < wordLength && emitted < syllables; ++i, ++emitted) {
      text += kPool[emitted % kPoolSize];
    }
    wordLength = wordLength % 4 + 1;
  }
  return text;
}

// Text goes into an XHTML file, so the three XML metacharacters are escaped;
// Expat decodes them back before characterData() sees the text.
std::string xmlEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (c == '&')
      out += "&amp;";
    else if (c == '<')
      out += "&lt;";
    else if (c == '>')
      out += "&gt;";
    else
      out += c;
  }
  return out;
}

std::string withoutSpaces(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (c != ' ') out += c;
  }
  return out;
}

// Split at spaces into chunks of roughly `targetBytes`, mimicking Expat handing
// text to characterData() in several calls. Never splits a UTF-8 sequence.
std::vector<std::string> chunkAtSpaces(const std::string& text, const size_t targetBytes) {
  std::vector<std::string> chunks;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = start + targetBytes;
    if (end >= text.size()) {
      chunks.push_back(text.substr(start));
      break;
    }
    const size_t space = text.find(' ', end);
    if (space == std::string::npos) {
      chunks.push_back(text.substr(start));
      break;
    }
    chunks.push_back(text.substr(start, space + 1 - start));  // keep the space
    start = space + 1;
  }
  return chunks;
}

struct ParagraphRun {
  size_t pagesCompleted = 0;
  size_t linesBeforeParagraphEnd = 0;
  std::string rendered;
};

class KoreanParserLayoutTest : public ::testing::TestWithParam<bool> {
 protected:
  // The HalStorage stub opens paths with fopen(), so a temp file stands in for
  // the SD card. Each run rewrites it.
  std::string filepath = (std::filesystem::temp_directory_path() / "korean-parser-layout.xhtml").generic_string();
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};

  void TearDown() override { std::remove(filepath.c_str()); }

  void writeParagraphFile(const std::string& text) const {
    std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<html><body><p>" << xmlEscape(text) << "</p></body></html>\n";
  }

  // chunkBytes == 0: let Expat drive the parser (parseAndBuildPages). Otherwise
  // feed the same paragraph through the callbacks by hand in chunks of roughly
  // chunkBytes so the soft-flush trigger sees different characterData() sizes.
  ParagraphRun runParagraph(const std::string& text, const bool characterWrap, const bool paragraphIndent,
                            const bool extraParagraphSpacing = false, const size_t chunkBytes = 0) {
    capturedLayoutLines.clear();
    writeParagraphFile(text);
    ParagraphRun run;
    ChapterHtmlSlimParser parser(
        nullptr, filepath, renderer, 0, 1.0f, extraParagraphSpacing, /*paragraphAlignment=*/0, kViewportWidth,
        kViewportHeight, /*hyphenationEnabled=*/false, /*focusReadingEnabled=*/false,
        [&run](std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t) { run.pagesCompleted++; },
        /*embeddedStyle=*/GetParam(), "", "", 0, {}, nullptr, &cssParser, characterWrap, paragraphIndent);
    if (chunkBytes == 0) {
      EXPECT_TRUE(parser.parseAndBuildPages());
    } else {
      EXPECT_TRUE(ChapterHtmlSlimParserTestAccess::beginParse(parser));
      const XML_Char* noAttributes[] = {nullptr};
      ChapterHtmlSlimParserTestAccess::start(parser, "body", noAttributes);
      ChapterHtmlSlimParserTestAccess::start(parser, "p", noAttributes);
      for (const std::string& chunk : chunkAtSpaces(text, chunkBytes)) {
        ChapterHtmlSlimParserTestAccess::characters(parser, chunk.data(), static_cast<int>(chunk.size()));
      }
      run.linesBeforeParagraphEnd = capturedLayoutLines.size();
      ChapterHtmlSlimParserTestAccess::end(parser, "p");
      ChapterHtmlSlimParserTestAccess::end(parser, "body");
      EXPECT_TRUE(ChapterHtmlSlimParserTestAccess::finishParse(parser));
    }
    for (const auto& line : capturedLayoutLines) {
      for (const auto& word : line.words) run.rendered += word;
    }
    return run;
  }

  // expectedFirstX: the KO indent (24), upstream's implicit three spaces (12)
  // when paragraph spacing is off, or 0.
  void expectLinesFitAndIndentOnce(const int expectedFirstX) const {
    ASSERT_FALSE(capturedLayoutLines.empty());
    for (size_t lineIndex = 0; lineIndex < capturedLayoutLines.size(); ++lineIndex) {
      const auto& line = capturedLayoutLines[lineIndex];
      ASSERT_EQ(line.words.size(), line.x.size());
      ASSERT_FALSE(line.words.empty()) << "empty line " << lineIndex;
      // Indent exactly once, on the first line of the paragraph — even when the
      // paragraph was soft-flushed several times on the way.
      EXPECT_EQ(line.x.front(), lineIndex == 0 ? expectedFirstX : 0) << "line " << lineIndex;
      for (size_t i = 0; i < line.words.size(); ++i) {
        EXPECT_LE(line.x[i] + renderer.getTextWidth(0, line.words[i].c_str()), kViewportWidth)
            << "line " << lineIndex << " word " << i;
        if (i > 0) EXPECT_GE(line.x[i], line.x[i - 1]) << "line " << lineIndex << " word " << i;
      }
    }
  }

  // Largest gap between consecutive words on any line except the last one.
  int maxJustifiedGap() const {
    int maxGap = 0;
    for (size_t lineIndex = 0; lineIndex + 1 < capturedLayoutLines.size(); ++lineIndex) {
      const auto& line = capturedLayoutLines[lineIndex];
      for (size_t i = 1; i < line.words.size(); ++i) {
        const int gap = line.x[i] - (line.x[i - 1] + renderer.getTextWidth(0, line.words[i - 1].c_str()));
        if (gap > maxGap) maxGap = gap;
      }
    }
    return maxGap;
  }
};

// §11: a long paragraph crosses the soft-flush threshold (320 tokens with CSS,
// 750 without) one or more times; no syllable may be dropped or duplicated and
// the order must be preserved.
TEST_P(KoreanParserLayoutTest, SoftFlushPreservesEveryHangulSyllable) {
  const size_t threshold = GetParam() ? 320 : 750;
  // +10: the parser only counts words already flushed from its word buffer, so
  // the paragraph must exceed the threshold by more than one trailing word.
  for (const size_t syllables : {threshold + 10, size_t{500}, size_t{1000}, size_t{3000}}) {
    const std::string text = makeKoreanText(syllables);
    const ParagraphRun run = runParagraph(text, /*characterWrap=*/true, /*paragraphIndent=*/true, false, 90);
    EXPECT_EQ(run.rendered, withoutSpaces(text)) << syllables << " syllables";
    if (syllables > threshold) {
      // The threshold was crossed inside characterData(), so lines were emitted
      // before </p> — proof that the soft-flush path ran, not just the final flush.
      EXPECT_GT(run.linesBeforeParagraphEnd, 0u) << syllables << " syllables";
    }
    expectLinesFitAndIndentOnce(kIndentWidth);
  }
}

// Soft flushes must not depend on how Expat chunks the text.
TEST_P(KoreanParserLayoutTest, ChunkSizeDoesNotChangeTheRenderedText) {
  const std::string text = makeKoreanText(1200);
  const std::string expected = withoutSpaces(text);
  for (const size_t chunkBytes : {size_t{0}, size_t{30}, size_t{90}, size_t{400}, text.size()}) {
    const ParagraphRun run = runParagraph(text, true, true, false, chunkBytes);
    EXPECT_EQ(run.rendered, expected) << "chunk " << chunkBytes;
    expectLinesFitAndIndentOnce(kIndentWidth);
  }
}

// §10: with character wrap the justified word gap is capped at 1.5x a space.
// Hangul alone never leaves much spare width (it wraps per syllable), so pair
// each syllable with an unbreakable 21-character Latin word: "가 <word>" is
// 196 px on a 220 px line and the next syllable does not fit, leaving 24 px for
// the single gap — four times the cap unless the cap is enforced.
TEST_P(KoreanParserLayoutTest, CharacterWrapCapsJustifiedGaps) {
  std::string text;
  for (int i = 0; i < 200; ++i) text += (i ? " 가 Internationalizations" : "가 Internationalizations");
  runParagraph(text, /*characterWrap=*/true, /*paragraphIndent=*/false);
  expectLinesFitAndIndentOnce(kSpaceWidth * 3);  // upstream implicit indent stays
  EXPECT_LE(maxJustifiedGap(), kSpaceWidth + kSpaceWidth / 2);

  // Control: without the KO flag upstream distributes the whole spare width.
  runParagraph(text, /*characterWrap=*/false, /*paragraphIndent=*/false);
  EXPECT_GT(maxJustifiedGap(), kSpaceWidth + kSpaceWidth / 2);
}

// Mixed script, Hanja and punctuation go through the same path unchanged.
TEST_P(KoreanParserLayoutTest, MixedScriptAndPunctuationSurvive) {
  std::string text;
  for (int i = 0; i < 40; ++i) {
    text +=
        "CrossPoint에서 한글 EPUB 1.6.0을 테스트합니다. 小說 作家 韓國語 漢字 “안녕하세요.” 그녀가 말했다. "
        "(테스트), [테스트], <테스트> ";
  }
  text += makeKoreanText(200);
  const ParagraphRun run = runParagraph(text, true, true);
  EXPECT_EQ(run.rendered, withoutSpaces(text));
  expectLinesFitAndIndentOnce(kIndentWidth);
}

// §12: the explicit indent replaces the implicit three-space indent that
// upstream applies when paragraph spacing is off; neither doubles the other.
TEST_P(KoreanParserLayoutTest, ParagraphIndentAppliesOnceRegardlessOfParagraphSpacing) {
  const std::string text = makeKoreanText(700);
  for (const bool extraSpacing : {false, true}) {
    runParagraph(text, true, /*paragraphIndent=*/true, extraSpacing);
    expectLinesFitAndIndentOnce(kIndentWidth);
    ASSERT_GT(capturedLayoutLines.size(), 1u);
    EXPECT_EQ(capturedLayoutLines[0].x.front(), kIndentWidth) << "extraSpacing " << extraSpacing;
    EXPECT_EQ(capturedLayoutLines[1].x.front(), 0) << "extraSpacing " << extraSpacing;
  }
  // Without the KO indent the upstream behaviour is untouched: three spaces of
  // implicit indent when paragraph spacing is off, none when it is on.
  runParagraph(text, true, /*paragraphIndent=*/false, /*extraParagraphSpacing=*/false);
  EXPECT_EQ(capturedLayoutLines[0].x.front(), kSpaceWidth * 3);
  runParagraph(text, true, /*paragraphIndent=*/false, /*extraParagraphSpacing=*/true);
  EXPECT_EQ(capturedLayoutLines[0].x.front(), 0);
}

INSTANTIATE_TEST_SUITE_P(EmbeddedStyle, KoreanParserLayoutTest, ::testing::Values(true, false),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? "CssThreshold320" : "PlainThreshold750";
                         });

}  // namespace
