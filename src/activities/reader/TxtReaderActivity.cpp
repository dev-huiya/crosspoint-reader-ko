#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Serialization.h>
#include <Utf8.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "ProgressFile.h"
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "TxtLineBreak.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 5;          // v5: resumable partial index (built in the background)

// KO 1.5 progress file ("TXTP" v2): magic, version, file size, nine layout
// fields, then the byte offset as 8 bytes. Only the offset is used.
constexpr uint32_t LEGACY_PROGRESS_MAGIC = 0x54585450;
constexpr uint8_t LEGACY_PROGRESS_VERSION = 2;
constexpr size_t LEGACY_PROGRESS_SIZE = 41;
constexpr size_t LEGACY_PROGRESS_FILE_SIZE_POS = 5;
constexpr size_t LEGACY_PROGRESS_OFFSET_POS = 33;

// Same rates as the EPUB reader's auto page turn (pages per minute, index 0 = off).
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
// Long-press page jump sizes offered by the reader menu (index 0 = off).
constexpr int PAGE_JUMP_STEPS[] = {0, 10, 20, 50, 100};
// Held duration above which a page-button release jumps instead of turning.
constexpr unsigned long PAGE_JUMP_HOLD_MS = ReaderUtils::SKIP_HOLD_MS;

int clampPercent(const int percent) { return std::max(0, std::min(100, percent)); }

uint32_t readU32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

}  // namespace

bool TxtReaderActivity::loadBook() {
  txt = makeUniqueNoThrow<Txt>(bookPath, "/.crosspoint");
  if (!txt) {
    LOG_ERR("TRS", "Failed to allocate TXT object");
    return false;
  }
  if (!txt->load()) {
    LOG_ERR("TRS", "Failed to load TXT");
    return false;
  }
  txt->setupCacheDir();
  (void)txt->generateCoverBmp(true);
  fileSize = txt->getFileSize();
  return true;
}

void TxtReaderActivity::initializeReader(GfxRenderer& renderer) {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;
  cachedCharacterWrap = SETTINGS.characterWrap;
  cachedParagraphIndent = SETTINGS.paragraphIndent;
  cachedExtraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  cachedLineCompression = SETTINGS.getReaderLineCompression();

  // Calculate viewport dimensions
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  lineHeight = std::max(1, renderer.getLineHeight(cachedFontId, cachedLineCompression));

  // Korean paragraph indent: one ideographic space (U+3000), as the EPUB reader
  // uses; a font without that glyph falls back to a line height. Paragraph
  // spacing is half a line above every paragraph but the page's first.
  if (cachedParagraphIndent) {
    paragraphIndentPx = renderer.getTextWidth(cachedFontId, "\xE3\x80\x80");
    if (paragraphIndentPx <= 0) paragraphIndentPx = lineHeight;
  } else {
    paragraphIndentPx = 0;
  }
  paragraphSpacingPx = cachedExtraParagraphSpacing ? lineHeight / 2 : 0;

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  LOG_DBG("TRS", "Viewport: %dx%d, line %dpx, up to %d lines per page, file %zu bytes", viewportWidth, viewportHeight,
          lineHeight, linesPerPage, fileSize);

  // The page index belongs to one layout. A cached index for this layout is
  // picked up whole or partial; otherwise indexTick() builds it from the
  // start while the user reads. The first page is drawn either way.
  pageIndex.reset();
  indexPagesSinceSave = 0;
  indexFailures = 0;
  indexRetryAfterMs = 0;
  estBytesPerPage = 0;
  backHistory.clear();
  renderedOffset = SIZE_MAX;
  atEnd = false;
  loadPageIndexCache();

  if (!progressLoaded) {
    loadProgress();
    progressLoaded = true;
  } else {
    // A re-layout (text settings, rotation) keeps the reading position: the
    // byte offset is layout-independent. Snap to a line start so the page
    // does not open on a partial wrap segment.
    moveToOffset(snapToLineStart(currentOffset));
  }

  initialized = true;
}

bool TxtReaderActivity::loadPageAtOffset(GfxRenderer& renderer, size_t offset, std::vector<std::string>& outLines,
                                         std::vector<bool>* outStartsParagraph, std::vector<bool>* outEndsParagraph,
                                         size_t& nextOffset) {
  outLines.clear();
  if (outStartsParagraph) outStartsParagraph->clear();
  if (outEndsParagraph) outEndsParagraph->clear();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file. One byte before the page comes along in the same
  // read: it tells whether the page starts a source line, so the index
  // builder does not pay a second file open per page for that.
  const size_t lead = offset > 0 ? 1 : 0;
  const size_t readStart = offset - lead;
  const size_t readLen = std::min(CHUNK_SIZE + lead, fileSize - readStart);
  auto* buffer = static_cast<uint8_t*>(malloc(readLen + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", readLen);
    return false;
  }

  if (!txt->readContent(buffer, readStart, readLen)) {
    free(buffer);
    return false;
  }
  buffer[readLen] = '\0';

  // A page that starts mid-line continues the previous page's paragraph:
  // no indent, no spacing above.
  const bool firstLineIsParagraphStart = lead == 0 || buffer[0] == '\n';
  uint8_t* chunk = buffer + lead;
  const size_t chunkSize = readLen - lead;

  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(chunk), /*styleMask=*/0x01);
  }

  // The page is filled by height, not by line count: paragraph spacing makes
  // lines unequal. linesPerPage only bounds the loop.
  int accumulatedY = 0;
  bool isFirstSourceLineOnPage = true;
  auto tryAddLine = [&](const std::string& segment, const bool startsParagraph, const bool endsParagraph,
                        const bool spacingAbove) -> bool {
    const int pixelHeight = lineHeight + (spacingAbove ? paragraphSpacingPx : 0);
    if (accumulatedY + pixelHeight > viewportHeight && !outLines.empty()) return false;
    outLines.push_back(segment);
    if (outStartsParagraph) outStartsParagraph->push_back(startsParagraph);
    if (outEndsParagraph) outEndsParagraph->push_back(endsParagraph);
    accumulatedY += pixelHeight;
    return true;
  };

  // Parse lines from buffer
  size_t pos = 0;

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && chunk[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    size_t lineContentLen = lineEnd - pos;
    bool hasCR = (lineContentLen > 0 && chunk[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    std::string line(reinterpret_cast<char*>(chunk + pos), displayLen);
    size_t lineBytePos = 0;

    const bool sourceLineStartsParagraph = isFirstSourceLineOnPage ? firstLineIsParagraphStart : true;
    // Spacing goes above a paragraph that is not the page's first line.
    const bool spacingAbove = paragraphSpacingPx > 0 && sourceLineStartsParagraph && !outLines.empty();
    const int firstSegmentIndent = sourceLineStartsParagraph ? paragraphIndentPx : 0;
    bool isFirstSegment = true;
    bool pageFull = false;

    do {
      const bool startsParagraph = isFirstSegment && sourceLineStartsParagraph;
      const bool spacing = isFirstSegment && spacingAbove;

      if (line.empty()) {
        if (!tryAddLine("", startsParagraph, true, spacing)) pageFull = true;
        break;
      }

      const int effectiveWidth = std::max(1, viewportWidth - (isFirstSegment ? firstSegmentIndent : 0));
      size_t breakPos =
          txt_layout::findBreakPosition(line, effectiveWidth, cachedCharacterWrap != 0, [&](const std::string& prefix) {
            return renderer.getTextAdvanceX(cachedFontId, prefix.c_str(), EpdFontFamily::REGULAR);
          });

      if (breakPos >= line.length()) {
        if (!tryAddLine(line, startsParagraph, true, spacing)) {
          pageFull = true;
          break;
        }
        lineBytePos = displayLen;
        line.clear();
        break;
      }

      if (breakPos == 0) {
        breakPos = 1;
      }

      if (!tryAddLine(line.substr(0, breakPos), startsParagraph, false, spacing)) {
        pageFull = true;
        break;
      }
      isFirstSegment = false;

      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      line = line.substr(skipChars);
    } while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage);

    isFirstSourceLineOnPage = false;

    if (line.empty() && !pageFull) {
      pos = lineEnd + 1;
    } else {
      // Page full mid-line (or on the empty line itself): resume here.
      pos = pos + lineBytePos;
      break;
    }
  }

  if (pos == 0 && !outLines.empty()) {
    pos = 1;
  }

  nextOffset = offset + pos;
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);
  return !outLines.empty();
}

size_t TxtReaderActivity::snapToLineStart(size_t offset) const {
  if (offset == 0 || offset >= fileSize) return offset;

  // Look back for the nearest '\n' + 1. Read at most one chunk worth.
  const size_t scanLen = std::min(CHUNK_SIZE, offset);
  const size_t scanStart = offset - scanLen;

  auto* buf = static_cast<uint8_t*>(malloc(scanLen));
  if (!buf) return offset;
  if (!txt->readContent(buf, scanStart, scanLen)) {
    free(buf);
    return offset;
  }

  size_t snapped = scanStart;
  for (size_t i = scanLen; i > 0; i--) {
    if (buf[i - 1] == '\n') {
      snapped = scanStart + i;  // position right after '\n'
      break;
    }
  }
  free(buf);
  return snapped;
}

size_t TxtReaderActivity::findBackwardPageStart(GfxRenderer& renderer, const size_t endOffset) {
  // Neither history nor the index knows this page: reconstruct it by walking
  // forward from a guessed earlier position until we reach endOffset. The
  // window comes from the bytes-per-page seen so far.
  const size_t perPage = pageIndex.bytesPerPage(estBytesPerPage > 0 ? estBytesPerPage : 2048);
  const size_t windowBytes = perPage * 2;
  size_t scanStart = endOffset > windowBytes ? endOffset - windowBytes : 0;
  scanStart = snapToLineStart(scanStart);

  std::vector<std::string> lines;
  size_t cursor = scanStart;
  size_t lastStart = scanStart;

  // Walk forward page-by-page. The last page-start at or before endOffset
  // is our answer. loadPageAtOffset decides the paragraph state of each
  // page's first line from the byte before it, exactly as forward rendering
  // does, so the reconstructed boundaries match the pages the user turned.
  while (cursor < endOffset) {
    size_t next = cursor;
    if (!loadPageAtOffset(renderer, cursor, lines, nullptr, nullptr, next)) {
      break;
    }
    if (next <= cursor) break;
    if (next >= endOffset) {
      lastStart = cursor;
      break;
    }
    lastStart = cursor;
    cursor = next;
  }
  return lastStart;
}

void TxtReaderActivity::moveToOffset(size_t offset) {
  if (fileSize == 0) return;
  if (offset >= fileSize) offset = fileSize - 1;
  // Onto the index grid when the index has reached this position, so the
  // page number is exact and Back walks the same pages as the index.
  const int page = pageIndex.pageContaining(static_cast<uint32_t>(offset));
  if (page >= 0) offset = pageIndex.starts[page];
  backHistory.clear();
  atEnd = false;
  currentOffset = offset;
}

void TxtReaderActivity::pushBackHistory(const size_t offset) {
  // Capped so it does not grow forever on very long reads.
  if (backHistory.size() >= MAX_BACK_HISTORY) {
    backHistory.erase(backHistory.begin(), backHistory.begin() + (MAX_BACK_HISTORY / 4));
  }
  backHistory.push_back(offset);
}

// --- Background page index ---------------------------------------------------

// Runs on the main task from loop(). Each tick lays out a few pages under the
// render lock (the renderer and its SD font caches are not thread-safe), as
// the EPUB reader's deferred section build does, and stops as soon as a
// render is pending so page turns are not delayed by more than one tick.
void TxtReaderActivity::indexTick() {
  if (!initialized || !txt || fileSize == 0 || pageIndex.complete || indexFailures >= INDEX_MAX_FAILURES) return;
  if (indexRetryAfterMs != 0 && static_cast<long>(millis() - indexRetryAfterMs) < 0) return;
  if (RenderLock::peek()) return;

  RenderLock lock;
  if (!initialized || pageIndex.complete) return;

  const unsigned long start = millis();
  int pages = 0;
  std::vector<std::string> lines;
  while (!pageIndex.complete && pages < INDEX_PAGES_PER_TICK && millis() - start < INDEX_TICK_BUDGET_MS) {
    const size_t pageStart = pageIndex.starts.back();
    size_t next = pageStart;
    if (!loadPageAtOffset(renderer, pageStart, lines, nullptr, nullptr, next) || next <= pageStart) {
      // Out of memory or an SD read failure: back off, and give up after a
      // few in a row (the status bar then keeps its estimate).
      indexFailures++;
      indexRetryAfterMs = millis() + 1000;
      LOG_ERR("TRS", "Page index stalled at %zu (%d)", pageStart, indexFailures);
      return;
    }
    indexFailures = 0;
    indexRetryAfterMs = 0;
    pageIndex.addPageEnd(static_cast<uint32_t>(next), static_cast<uint32_t>(fileSize));
    pages++;
    indexPagesSinceSave++;
  }

  // An upstream 1.6 progress file names a page, not an offset; go there once
  // the index knows the page, unless the user has moved on.
  if (pendingProgressPage >= 0 &&
      (pageIndex.complete || pendingProgressPage < static_cast<int>(pageIndex.starts.size()))) {
    const int page = std::min(pendingProgressPage, static_cast<int>(pageIndex.starts.size()) - 1);
    pendingProgressPage = -1;
    if (currentOffset == 0 && backHistory.empty() && page > 0) {
      currentOffset = pageIndex.starts[page];
      requestUpdate();
    }
  }

  if (pageIndex.complete) {
    LOG_DBG("TRS", "Page index complete: %d pages", static_cast<int>(pageIndex.starts.size()));
    savePageIndexCache();
  } else if (indexPagesSinceSave >= INDEX_PAGES_PER_SAVE) {
    savePageIndexCache();
  }
}

void TxtReaderActivity::loop() {
  indexTick();
  ReaderActivity::loop();
}

// --- Rendering -----------------------------------------------------------------

void TxtReaderActivity::renderBook() {
  if (!txt) {
    return;
  }

  if (!initialized) {
    initializeReader(renderer);
  }

  if (fileSize == 0) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Recover from an out-of-range offset (a settings change can shrink the
  // reachable positions): fall back to the last actual page start.
  if (currentOffset >= fileSize) {
    currentOffset = findBackwardPageStart(renderer, fileSize);
  }

  // Load current page content and remember where the next page starts.
  size_t nextOffset = currentOffset;
  currentPageLines.clear();
  loadPageAtOffset(renderer, currentOffset, currentPageLines, &currentPageLineStartsParagraph,
                   &currentPageLineEndsParagraph, nextOffset);
  currentEndOffset = nextOffset;
  renderedOffset = currentOffset;

  // Seed the page-count estimate from the first page rendered in this layout
  // so the status bar is usable before the index knows any page.
  if (estBytesPerPage == 0 && nextOffset > currentOffset) {
    estBytesPerPage = nextOffset - currentOffset;
  }

  renderer.clearScreen();
  renderPage(renderer);

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }

  // Save progress
  saveProgress();
}

void TxtReaderActivity::renderPage(GfxRenderer& renderer) {
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    const size_t lineCount = currentPageLines.size();
    for (size_t i = 0; i < lineCount; ++i) {
      const auto& line = currentPageLines[i];
      const bool startsParagraph =
          i < currentPageLineStartsParagraph.size() ? currentPageLineStartsParagraph[i] : false;
      const bool endsParagraph = i < currentPageLineEndsParagraph.size() ? currentPageLineEndsParagraph[i] : true;
      if (startsParagraph && i > 0) y += paragraphSpacingPx;

      if (!line.empty()) {
        const int indent = startsParagraph ? paragraphIndentPx : 0;
        int x = cachedOrientedMarginLeft + indent;
        const int effectiveContentWidth = contentWidth - indent;
        const bool lineIsRtl = BidiUtils::startsWithRtl(line.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        const int textWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR);

        // Apply text alignment
        int8_t letterSpacing = 0;
        switch (effectiveAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            x = cachedOrientedMarginLeft + indent + (effectiveContentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED: {
            // Only wrapped continuations are stretched; a paragraph's last
            // line and the page's last line (which may continue on the next
            // page) stay ragged. The slack is spread between glyphs because
            // character-wrapped Korean lines have few word gaps to widen.
            const bool canJustify = !endsParagraph && i + 1 < lineCount;
            const int gaps = txt_layout::utf8Length(line) - 1;
            const int extra = effectiveContentWidth - textWidth;
            if (canJustify && extra > 0 && gaps > 0) {
              letterSpacing = static_cast<int8_t>(std::min(extra / gaps, 127));
            }
            break;
          }
        }

        if (letterSpacing > 0) {
          renderer.drawTextTracked(cachedFontId, x, y, line.c_str(), letterSpacing);
        } else {
          renderer.drawText(cachedFontId, x, y, line.c_str());
        }
      }
      y += lineHeight;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();      // scan pass
  renderStatusBar();  // scan: a CJK title joins the batch prewarm
  scope.endScanAndPrewarm();

  // BW rendering
  renderLines();
  renderStatusBar();

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
}

void TxtReaderActivity::renderStatusBar() const {
  std::string title;
  if (SETTINGS.statusBarSpec().showsTitle()) {
    title = txt->getTitle();
  }
  const float progress = fileSize > 0 ? currentOffset * 100.0f / fileSize : 0.0f;
  GUI.drawStatusBar(renderer, progress, currentPageNumber(), totalPageCount(), title, 0, 0, true, false,
                    pageCountEstimated());
}

// --- Page numbers ------------------------------------------------------------

// Progress is byte-based (always exact); page numbers come from the index and
// are estimated from bytes per page beyond the point it has reached.
int TxtReaderActivity::currentPageNumber() const {
  const int total = totalPageCount();
  if (atEnd) return total;
  const int page = pageIndex.pageFor(static_cast<uint32_t>(currentOffset), static_cast<uint32_t>(estBytesPerPage));
  return std::max(1, std::min(total, page + 1));
}

int TxtReaderActivity::totalPageCount() const {
  return pageIndex.totalPages(static_cast<uint32_t>(fileSize), static_cast<uint32_t>(estBytesPerPage));
}

bool TxtReaderActivity::pageCountEstimated() const { return !pageIndex.complete; }

int TxtReaderActivity::progressPercent() const {
  if (fileSize == 0) return 0;
  if (atEnd) return 100;
  return clampPercent(static_cast<int>(currentOffset * 100.0f / fileSize + 0.5f));
}

// --- Reader menu -----------------------------------------------------------

bool TxtReaderActivity::handleFormatInput() {
  if (!initialized) return false;

  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
      automaticPageTurnActive = false;
      requestUpdate();
      return true;
    }
    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return false;
    }
    if (millis() - lastPageTurnTime >= pageTurnDuration) {
      lastPageTurnTime = millis();
      const bool onLastPage = renderedOffset == currentOffset && currentEndOffset >= fileSize;
      if (onLastPage) {
        automaticPageTurnActive = false;  // stop at the last page rather than leave the book
      } else {
        pageTurn(true);
      }
      requestUpdate();
      return true;
    }
  }

  // Home-key boards have no front Confirm button: a Home-key hold opens the
  // menu when that is the user's long-press function, as in the EPUB reader.
  const bool homeKeyMenu =
      mappedInput.wasHomeKeyHold() && SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_READER_MENU;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || homeKeyMenu ||
      ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
    openReaderMenu();
    return true;
  }

  // Long-press page jump chosen in the reader menu. Independent of the
  // long-press button setting: the menu choice is the whole opt-in.
  if (currentPageJumpOption > 0 && currentPageJumpOption < std::size(PAGE_JUMP_STEPS)) {
    using Button = MappedInputManager::Button;
    const int step = PAGE_JUMP_STEPS[currentPageJumpOption];
    const bool swapFront = mappedInput.isNavDirectionSwapped();
    const Button prevButtons[] = {Button::PageBack, swapFront ? Button::Right : Button::Left};
    const Button nextButtons[] = {Button::PageForward, swapFront ? Button::Left : Button::Right};
    const auto anyLongPressed = [&](const Button(&buttons)[2]) {
      return mappedInput.wasLongPressed(buttons[0], PAGE_JUMP_HOLD_MS) ||
             mappedInput.wasLongPressed(buttons[1], PAGE_JUMP_HOLD_MS);
    };
    if (anyLongPressed(prevButtons) || anyLongPressed(nextButtons)) {
      // Both checks ran so a fired long press consumes its release either way.
      skipPages(mappedInput.isPressed(nextButtons[0]) || mappedInput.isPressed(nextButtons[1]) ? step : -step);
      requestUpdate();
      return true;
    }
    if (SETTINGS.longPressButtonBehavior == CrossPointSettings::OFF) {
      // Press-to-turn would fire before the hold can be measured, so while a
      // jump step is active the page turns on the short release instead.
      const auto anyPressed = [&](const Button(&buttons)[2]) {
        return mappedInput.wasPressed(buttons[0]) || mappedInput.wasPressed(buttons[1]);
      };
      const auto anyReleased = [&](const Button(&buttons)[2]) {
        return mappedInput.wasReleased(buttons[0]) || mappedInput.wasReleased(buttons[1]);
      };
      if (anyPressed(prevButtons) || anyPressed(nextButtons)) return true;
      const bool prevReleased = anyReleased(prevButtons);
      const bool nextReleased = anyReleased(nextButtons);
      if (prevReleased || nextReleased) {
        pageTurn(nextReleased);
        requestUpdate();
        return true;
      }
    }
  }
  return false;
}

bool TxtReaderActivity::handleButtonAction(CrossPointSettings::ButtonAction action) {
  using A = CrossPointSettings::ButtonAction;
  if (ReaderActivity::handleButtonAction(action)) return true;
  if (action == A::ReaderMenu) { openReaderMenu(); return true; }
  if (action == A::Rotate) {
    SETTINGS.orientation = (SETTINGS.orientation + 1) % CrossPointSettings::ORIENTATION_COUNT;
    SETTINGS.saveToFile();
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    invalidateLayout();
    requestUpdate();
    return true;
  }
  return false;
}

void TxtReaderActivity::openReaderMenu() {
  startActivityForResult(
      std::make_unique<TxtReaderMenuActivity>(renderer, mappedInput, txt->getTitle(), currentPageNumber(),
                                              totalPageCount(), progressPercent(), SETTINGS.orientation,
                                              currentPageTurnOption, currentPageJumpOption, totalReadingSeconds()),
      [this](const ActivityResult& result) {
        const auto& menu = std::get<MenuResult>(result.data);
        if (SETTINGS.orientation != menu.orientation) {
          SETTINGS.orientation = menu.orientation;
          SETTINGS.saveToFile();
          ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
          invalidateLayout();  // the page index depends on the viewport; the byte offset carries the position
        }
        currentPageJumpOption = menu.pageJumpOption;
        toggleAutoPageTurn(menu.pageTurnOption);
        if (!result.isCancelled) {
          onReaderMenuConfirm(static_cast<TxtReaderMenuActivity::MenuAction>(menu.action));
        } else {
          requestUpdate();
        }
      });
}

void TxtReaderActivity::invalidateLayout() {
  // Called on the main task with the render lock free. A partial index for
  // the old layout is saved so switching back later resumes it.
  RenderLock lock;
  if (initialized && indexPagesSinceSave > 0) savePageIndexCache();
  initialized = false;
}

void TxtReaderActivity::toggleAutoPageTurn(const uint8_t selectedPageTurnOption) {
  currentPageTurnOption = selectedPageTurnOption;
  if (selectedPageTurnOption == 0 || selectedPageTurnOption >= std::size(PAGE_TURN_RATES)) {
    automaticPageTurnActive = false;
    return;
  }
  lastPageTurnTime = millis();
  pageTurnDuration = (1UL * 60 * 1000) / PAGE_TURN_RATES[selectedPageTurnOption];
  automaticPageTurnActive = true;
}

void TxtReaderActivity::jumpToPercent(int percent) {
  if (fileSize == 0) return;
  percent = clampPercent(percent);

  // Overflow-safe (fileSize/100)*percent + (fileSize%100)*percent/100.
  size_t target =
      (fileSize / 100) * static_cast<size_t>(percent) + (fileSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    target = fileSize - 1;
  }

  RenderLock lock;  // snapToLineStart reads the file
  moveToOffset(snapToLineStart(target));
}

void TxtReaderActivity::onReaderMenuConfirm(const TxtReaderMenuActivity::MenuAction action) {
  using MA = TxtReaderMenuActivity::MenuAction;
  switch (action) {
    case MA::TEXT_SETTINGS:
      startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                    TextSettingsActivity::Tab::Family),
                             [this](const ActivityResult&) {
                               // Any of font, size, spacing, margin, alignment, indent or
                               // character wrap changes the pagination; re-layout from the
                               // current byte offset.
                               invalidateLayout();
                               openReaderMenu();
                             });
      return;
    case MA::GO_TO_PERCENT: {
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, progressPercent()),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              openReaderMenu();
            } else {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
              requestUpdate();
            }
          });
      return;
    }
    case MA::SCREENSHOT:
      pendingScreenshot = true;
      break;
    case MA::GO_HOME:
      onGoHome();
      return;
    case MA::RESET_READING_TIMER:
      startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESET_READING_TIMER),
                                                                    tr(STR_RESET_READING_TIMER_PROMPT)),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) resetReadingTimer();
                               requestUpdate();
                             });
      return;
    case MA::NIGHT_MODE:
    case MA::AUTO_PAGE_TURN:
    case MA::PAGE_JUMP_STEP:
    case MA::ROTATE_SCREEN:
      // Applied through the menu result (or in place by the menu itself).
      break;
  }
  requestUpdate();
}

void TxtReaderActivity::onExit() {
  automaticPageTurnActive = false;
  // Runs under the activity manager's render lock: no lock of our own here.
  if (txt && initialized && indexPagesSinceSave > 0) savePageIndexCache();
  ReaderActivity::onExit();
}

// --- Navigation --------------------------------------------------------------

bool TxtReaderActivity::pageTurn(const bool isForward) {
  if (!initialized || fileSize == 0) {
    return false;
  }

  if (isForward) {
    if (atEnd) return false;
    // The rendered page told us where the next one starts. If a render is
    // still in flight (the button came before it finished), wait for it; a
    // page the index knows is turned from the index, and as a last resort the
    // page is laid out here.
    size_t next = SIZE_MAX;
    if (renderedOffset != currentOffset) {
      RenderLock lock;
    }
    if (renderedOffset == currentOffset) {
      next = currentEndOffset;
    } else {
      const int page = pageIndex.pageContaining(static_cast<uint32_t>(currentOffset));
      if (page >= 0 && pageIndex.starts[page] == currentOffset) {
        if (page + 1 < static_cast<int>(pageIndex.starts.size())) {
          next = pageIndex.starts[page + 1];
        } else if (pageIndex.complete) {
          next = fileSize;
        }
      }
      if (next == SIZE_MAX) {
        RenderLock lock;
        std::vector<std::string> lines;
        size_t laidOut = currentOffset;
        if (loadPageAtOffset(renderer, currentOffset, lines, nullptr, nullptr, laidOut)) next = laidOut;
      }
    }
    if (next == SIZE_MAX || next <= currentOffset) return false;
    if (next >= fileSize) {
      atEnd = true;
      return true;
    }
    pushBackHistory(currentOffset);
    currentOffset = next;
    return true;
  }

  if (atEnd) {
    atEnd = false;
    return true;
  }
  if (currentOffset == 0) {
    return false;
  }
  if (!backHistory.empty()) {
    currentOffset = backHistory.back();
    backHistory.pop_back();
    return true;
  }
  const int page = pageIndex.pageContaining(static_cast<uint32_t>(currentOffset));
  if (page >= 0) {
    // Mid-page relative to the index (after a jump): back onto the grid first.
    if (pageIndex.starts[page] < currentOffset) {
      currentOffset = pageIndex.starts[page];
      return true;
    }
    if (page > 0) {
      currentOffset = pageIndex.starts[page - 1];
      return true;
    }
    return false;
  }
  {
    RenderLock lock;
    currentOffset = findBackwardPageStart(renderer, currentOffset);
  }
  return true;
}

bool TxtReaderActivity::skipPages(int amount) {
  if (!initialized || fileSize == 0 || amount == 0) {
    return false;
  }
  if (atEnd) {
    // The end-of-book screen counts as one position past the last page.
    if (amount > 0) return false;
    atEnd = false;
    if (++amount == 0) return true;
  }

  const int page = pageIndex.pageFor(static_cast<uint32_t>(currentOffset), static_cast<uint32_t>(estBytesPerPage));
  const int total = totalPageCount();
  int target = page + amount;
  if (target < 0) target = 0;
  if (target >= total) {
    if (pageIndex.complete) {
      atEnd = true;
      return true;
    }
    target = total - 1;
  }

  size_t offset =
      pageIndex.offsetForPage(target, static_cast<uint32_t>(fileSize), static_cast<uint32_t>(estBytesPerPage));
  if (target >= static_cast<int>(pageIndex.starts.size())) {
    // Extrapolated from bytes per page: land on a line start.
    RenderLock lock;
    offset = snapToLineStart(offset);
  }
  if (offset == currentOffset) return false;

  // Only a forward jump lets Back return to the pre-jump page. After a
  // backward jump, Back keeps moving backward page by page.
  if (amount > 0) {
    pushBackHistory(currentOffset);
  } else {
    backHistory.clear();
  }
  currentOffset = offset;
  return true;
}

bool TxtReaderActivity::isAtEndOfBook() const { return initialized && atEnd; }

void TxtReaderActivity::onReturnFromEndOfBook() { atEnd = false; }

// --- Progress and page index cache -------------------------------------------

// progress.bin: page (2 bytes, the upstream 1.6 layout) followed by the page's
// byte offset (4 bytes). The offset is the position; the page number is for
// upstream builds that read only the first two bytes.
void TxtReaderActivity::saveProgress() const {
  const int page = std::min(currentPageNumber() - 1, 0xFFFF);
  const uint32_t offset = static_cast<uint32_t>(currentOffset);
  uint8_t data[8];
  data[0] = page & 0xFF;
  data[1] = (page >> 8) & 0xFF;
  data[2] = 0;
  data[3] = 0;
  data[4] = offset & 0xFF;
  data[5] = (offset >> 8) & 0xFF;
  data[6] = (offset >> 16) & 0xFF;
  data[7] = (offset >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, sizeof(data))) {
    LOG_ERR("TRS", "Failed to save progress: offset %zu", currentOffset);
  }
}

void TxtReaderActivity::loadProgress() {
  currentOffset = 0;
  pendingProgressPage = -1;

  HalFile f;
  if (!Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    return;
  }
  uint8_t data[LEGACY_PROGRESS_SIZE] = {};
  const int n = f.read(data, sizeof(data));

  size_t offset = SIZE_MAX;
  if (n >= static_cast<int>(LEGACY_PROGRESS_SIZE) && readU32(data) == LEGACY_PROGRESS_MAGIC) {
    // KO 1.5: the offset is valid for this file only, and its high 32 bits are zero.
    if (data[4] == LEGACY_PROGRESS_VERSION && readU32(data + LEGACY_PROGRESS_FILE_SIZE_POS) == fileSize &&
        readU32(data + LEGACY_PROGRESS_OFFSET_POS + 4) == 0) {
      offset = readU32(data + LEGACY_PROGRESS_OFFSET_POS);
    }
  } else if (n >= 8) {
    offset = readU32(data + 4);
  } else if (n >= 4) {
    // Upstream 1.6 wrote only the page number; the index resolves it.
    const int page = data[0] | (data[1] << 8);
    if (page < static_cast<int>(pageIndex.starts.size())) {
      offset = pageIndex.starts[page];
    } else {
      pendingProgressPage = page;
    }
  }

  if (offset != SIZE_MAX && offset < fileSize) {
    moveToOffset(snapToLineStart(offset));
    LOG_DBG("TRS", "Loaded progress: offset %zu / %zu", currentOffset, fileSize);
  }
}

bool TxtReaderActivity::loadPageIndexCache() {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    return false;
  }

  uint32_t cachedFileSize;
  serialization::readPod(f, cachedFileSize);
  if (cachedFileSize != fileSize) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    return false;
  }

  int32_t cachedHeight;
  serialization::readPod(f, cachedHeight);
  if (cachedHeight != viewportHeight) {
    LOG_DBG("TRS", "Cache viewport height mismatch, rebuilding");
    return false;
  }

  int32_t cachedLineHeight;
  serialization::readPod(f, cachedLineHeight);
  if (cachedLineHeight != lineHeight) {
    LOG_DBG("TRS", "Cache line height mismatch, rebuilding");
    return false;
  }

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, cachedFontId);
    return false;
  }

  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) {
    LOG_DBG("TRS", "Cache screen margin mismatch, rebuilding");
    return false;
  }

  uint8_t alignment;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    return false;
  }

  // Korean layout: each changes where lines break or how tall pages are.
  uint8_t layoutFlags;
  serialization::readPod(f, layoutFlags);
  const uint8_t currentFlags = static_cast<uint8_t>((cachedCharacterWrap ? 1 : 0) | (cachedParagraphIndent ? 2 : 0) |
                                                    (cachedExtraParagraphSpacing ? 4 : 0));
  if (layoutFlags != currentFlags) {
    LOG_DBG("TRS", "Cache layout flags mismatch, rebuilding");
    return false;
  }

  int32_t indentPx;
  serialization::readPod(f, indentPx);
  if (indentPx != paragraphIndentPx) {
    LOG_DBG("TRS", "Cache indent mismatch, rebuilding");
    return false;
  }

  uint8_t complete;
  serialization::readPod(f, complete);
  uint32_t indexedEnd;
  serialization::readPod(f, indexedEnd);
  uint32_t numPages;
  serialization::readPod(f, numPages);
  if (numPages == 0 || numPages > fileSize) {
    LOG_DBG("TRS", "Cache page count invalid, rebuilding");
    return false;
  }

  std::vector<uint32_t> starts;
  starts.reserve(numPages);
  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    starts.push_back(offset);
  }

  // A partial index must end on the page it was building; a complete one
  // covers the file.
  const bool consistent = starts.front() == 0 && std::is_sorted(starts.begin(), starts.end()) &&
                          starts.back() < fileSize && (complete ? indexedEnd == fileSize : indexedEnd == starts.back());
  if (!consistent) {
    LOG_DBG("TRS", "Cache index inconsistent, rebuilding");
    return false;
  }

  pageIndex.starts = std::move(starts);
  pageIndex.indexedEnd = indexedEnd;
  pageIndex.complete = complete != 0;
  LOG_DBG("TRS", "Loaded page index cache: %u pages%s", numPages, pageIndex.complete ? "" : " (partial)");
  return true;
}

void TxtReaderActivity::savePageIndexCache() {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }

  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(fileSize));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(viewportHeight));
  serialization::writePod(f, static_cast<int32_t>(lineHeight));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  const uint8_t layoutFlags = static_cast<uint8_t>((cachedCharacterWrap ? 1 : 0) | (cachedParagraphIndent ? 2 : 0) |
                                                   (cachedExtraParagraphSpacing ? 4 : 0));
  serialization::writePod(f, layoutFlags);
  serialization::writePod(f, static_cast<int32_t>(paragraphIndentPx));
  serialization::writePod(f, static_cast<uint8_t>(pageIndex.complete ? 1 : 0));
  serialization::writePod(f, pageIndex.indexedEnd);
  serialization::writePod(f, static_cast<uint32_t>(pageIndex.starts.size()));

  for (const uint32_t offset : pageIndex.starts) {
    serialization::writePod(f, offset);
  }

  indexPagesSinceSave = 0;
  LOG_DBG("TRS", "Saved page index cache: %u pages%s", static_cast<unsigned>(pageIndex.starts.size()),
          pageIndex.complete ? "" : " (partial)");
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPageNumber();
  info.totalPages = totalPageCount();
  info.progressPercent = progressPercent();
  return info;
}
