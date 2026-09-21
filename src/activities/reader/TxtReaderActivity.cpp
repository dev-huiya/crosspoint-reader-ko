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
constexpr uint8_t CACHE_VERSION = 4;          // v4: height-driven pages, Korean layout settings

// Same rates as the EPUB reader's auto page turn (pages per minute, index 0 = off).
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
// Long-press page jump sizes offered by the reader menu (index 0 = off).
constexpr int PAGE_JUMP_STEPS[] = {0, 10, 20, 50, 100};
// Held duration above which a page-button release jumps instead of turning.
constexpr unsigned long PAGE_JUMP_HOLD_MS = ReaderUtils::SKIP_HOLD_MS;

int clampPercent(const int percent) { return std::max(0, std::min(100, percent)); }

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

  LOG_DBG("TRS", "Viewport: %dx%d, line %dpx, up to %d lines per page", viewportWidth, viewportHeight, lineHeight,
          linesPerPage);

  // Try to load cached page index first
  if (!loadPageIndexCache()) {
    // Cache not found, build page index
    buildPageIndex(renderer);
    // Save to cache for next time
    savePageIndexCache();
  }

  // Load saved progress
  loadProgress();

  initialized = true;
}

bool TxtReaderActivity::isOffsetAtLineStart(const size_t offset) const {
  if (offset == 0) return true;
  uint8_t previous = 0;
  if (!txt->readContent(&previous, offset - 1, 1)) return true;
  return previous == '\n';
}

void TxtReaderActivity::buildPageIndex(GfxRenderer& renderer) {
  pageOffsets.clear();
  pageOffsets.push_back(0);  // First page starts at offset 0

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(renderer, offset, tempLines, nullptr, nullptr, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages", totalPages);
}

bool TxtReaderActivity::loadPageAtOffset(GfxRenderer& renderer, size_t offset, std::vector<std::string>& outLines,
                                         std::vector<bool>* outStartsParagraph, std::vector<bool>* outEndsParagraph,
                                         size_t& nextOffset) {
  outLines.clear();
  if (outStartsParagraph) outStartsParagraph->clear();
  if (outEndsParagraph) outEndsParagraph->clear();
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x01);
  }

  // A page that starts mid-line continues the previous page's paragraph:
  // no indent, no spacing above.
  const bool firstLineIsParagraphStart = isOffsetAtLineStart(offset);

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
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    size_t lineContentLen = lineEnd - pos;
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);
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

void TxtReaderActivity::renderBook() {
  if (!txt) {
    return;
  }

  if (!initialized) {
    initializeReader(renderer);
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  // Load current page content
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(renderer, offset, currentPageLines, &currentPageLineStartsParagraph, &currentPageLineEndsParagraph,
                   nextOffset);

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
  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;
  std::string title;
  if (SETTINGS.statusBarSpec().showsTitle()) {
    title = txt->getTitle();
  }
  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title);
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
      if (currentPage + 1 < totalPages) {
        pageTurn(true);
      } else {
        automaticPageTurnActive = false;  // stop at the last page rather than leave the book
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

void TxtReaderActivity::openReaderMenu() {
  const int progressPercent = totalPages > 0 ? clampPercent((currentPage + 1) * 100 / totalPages) : 0;
  startActivityForResult(std::make_unique<TxtReaderMenuActivity>(
                             renderer, mappedInput, txt->getTitle(), currentPage + 1, totalPages, progressPercent,
                             SETTINGS.orientation, currentPageTurnOption, currentPageJumpOption, totalReadingSeconds()),
                         [this](const ActivityResult& result) {
                           const auto& menu = std::get<MenuResult>(result.data);
                           if (SETTINGS.orientation != menu.orientation) {
                             SETTINGS.orientation = menu.orientation;
                             SETTINGS.saveToFile();
                             ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
                             initialized =
                                 false;  // the page index depends on the viewport; progress carries the byte offset
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

void TxtReaderActivity::jumpToPercent(const int percent) {
  if (totalPages <= 0) return;
  currentPage = std::max(0, std::min(totalPages - 1, (totalPages * clampPercent(percent)) / 100));
}

void TxtReaderActivity::onReaderMenuConfirm(const TxtReaderMenuActivity::MenuAction action) {
  using MA = TxtReaderMenuActivity::MenuAction;
  switch (action) {
    case MA::TEXT_SETTINGS:
      startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                    TextSettingsActivity::Tab::Family),
                             [this](const ActivityResult&) {
                               // Any of font, size, spacing, margin, alignment, indent or
                               // character wrap changes the pagination; rebuild from the
                               // saved byte offset.
                               initialized = false;
                               openReaderMenu();
                             });
      return;
    case MA::GO_TO_PERCENT: {
      const int initialPercent = totalPages > 0 ? clampPercent((currentPage + 1) * 100 / totalPages) : 0;
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
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
  ReaderActivity::onExit();
}

// --- Navigation --------------------------------------------------------------

bool TxtReaderActivity::pageTurn(bool isForward) {
  // Ignore paging until initializeReader has established the page index
  if (!initialized) {
    return false;
  }
  if (isForward) {
    if (currentPage < totalPages) {
      currentPage++;
      return true;
    }
  } else {
    if (currentPage > 0) {
      currentPage--;
      return true;
    }
  }
  return false;
}

bool TxtReaderActivity::skipPages(int amount) {
  if (!initialized) {
    return false;
  }
  int newPage = currentPage + amount;
  if (newPage < 0) newPage = 0;
  // Clamp to totalPages, not totalPages - 1: pageTurn() lets currentPage reach
  // totalPages and isAtEndOfBook() treats that as the end-of-book sentinel, so
  // a forward skip must be able to reach it too.
  if (newPage > totalPages) newPage = totalPages;
  if (newPage != currentPage) {
    currentPage = newPage;
    return true;
  }
  return false;
}

bool TxtReaderActivity::isAtEndOfBook() const { return initialized && currentPage >= totalPages; }

void TxtReaderActivity::onReturnFromEndOfBook() { currentPage = totalPages > 0 ? totalPages - 1 : 0; }

// --- Progress and page index cache -------------------------------------------

// progress.bin: page (2 bytes, the upstream 1.6 layout) followed by the page's
// byte offset (4 bytes). When the index is rebuilt for new text settings the
// page number is meaningless, so the offset restores the position instead.
void TxtReaderActivity::saveProgress() const {
  // Past the end (the end-of-book sentinel) records the last page.
  uint32_t offset = 0;
  if (!pageOffsets.empty()) {
    offset = currentPage < static_cast<int>(pageOffsets.size()) ? pageOffsets[currentPage] : pageOffsets.back();
  }
  uint8_t data[8];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = 0;
  data[3] = 0;
  data[4] = offset & 0xFF;
  data[5] = (offset >> 8) & 0xFF;
  data[6] = (offset >> 16) & 0xFF;
  data[7] = (offset >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, sizeof(data))) {
    LOG_ERR("TRS", "Failed to save progress: page %d", currentPage);
  }
}

void TxtReaderActivity::loadProgress() {
  HalFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[8] = {};
    const int n = f.read(data, sizeof(data));
    if (n >= 4) {
      currentPage = data[0] + (data[1] << 8);
      if (n >= 8) {
        // Prefer the offset: it survives a re-pagination, the page number does not.
        const uint32_t offset = static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8) |
                                (static_cast<uint32_t>(data[6]) << 16) | (static_cast<uint32_t>(data[7]) << 24);
        const auto it = std::upper_bound(pageOffsets.begin(), pageOffsets.end(), static_cast<size_t>(offset));
        if (it != pageOffsets.begin()) currentPage = static_cast<int>(it - pageOffsets.begin()) - 1;
      }
      if (currentPage >= totalPages) {
        currentPage = totalPages - 1;
      }
      if (currentPage < 0) {
        currentPage = 0;
      }
      LOG_DBG("TRS", "Loaded progress: page %d/%d", currentPage, totalPages);
    }
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

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != txt->getFileSize()) {
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

  uint32_t numPages;
  serialization::readPod(f, numPages);

  pageOffsets.clear();
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    pageOffsets.push_back(offset);
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void TxtReaderActivity::savePageIndexCache() const {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }

  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
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
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;
  info.totalPages = totalPages;
  info.progressPercent = totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}
