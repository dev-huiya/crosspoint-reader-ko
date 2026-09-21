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
};
