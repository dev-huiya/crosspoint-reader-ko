#pragma once

#include <Txt.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReaderActivity.h"
#include "TxtPageIndex.h"
#include "TxtReaderMenuActivity.h"

class TxtReaderActivity final : public ReaderActivity {
  std::unique_ptr<Txt> txt;
  size_t fileSize = 0;

  // Offset-based navigation, as in KO 1.5: no whole-file pagination before
  // the first page. The page on screen is identified by its byte offset; where
  // it ends (= the next page's start) is a side effect of rendering it.
  // Previous-page navigation uses a bounded history stack, then the page
  // index, then a coarse backward scan.
  size_t currentOffset = 0;
  size_t currentEndOffset = 0;  // next page start, valid while renderedOffset == currentOffset
  size_t renderedOffset = SIZE_MAX;
  std::vector<size_t> backHistory;
  static constexpr size_t MAX_BACK_HISTORY = 256;
  bool atEnd = false;  // past the last page (end-of-book screen)

  // Page index built in the background from loop(); see indexTick(). Until it
  // reaches currentOffset the status bar shows an estimated page count. Before
  // the index knows any page, the first rendered page seeds the estimate.
  txt_index::PageIndex pageIndex;
  size_t estBytesPerPage = 0;
  int indexPagesSinceSave = 0;
  int indexFailures = 0;
  unsigned long indexRetryAfterMs = 0;
  // Upstream 1.6 progress files hold only a page number, which needs the
  // index to resolve; -1 when there is nothing to resolve.
  int pendingProgressPage = -1;
  static constexpr unsigned long INDEX_TICK_BUDGET_MS = 40;
  static constexpr int INDEX_PAGES_PER_TICK = 16;
  static constexpr int INDEX_PAGES_PER_SAVE = 200;
  static constexpr int INDEX_MAX_FAILURES = 5;

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
  bool progressLoaded = false;

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
  // Nearest source-line start at or before `offset`, so a page never opens on
  // a partial wrap segment.
  size_t snapToLineStart(size_t offset) const;
  // Start of the page that ends at `endOffset`, reconstructed by paginating
  // forward from an estimated earlier position. Needs the render lock.
  size_t findBackwardPageStart(GfxRenderer& renderer, size_t endOffset);
  // Move to `offset` as a fresh location: history and the end-of-book state
  // are dropped, and the page is snapped onto the index grid when known.
  void moveToOffset(size_t offset);
  void pushBackHistory(size_t offset);
  void indexTick();
  bool loadPageIndexCache();
  void savePageIndexCache();
  void saveProgress() const;
  void loadProgress();
  void renderStatusBar() const;

  int currentPageNumber() const;  // 1-based, exact or estimated
  int totalPageCount() const;
  bool pageCountEstimated() const;
  int progressPercent() const;

  void openReaderMenu();
  // Text settings or rotation changed: re-layout on the next render, keeping
  // the byte offset (and any partial index for the old layout on disk).
  void invalidateLayout();
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
  void loop() override;
  // Keep the main loop tight (and the CPU at full speed) while the page index
  // is still being built.
  bool skipLoopDelay() override { return initialized && !pageIndex.complete && indexFailures < INDEX_MAX_FAILURES; }
  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
