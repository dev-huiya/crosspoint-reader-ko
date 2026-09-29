#pragma once

// Friend access into the parser and ParsedText for tests. Both classes declare
// these structs as friends, so tests never need `#define private public`.

#include "Epub/parsers/ChapterHtmlSlimParser.h"

struct ChapterHtmlSlimParserTestAccess {
  static void resetText(ChapterHtmlSlimParser& parser) {
    parser.currentTextBlock = std::make_unique<ParsedText>(false);
  }
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
  // Lifecycle for tests that feed callbacks by hand instead of running Expat.
  static bool beginParse(ChapterHtmlSlimParser& parser) { return parser.beginParse(); }
  static bool finishParse(ChapterHtmlSlimParser& parser) { return parser.finishParse(); }
};

struct ParsedTextTestAccess {
  static const auto& linkIds(const ParsedText& text) { return text.wordLinkIds; }
  static std::string_view wordAt(const ParsedText& text, const size_t i) { return text.wordAt(i); }
};
