#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "LayoutCapture.h"

struct ChapterHtmlSlimParserTestAccess {
  static void resetText(ChapterHtmlSlimParser& parser) { parser.currentTextBlock = std::make_unique<ParsedText>(false); }
  static ParsedText& text(ChapterHtmlSlimParser& parser) { return *parser.currentTextBlock; }
  static uint8_t linkId(const ChapterHtmlSlimParser& parser) { return parser.currentFootnoteLinkId; }
  static const auto& footnotes(const ChapterHtmlSlimParser& parser) { return parser.pendingFootnotes; }
  static int partWordBufferIndex(const ChapterHtmlSlimParser& parser) { return parser.partWordBufferIndex; }
  static uint16_t& viewportWidth(ChapterHtmlSlimParser& parser) { return parser.viewportWidth; }
  static uint16_t& viewportHeight(ChapterHtmlSlimParser& parser) { return parser.viewportHeight; }
  static auto& tableRowCells(ChapterHtmlSlimParser& parser) { return parser.tableRowCells; }
  static const auto& tableCellLines(const ChapterHtmlSlimParser& parser) { return parser.tableCellLines; }
  static auto& completePageFn(ChapterHtmlSlimParser& parser) { return parser.completePageFn; }
  static std::unique_ptr<Page>& currentPage(ChapterHtmlSlimParser& parser) { return parser.currentPage; }
  static void finishTableRow(ChapterHtmlSlimParser& parser) { parser.finishTableRow(); }
  static void makePages(ChapterHtmlSlimParser& parser) { parser.makePages(); }
  static void start(ChapterHtmlSlimParser& parser, const XML_Char* name, const XML_Char** attributes) {
    ChapterHtmlSlimParser::startElement(&parser, name, attributes);
  }
  static void characters(ChapterHtmlSlimParser& parser, const XML_Char* data, int length) {
    ChapterHtmlSlimParser::characterData(&parser, data, length);
  }
  static void end(ChapterHtmlSlimParser& parser, const XML_Char* name) {
    ChapterHtmlSlimParser::endElement(&parser, name);
  }
};

struct ParsedTextTestAccess {
  static const auto& linkIds(const ParsedText& text) { return text.wordLinkIds; }
};

using Access = ChapterHtmlSlimParserTestAccess;

namespace {

class ChapterHtmlSlimParserTest : public ::testing::TestWithParam<const char*> {
 protected:
  std::string filepath = "unused.xhtml";
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};
  ChapterHtmlSlimParser parser{nullptr,
                               filepath,
                               renderer,
                               0,
                               1.0f,
                               false,
                               0,
                               static_cast<uint16_t>(renderer.getScreenWidth()),
                               static_cast<uint16_t>(renderer.getScreenHeight()),
                               false,
                               false,
                               {},
                               true,
                               "",
                               "",
                               0,
                               {},
                               nullptr,
                               &cssParser};

  void SetUp() override { Access::resetText(parser); }
};

TEST_F(ChapterHtmlSlimParserTest, RubySurvivesPartialParagraphExtraction) {
  ParsedText text(false);
  text.addWord("a", EpdFontFamily::REGULAR);
  text.addWord("b", EpdFontFamily::REGULAR);
  text.addWord("c", EpdFontFamily::REGULAR);
  text.setRubyForWordAt(2, "c");
  size_t lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 20,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        EXPECT_TRUE(line->getRubyTexts().empty());
      },
      false);
  EXPECT_EQ(lines, 1u);
  const size_t retainedWords = text.size();
  ASSERT_GT(retainedWords, 0u);
  ASSERT_LT(retainedWords, 3u);
  text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
    ++lines;
    ASSERT_EQ(line->getRubyTexts().size(), retainedWords);
    EXPECT_EQ(line->getRubyTexts().back(), "c");
    for (size_t i = 0; i + 1 < retainedWords; ++i) EXPECT_TRUE(line->getRubyTexts()[i].empty());
  });
  EXPECT_EQ(lines, 2u);
}

TEST_F(ChapterHtmlSlimParserTest, UnequalTableCellsAndRubySurvivePageBreaks) {
  Access::viewportWidth(parser) = 240;
  Access::viewportHeight(parser) = 32;
  Access::tableRowCells(parser).reserve(2);
  std::multiset<std::string> expected;
  for (int column = 0; column < 2; ++column) {
    auto cell = std::make_unique<ParsedText>(false);
    for (int index = 0; index < (column == 0 ? 30 : 3); ++index) {
      const auto word = std::string(column == 0 ? "left" : "right") + std::to_string(index);
      expected.insert(word);
      cell->addWord(word, EpdFontFamily::REGULAR);
    }
    if (column == 0) cell->setRubyGroupAt(0, 2, "reading");
    Access::tableRowCells(parser).push_back(std::move(cell));
  }
  std::multiset<std::string> actual;
  unsigned pages = 0;
  unsigned rubyLines = 0;
  auto inspect = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    ++pages;
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& line = static_cast<const PageLine&>(*element);
      const auto& block = *line.getBlock();
      ASSERT_TRUE(block.valid());
      EXPECT_LE(element->yPos + 16 + block.getRubyShift(12), Access::viewportHeight(parser));
      rubyLines += block.hasRuby();
      for (uint16_t word = 0; word < block.wordCount(); ++word) actual.insert(block.wordText(word));
    }
  };
  Access::completePageFn(parser) = inspect;
  Access::finishTableRow(parser);
  ASSERT_NE(Access::currentPage(parser), nullptr);
  inspect(std::move(Access::currentPage(parser)), 0, 0, 0);
  EXPECT_GT(pages, 2u);
  EXPECT_EQ(rubyLines, 1u);
  EXPECT_EQ(actual, expected);
  for (const auto& lines : Access::tableCellLines(parser)) EXPECT_TRUE(lines.empty());
}

TEST_F(ChapterHtmlSlimParserTest, PageImageDeserializeRejectsMissingImageBlock) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-missing-image-cache.bin";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    const int16_t coordinates[] = {0, 0};
    output.write(coordinates, sizeof(coordinates));
  }
  HalFile input;
  ASSERT_TRUE(input.open(path.c_str(), "rb"));
  EXPECT_EQ(PageImage::deserialize(input), nullptr);
}

TEST_P(ChapterHtmlSlimParserTest, KeepsCssVerticalAlignAndInternalLinkMetadata) {
  const char* verticalAlign = GetParam();
  const char* expectedHref = "#note-target";
  const XML_Char* attributes[] = {"href", expectedHref, "style", verticalAlign, nullptr};

  Access::start(parser, "a", attributes);
  const uint8_t linkId = Access::linkId(parser);
  ASSERT_NE(linkId, 0u);
  Access::characters(parser, "1", 1);
  Access::end(parser, "a");

  ASSERT_EQ(Access::text(parser).size(), 1u);
  const auto style = Access::text(parser).getWordStyleAt(0);
  const auto expectedStyle =
      std::string(verticalAlign).find("super") != std::string::npos ? EpdFontFamily::SUP : EpdFontFamily::SUB;
  EXPECT_NE(static_cast<uint8_t>(style) & static_cast<uint8_t>(expectedStyle), 0u);

  ASSERT_EQ(Access::footnotes(parser).size(), 1u);
  const FootnoteEntry& footnote = Access::footnotes(parser).front().second;
  EXPECT_STREQ(footnote.href, expectedHref);
  ASSERT_EQ(ParsedTextTestAccess::linkIds(Access::text(parser)).size(), 1u);
  EXPECT_EQ(ParsedTextTestAccess::linkIds(Access::text(parser)).front(), linkId);
  EXPECT_TRUE(Access::text(parser).linkTargetMatches(linkId, expectedHref));
}

INSTANTIATE_TEST_SUITE_P(CssVerticalAlign, ChapterHtmlSlimParserTest,
                         ::testing::Values("vertical-align: super", "vertical-align: sub"));

TEST_F(ChapterHtmlSlimParserTest, ParagraphWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  Access::start(parser, "p", attributes);
  Access::characters(parser, "[HIDDEN]", 8);

  ASSERT_EQ(Access::partWordBufferIndex(parser), 0);
}

TEST_F(ChapterHtmlSlimParserTest, HeaderWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  Access::start(parser, "h1", attributes);
  Access::characters(parser, "[HIDDEN]", 8);

  ASSERT_EQ(Access::partWordBufferIndex(parser), 0);
}

TEST_F(ChapterHtmlSlimParserTest, SpanWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  Access::start(parser, "p", nullptr);
  Access::characters(parser, "Before ", 7);
  Access::start(parser, "span", attributes);
  Access::characters(parser, "[HIDDEN]", 8);
  Access::end(parser, "span");
  Access::characters(parser, " After ", 7);

  ASSERT_EQ(Access::text(parser).size(), 2);
  ASSERT_EQ(Access::text(parser).wordAt(0), "Before");
  ASSERT_EQ(Access::text(parser).wordAt(1), "After");
}

TEST_F(ChapterHtmlSlimParserTest, DivWithHiddenAttributeContentShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  Access::start(parser, "div", attributes);
  Access::start(parser, "p", nullptr);
  Access::characters(parser, "[HIDDEN]", 8);

  ASSERT_EQ(Access::partWordBufferIndex(parser), 0);
}

TEST(KoreanLayout, SoftFlushNeverDropsHangulTokens) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  for (const size_t tokenCount : {320u, 500u, 1000u, 2500u}) {
    capturedLayoutLines.clear();
    ParsedText text(false, false, false, style, true, true);
    std::string expected;
    expected.reserve(tokenCount * 3);
    const auto append = [&](const size_t start, const size_t end) {
      for (size_t i = start; i < end; ++i) {
        const char* glyph = (i % 2 == 0) ? "가" : "나";
        text.addWord(glyph, EpdFontFamily::REGULAR, false, i != 0);
        expected += glyph;
      }
    };
    append(0, std::min<size_t>(320, tokenCount));
    text.layoutAndExtractLines(
        renderer, 0, 220, [](std::unique_ptr<TextBlock> line, uint32_t) { captureLayoutLine(*line); }, false);
    append(std::min<size_t>(320, tokenCount), tokenCount);
    text.layoutAndExtractLines(
        renderer, 0, 220, [](std::unique_ptr<TextBlock> line, uint32_t) { captureLayoutLine(*line); }, true);
    EXPECT_TRUE(text.isEmpty()) << tokenCount;

    std::string actual;
    ASSERT_GT(capturedLayoutLines.size(), 1u);
    for (size_t lineIndex = 0; lineIndex < capturedLayoutLines.size(); ++lineIndex) {
      const auto& line = capturedLayoutLines[lineIndex];
      ASSERT_EQ(line.words.size(), line.x.size());
      ASSERT_FALSE(line.x.empty());
      EXPECT_EQ(line.x.front(), lineIndex == 0 ? 8 : 0) << tokenCount;  // U+3000 indent, 8 px stub glyph
      for (size_t i = 0; i < line.words.size(); ++i) {
        actual += line.words[i];
        EXPECT_LE(line.x[i] + renderer.getTextWidth(0, line.words[i].c_str()), 220) << tokenCount;
      }
    }
    EXPECT_EQ(actual, expected) << tokenCount;
  }
}

}  // namespace

TEST(TextSpacingLayout, TrackingSeparatesCjkTokensAndScalesWordSpaces) {
  GfxRenderer renderer;
  for (bool hyphenation : {false, true}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(false, hyphenation, false, style);
    text.addWord("一二三", EpdFontFamily::REGULAR);
    text.addWord("四五", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(
        renderer, 0, 200,
        [&](std::unique_ptr<TextBlock> line, auto) {
          ++lines;
          ASSERT_EQ(line->wordCount(), 5);
          EXPECT_EQ(line->wordXpos(0), 0);
          EXPECT_EQ(line->wordXpos(1), 7);  // 8 px glyph, -1 px tracking
          EXPECT_EQ(line->wordXpos(2), 14);
          EXPECT_EQ(line->wordXpos(3), 28);  // 8 px glyph plus 150% of a 4 px space, no tracking
          EXPECT_EQ(line->wordXpos(4), 35);
        },
        true, -1, 150);
    EXPECT_EQ(lines, 1u);
  }
  EXPECT_EQ(renderer.getTextAdvanceX(0, "ab", EpdFontFamily::REGULAR), 16);
  EXPECT_EQ(renderer.getSpaceWidth(0, EpdFontFamily::REGULAR), 4);
}

TEST(TextSpacingLayout, WordSpacingChangesWrapThreshold) {
  GfxRenderer renderer;
  for (uint8_t percent : {50, 100, 125, 200}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(false, false, false, style);
    text.addWord("ab", EpdFontFamily::REGULAR);
    text.addWord("cd", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(renderer, 0, 36, [&](std::unique_ptr<TextBlock>, auto) { ++lines; }, true, 0, percent);
    EXPECT_EQ(lines, percent > 100 ? 2u : 1u);  // 16 + 16 + scaled 4 px space
  }
}

TEST(TextSpacingLayout, CachedPageRestoresSpacing) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textIndentDefined = true;
  ParsedText text(false, false, false, style);
  text.addWord("一二三", EpdFontFamily::REGULAR);
  text.addWord("四五", EpdFontFamily::REGULAR);
  const auto path = (std::filesystem::temp_directory_path() / "crosspoint-text-spacing.bin").string();
  unsigned lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 200,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        Page page;
        page.elements.push_back(std::make_unique<PageLine>(std::move(line), 4, 12));
        const auto* original = static_cast<const PageLine&>(*page.elements[0]).getBlock();
        {
          HalFile file;
          ASSERT_TRUE(file.open(path.c_str(), "wb"));
          ASSERT_TRUE(page.serialize(file));
        }
        HalFile file;
        ASSERT_TRUE(file.open(path.c_str(), "rb"));
        auto cachedPage = Page::deserialize(file);
        ASSERT_NE(cachedPage, nullptr);
        ASSERT_EQ(cachedPage->elements.size(), 1);
        const auto* cached = static_cast<const PageLine&>(*cachedPage->elements[0]).getBlock();
        ASSERT_NE(cached, nullptr);
        EXPECT_EQ(cached->getBlockStyle().characterSpacing, -2);
        ASSERT_EQ(cached->wordCount(), 5);
        EXPECT_EQ(cached->wordXpos(3) - cached->wordXpos(2), 10);  // 8 + half-width space
        EXPECT_EQ(file.position(), file.size());
        ASSERT_EQ(cached->wordCount(), original->wordCount());
        for (uint16_t i = 0; i < original->wordCount(); ++i) EXPECT_EQ(cached->wordXpos(i), original->wordXpos(i));
      },
      true, -2, 50);
  EXPECT_EQ(lines, 1u);
  std::filesystem::remove(path);
}

TEST_F(ChapterHtmlSlimParserTest, ParserAppliesTextSpacingToParagraphs) {
  parser.setTextSpacing(-1, 150);
  parser.beginParse();
  Access::start(parser, "p", nullptr);
  const std::string text = "\xe4\xb8\x80\xe4\xba\x8c\xe4\xb8\x89 \xe5\x9b\x9b\xe4\xba\x94";  // 一二三 四五
  Access::characters(parser, text.c_str(), static_cast<int>(text.size()));
  Access::end(parser, "p");
  Access::makePages(parser);
  ASSERT_NE(Access::currentPage(parser), nullptr);
  unsigned lines = 0;
  for (const auto& element : Access::currentPage(parser)->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& block = *static_cast<const PageLine&>(*element).getBlock();
    ++lines;
    ASSERT_EQ(block.wordCount(), 5);
    EXPECT_EQ(block.getBlockStyle().characterSpacing, -1);
    EXPECT_EQ(block.wordXpos(1) - block.wordXpos(0), 7);   // 8 px glyph, -1 px tracking
    EXPECT_EQ(block.wordXpos(3) - block.wordXpos(2), 14);  // glyph plus 150% of a 4 px space
  }
  EXPECT_EQ(lines, 1u);
}

TEST(KoreanLayout, HangulWordsStayWholeAndWrapAtSpaces) {
  GfxRenderer renderer;
  {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(false, false, false, style);
    text.addWord("가나다", EpdFontFamily::REGULAR);
    text.addWord("라마", EpdFontFamily::REGULAR);
    text.addWord("3개를", EpdFontFamily::REGULAR);
    text.addWord("iPhone을", EpdFontFamily::REGULAR);
    std::vector<std::vector<std::string>> lines;
    text.layoutAndExtractLines(renderer, 0, 60, [&](std::unique_ptr<TextBlock> line, auto) {
      auto& words = lines.emplace_back();
      for (uint16_t i = 0; i < line->wordCount(); ++i) words.emplace_back(line->wordText(i));
    });
    // 가나다 라마 is 24 + 4 + 16 px; adding 3개를 would need 72 px, and no break exists inside it.
    const std::vector<std::vector<std::string>> expected{{"가나다", "라마"}, {"3개를"}, {"iPhone을"}};
    EXPECT_EQ(lines, expected);
  }
}

TEST(KoreanLayout, JustifiedHangulStretchesOnlyWordSpaces) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, false, false, style);
  for (const char* word : {"가나", "다라", "마바", "사아"}) text.addWord(word, EpdFontFamily::REGULAR);
  unsigned lines = 0;
  text.layoutAndExtractLines(renderer, 0, 60, [&](std::unique_ptr<TextBlock> line, auto) {
    if (lines++ != 0) return;
    // 3 x 16 px words + 2 x 4 px spaces leave 4 px, split across the two spaces only.
    ASSERT_EQ(line->wordCount(), 3);
    EXPECT_EQ(line->wordXpos(0), 0);
    EXPECT_EQ(line->wordXpos(1), 22);
    EXPECT_EQ(line->wordXpos(2), 44);
  });
  EXPECT_EQ(lines, 2u);
}

TEST(KoreanLayout, HangulGluedAcrossInlineStyleIsUnbreakable) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, false, false, style);
  text.addWord("가나", EpdFontFamily::REGULAR);
  text.addWord("한국", EpdFontFamily::REGULAR);
  text.addWord("어", EpdFontFamily::BOLD, false, /*attachToPrevious=*/true);
  std::vector<std::vector<std::string>> lines;
  text.layoutAndExtractLines(renderer, 0, 40, [&](std::unique_ptr<TextBlock> line, auto) {
    auto& words = lines.emplace_back();
    for (uint16_t i = 0; i < line->wordCount(); ++i) words.emplace_back(line->wordText(i));
  });
  // 가나 한국 fits in 36 px, but 어 is glued to 한국, so the whole word moves down.
  const std::vector<std::vector<std::string>> expected{{"가나"}, {"한국", "어"}};
  EXPECT_EQ(lines, expected);
}
