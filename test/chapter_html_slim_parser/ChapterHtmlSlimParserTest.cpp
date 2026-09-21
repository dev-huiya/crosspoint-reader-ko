#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <array>

#include "Epub/parsers/ChapterHtmlSlimParser.h"

struct ChapterHtmlSlimParserTestAccess {
  static void resetText(ChapterHtmlSlimParser& parser) { parser.currentTextBlock = std::make_unique<ParsedText>(false); }
  static ParsedText& text(ChapterHtmlSlimParser& parser) { return *parser.currentTextBlock; }
  static uint8_t linkId(const ChapterHtmlSlimParser& parser) { return parser.currentFootnoteLinkId; }
  static const auto& footnotes(const ChapterHtmlSlimParser& parser) { return parser.pendingFootnotes; }
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

  void SetUp() override { ChapterHtmlSlimParserTestAccess::resetText(parser); }
};

TEST_P(ChapterHtmlSlimParserTest, KeepsCssVerticalAlignAndInternalLinkMetadata) {
  const char* verticalAlign = GetParam();
  const char* expectedHref = "#note-target";
  const XML_Char* attributes[] = {"href", expectedHref, "style", verticalAlign, nullptr};

  ChapterHtmlSlimParserTestAccess::start(parser, "a", attributes);
  const uint8_t linkId = ChapterHtmlSlimParserTestAccess::linkId(parser);
  ASSERT_NE(linkId, 0u);
  ChapterHtmlSlimParserTestAccess::characters(parser, "1", 1);
  ChapterHtmlSlimParserTestAccess::end(parser, "a");

  const ParsedText& text = ChapterHtmlSlimParserTestAccess::text(parser);
  ASSERT_EQ(text.size(), 1u);
  const auto style = text.getWordStyleAt(0);
  const auto expectedStyle =
      std::string(verticalAlign).find("super") != std::string::npos ? EpdFontFamily::SUP : EpdFontFamily::SUB;
  EXPECT_NE(static_cast<uint8_t>(style) & static_cast<uint8_t>(expectedStyle), 0u);

  const auto& footnotes = ChapterHtmlSlimParserTestAccess::footnotes(parser);
  ASSERT_EQ(footnotes.size(), 1u);
  const FootnoteEntry& footnote = footnotes.front().second;
  EXPECT_STREQ(footnote.href, expectedHref);
  const auto& linkIds = ParsedTextTestAccess::linkIds(text);
  ASSERT_EQ(linkIds.size(), 1u);
  EXPECT_EQ(linkIds.front(), linkId);
  EXPECT_TRUE(text.linkTargetMatches(linkId, expectedHref));
}

INSTANTIATE_TEST_SUITE_P(CssVerticalAlign, ChapterHtmlSlimParserTest,
                         ::testing::Values("vertical-align: super", "vertical-align: sub"));


}  // namespace
