#pragma once

#include "components/themes/lyra/Lyra3CoversTheme.h"

class GfxRenderer;

// Lyra look with three 2:3 covers rendered in 4 gray levels (2-bit thumbnails
// + a home grayscale pass) and the home menu as a 2-column grid of boxes.
namespace OptimizedMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = Lyra3CoversMetrics::values;
  // 130x195 cover box: exact 2:3 (width = height * 2 / 3).
  v.homeCoverHeight = 195;
  v.homeCoverTileHeight = 262;
  v.homeRecentBooksCount = 3;
  // Grid cell height and the gap between cells (both axes).
  v.menuRowHeight = 104;
  v.menuSpacing = 12;
  v.homeMenuColumns = 2;
  v.homeGrayscaleCovers = true;
  return v;
}();
}  // namespace OptimizedMetrics

class OptimizedTheme : public LyraTheme {
 public:
  int getMenuRowHeight(const GfxRenderer& renderer) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawRecentBookCoversGray(GfxRenderer& renderer, Rect rect,
                                const std::vector<RecentBook>& recentBooks) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};
