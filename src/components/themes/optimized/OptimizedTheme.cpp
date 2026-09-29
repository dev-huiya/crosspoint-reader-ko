#include "OptimizedTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
constexpr int kTilePadding = 8;  // selection band around each cover
constexpr int kCornerRadius = 6;
constexpr int kCellRadius = 8;
constexpr int kCellBorder = 2;
constexpr int kIconSize = 32;
constexpr int kIconLabelGap = 8;
constexpr int kTitleTopGap = 5;
constexpr int kMaxTitleLines = 2;

const uint8_t* iconForName(const UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Blocks:
      return BlocksIcon;
    default:
      return nullptr;
  }
}

// GfxRenderer::drawIcon only paints ink black; the selected grid cell is a
// black box, so its icon is plotted white with the same pixel mapping.
void drawIconWhite(const GfxRenderer& renderer, const uint8_t* bitmap, const int x, const int y, const int size) {
  const int rowBytes = (size + 7) / 8;
  for (int row = 0; row < size; row++) {
    for (int col = 0; col < size; col++) {
      const uint8_t byte = bitmap[row * rowBytes + (col >> 3)];
      if (((byte >> (7 - (col & 7))) & 1) == 0) {
        renderer.drawPixel(x + (size - 1 - row), y + col, false);
      }
    }
  }
}

int visibleCoverCount(const std::vector<RecentBook>& recentBooks) {
  return std::min(static_cast<int>(recentBooks.size()), OptimizedMetrics::values.homeRecentBooksCount);
}

int tileWidthFor(const Rect& rect) {
  return (rect.width - 2 * OptimizedMetrics::values.contentSidePadding) /
         std::max(1, OptimizedMetrics::values.homeRecentBooksCount);
}

// 2:3 cover box centred in tile `index`.
Rect coverBox(const Rect& rect, const int index) {
  const int tileWidth = tileWidthFor(rect);
  const int tileX = rect.x + OptimizedMetrics::values.contentSidePadding + tileWidth * index;
  const int height = OptimizedMetrics::values.homeCoverHeight;
  const int width = std::min(height * 2 / 3, tileWidth - 2 * kTilePadding);
  return Rect{tileX + (tileWidth - width) / 2, rect.y + kTilePadding, width, height};
}

// Thumbnails are generated cover-filled (both sides >= target), so crop the
// overflow symmetrically. cropX is nudged by a quarter pixel so drawBitmap's
// floor() lands on exactly cropPix and the drawn image never exceeds the box.
void drawCoverBitmap(const GfxRenderer& renderer, const Bitmap& bitmap, const Rect& box) {
  const int bmpW = bitmap.getWidth();
  const int bmpH = bitmap.getHeight();
  if (bmpW <= 0 || bmpH <= 0) return;
  const int cropPixX = bmpW > box.width ? (bmpW - box.width + 1) / 2 : 0;
  const int cropPixY = bmpH > box.height ? (bmpH - box.height + 1) / 2 : 0;
  const float cropX = cropPixX > 0 ? (2.0f * cropPixX + 0.5f) / static_cast<float>(bmpW) : 0.0f;
  const float cropY = cropPixY > 0 ? (2.0f * cropPixY + 0.5f) / static_cast<float>(bmpH) : 0.0f;
  const int drawnW = bmpW - 2 * cropPixX;
  const int drawnH = bmpH - 2 * cropPixY;
  renderer.drawBitmap(bitmap, box.x + std::max(0, (box.width - drawnW) / 2),
                      box.y + std::max(0, (box.height - drawnH) / 2), box.width, box.height, cropX, cropY);
}
}  // namespace

int OptimizedTheme::getMenuRowHeight(const GfxRenderer&) const { return OptimizedMetrics::values.menuRowHeight; }

void OptimizedTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                         const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                         bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int count = visibleCoverCount(recentBooks);
  const int coverHeight = OptimizedMetrics::values.homeCoverHeight;

  // Covers come from SD only on the first render; later renders restore the
  // stored tile band and only redraw the selection and titles on top of it.
  if (!coverRendered) {
    for (int i = 0; i < count; i++) {
      const Rect box = coverBox(rect, i);
      bool hasCover = false;
      if (!recentBooks[i].coverBmpPath.empty()) {
        const std::string path = UITheme::getGrayCoverThumbPath(recentBooks[i].coverBmpPath, coverHeight);
        HalFile file;
        if (Storage.openFileForRead("HOME", path, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            drawCoverBitmap(renderer, bitmap, box);
            hasCover = true;
          }
        }
      }

      renderer.drawRect(box.x, box.y, box.width, box.height, true);
      if (!hasCover) {
        renderer.fillRect(box.x, box.y + box.height / 3, box.width, 2 * box.height / 3, true);
        renderer.drawIcon(CoverIcon, box.x + 24, box.y + 24, 32);
      }
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
  }

  const int tileWidth = tileWidthFor(rect);
  const int titleLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  for (int i = 0; i < count; i++) {
    const Rect box = coverBox(rect, i);
    const int tileX = rect.x + OptimizedMetrics::values.contentSidePadding + tileWidth * i;
    const int titleTop = box.y + box.height + kTitleTopGap;
    const int titleSpace = rect.y + rect.height - kTilePadding / 2 - titleTop;
    const int maxLines = std::max(1, std::min(kMaxTitleLines, titleLineHeight > 0 ? titleSpace / titleLineHeight : 1));
    const auto titleLines =
        renderer.wrappedText(SMALL_FONT_ID, recentBooks[i].title.c_str(), tileWidth - 2 * kTilePadding, maxLines);

    if (selectorIndex == i) {
      // Light band around the cover only: the cover pixels stay untouched so
      // their grays (painted once by the home grayscale pass) survive.
      const int bandBottom = titleTop + static_cast<int>(titleLines.size()) * titleLineHeight + kTilePadding / 2;
      renderer.fillRoundedRect(tileX, rect.y, tileWidth, box.y - rect.y, kCornerRadius, true, true, false, false,
                               Color::LightGray);
      renderer.fillRectDither(tileX, box.y, box.x - tileX, box.height, Color::LightGray);
      renderer.fillRectDither(box.x + box.width, box.y, tileX + tileWidth - box.x - box.width, box.height,
                              Color::LightGray);
      renderer.fillRoundedRect(tileX, box.y + box.height, tileWidth, bandBottom - box.y - box.height, kCornerRadius,
                               false, false, true, true, Color::LightGray);
    }

    int y = titleTop;
    for (const auto& line : titleLines) {
      UITheme::drawCenteredText(renderer, Rect{tileX, y, tileWidth, titleLineHeight}, SMALL_FONT_ID, y, line.c_str(),
                                true);
      y += titleLineHeight;
    }
  }
}

void OptimizedTheme::drawRecentBookCoversGray(GfxRenderer& renderer, Rect rect,
                                              const std::vector<RecentBook>& recentBooks) const {
  const int count = visibleCoverCount(recentBooks);
  for (int i = 0; i < count; i++) {
    if (recentBooks[i].coverBmpPath.empty()) continue;
    const std::string path =
        UITheme::getGrayCoverThumbPath(recentBooks[i].coverBmpPath, OptimizedMetrics::values.homeCoverHeight);
    HalFile file;
    if (!Storage.openFileForRead("HOME", path, file)) continue;
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() != BmpReaderError::Ok || !bitmap.hasGreyscale()) continue;
    drawCoverBitmap(renderer, bitmap, coverBox(rect, i));
  }
}

void OptimizedTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                    const std::function<std::string(int index)>& buttonLabel,
                                    const std::function<UIIcon(int index)>& rowIcon) const {
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  for (int i = 0; i < buttonCount; ++i) {
    const Rect cell = homeMenuGridCell(OptimizedMetrics::values, rect, i, buttonCount);
    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(cell.x, cell.y, cell.width, cell.height, kCellRadius, Color::Black);
    } else {
      renderer.drawRoundedRect(cell.x, cell.y, cell.width, cell.height, kCellBorder, kCellRadius, true);
    }

    const uint8_t* icon = rowIcon != nullptr ? iconForName(rowIcon(i)) : nullptr;
    const int blockHeight = (icon != nullptr ? kIconSize + kIconLabelGap : 0) + lineHeight;
    int y = cell.y + (cell.height - blockHeight) / 2;
    if (icon != nullptr) {
      const int iconX = cell.x + (cell.width - kIconSize) / 2;
      if (selected) {
        drawIconWhite(renderer, icon, iconX, y, kIconSize);
      } else {
        renderer.drawIcon(icon, iconX, y, kIconSize);
      }
      y += kIconSize + kIconLabelGap;
    }

    const std::string label = buttonLabel(i);
    const std::string fitted =
        renderer.truncatedText(UI_12_FONT_ID, label.c_str(), cell.width - 2 * kCellRadius, EpdFontFamily::BOLD);
    UITheme::drawCenteredText(renderer, cell, UI_12_FONT_ID, y, fitted.c_str(), !selected, EpdFontFamily::BOLD);
  }
}
