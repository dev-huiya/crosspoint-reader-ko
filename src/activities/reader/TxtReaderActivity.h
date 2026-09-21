#pragma once

#include <Txt.h>

#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReaderActivity.h"
#include "TxtReaderMenuActivity.h"

class TxtReaderActivity final : public ReaderActivity {
  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;
  std::vector<std::string> currentPageLines;
  // Parallel to currentPageLines. A line "starts a paragraph" when it is the
  // first wrapped segment of a source line (it gets the paragraph indent and
  // the extra paragraph spacing above it); it "ends a paragraph" when it is
  // the last segment (never justified).
  std::vector<bool> currentPageLineStartsParagraph;
  std::vector<bool> currentPageLineEndsParagraph;
  int linesPerPage = 0;  // upper bound; the page is filled by height
  int viewportWidth = 0;
  int viewportHeight = 0;
  int lineHeight = 0;
  int paragraphIndentPx = 0;
  int paragraphSpacingPx = 0;
  bool initialized = false;

  // Cached settings for cache validation
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  // Korean layout settings shared with the EPUB reader (see ReaderRenderSpec).
  uint8_t cachedCharacterWrap = 0;
  uint8_t cachedParagraphIndent = 0;
  uint8_t cachedExtraParagraphSpacing = 0;
  float cachedLineCompression = 1.0f;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  // Reader-menu-driven options (ephemeral; not persisted to global settings)
  bool automaticPageTurnActive = false;
  unsigned long lastPageTurnTime = 0;
  unsigned long pageTurnDuration = 0;
  uint8_t currentPageTurnOption = 0;  // index into the menu's page-turn rates (0 = off)
  uint8_t currentPageJumpOption = 0;  // index into PAGE_JUMP_STEPS (0 = off)
  bool pendingScreenshot = false;

  void renderPage(GfxRenderer& renderer);
  void initializeReader(GfxRenderer& renderer);
  bool loadPageAtOffset(GfxRenderer& renderer, size_t offset, std::vector<std::string>& outLines,
                        std::vector<bool>* outStartsParagraph, std::vector<bool>* outEndsParagraph, size_t& nextOffset);
  // True when `offset` begins a source line (offset 0 or the byte before is
  // '\n'): the page's first line then starts a paragraph.
  bool isOffsetAtLineStart(size_t offset) const;
  void buildPageIndex(GfxRenderer& renderer);
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void loadProgress();
  void renderStatusBar() const;

  void openReaderMenu();
  void onReaderMenuConfirm(TxtReaderMenuActivity::MenuAction action);
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void jumpToPercent(int percent);

  bool loadBook() override;
  std::string getBookTitle() const override { return txt ? txt->getTitle() : ""; }
  std::string getBookCachePath() const override { return txt ? txt->getCachePath() : ""; }
  void renderBook() override;
  bool handleFormatInput() override;

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("TxtReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~TxtReaderActivity() override = default;

  void onExit() override;
  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
