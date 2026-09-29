#include "HomeCoverCache.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>

#include "UITheme.h"

namespace fui = freeink::ui;

void HomeCoverCache::begin() {
  // Cover regions do not overlap. Each may widen by one physical byte per row.
  coverCacheCapacity = renderer.getRegionByteSize(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()) +
                       MAX_COVERS * std::max(renderer.getScreenWidth(), renderer.getScreenHeight());
  coverCache = HalMemory::allocatePsram(coverCacheCapacity);
  if (!coverCache) {
    LOG_ERR("HOME", "PSRAM cover cache unavailable (%u bytes); rendering uncached", unsigned(coverCacheCapacity));
    coverCacheCapacity = 0;
  }
}

void HomeCoverCache::invalidate() {
  coverCacheUsed = 0;
  cachedCovers.fill(CachedCover{});
}

void HomeCoverCache::invalidate(size_t index) {
  if (index < cachedCovers.size()) cachedCovers[index].valid = false;
}

void HomeCoverCache::prepare() {
  const int orientation = static_cast<int>(renderer.getOrientation());
  if (coverCacheOrientation != orientation) {
    invalidate();
    coverCacheOrientation = orientation;
  }
}

bool HomeCoverCache::paint(fui::Rect rect, size_t index, const std::string& path) {
  if (index >= cachedCovers.size()) return false;
  auto& cached = cachedCovers[index];
  uint8_t* const cacheData = coverCache.get();
  if (coverCache && cached.valid && cached.rect.x == rect.x && cached.rect.y == rect.y &&
      cached.rect.width == rect.width && cached.rect.height == rect.height &&
      renderer.copyBufferToRegion(rect.x, rect.y, rect.width, rect.height, cacheData + cached.offset, cached.bytes)) {
    return true;
  }
  cached.valid = false;
  bool drawn = false;
  const unsigned long start = millis();
  // Only a cached thumbnail is drawn; a missing one gets the placeholder (the
  // book's "Refresh cache" on Home builds it).
  if (!path.empty() && Storage.openFileForRead("HOME", path, coverFile)) {
    // A file shorter than its pixel rows is still being written by the cover
    // job (or was cut off); it counts as missing.
    if (coverBitmap.parseHeaders() == BmpReaderError::Ok && coverBitmap.getWidth() > 0 &&
        coverBitmap.getHeight() > 0 &&
        coverFile.size() >= static_cast<size_t>(coverBitmap.getRowBytes()) * coverBitmap.getHeight() + 54) {
      // The art nudges a few px right of center; the spine below hugs its
      // left edge either way.
      constexpr int ART_SHIFT = 3;
      drawn = GUI.drawCoverThumbFill(renderer, coverBitmap, Rect{rect.x, rect.y, rect.width, rect.height}, ART_SHIFT);
      if (drawn) {
        renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
        // False spine glued to the art's left edge (centered art means that
        // edge moves with each cover's fit margin): a dark band with a
        // dithered crease makes every cover read as a bound book. Drawn here
        // so the PSRAM snapshot below captures it.
        // An oversized thumbnail is drawn fitted, unshifted (drawCoverThumbFill).
        const int bmpW = coverBitmap.getWidth();
        const int bmpH = coverBitmap.getHeight();
        const bool fitted = bmpW * 4 > rect.width * 5 && bmpH * 4 > rect.height * 5;
        const int artWidth =
            fitted ? static_cast<int>(bmpW * std::min(static_cast<float>(rect.width) / bmpW,
                                                      static_cast<float>(rect.height) / bmpH))
                   : bmpW;
        const int artLeft = std::max<int>(rect.x, rect.x + (rect.width - artWidth) / 2 + (fitted ? 0 : ART_SHIFT));
        renderer.fillRect(artLeft, rect.y, 3, rect.height);
        renderer.fillRectDither(artLeft + 3, rect.y, 2, rect.height, Color::LightGray);
      }
    }
    coverFile.close();
  }
  if (!drawn) {
    GUI.drawCoverPlaceholder(renderer, Rect{rect.x, rect.y, rect.width, rect.height});
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  }
  LOG_DBG("HOME", "Cover %u %s: %lu ms", static_cast<unsigned>(index), drawn ? "drawn" : "placeholder", millis() - start);
  if (coverCache) {
    const size_t needed = renderer.getRegionByteSize(rect.x, rect.y, rect.width, rect.height);
    if (needed > cached.bytes && needed <= coverCacheCapacity - coverCacheUsed) {
      cached.offset = coverCacheUsed;
      cached.bytes = needed;
      coverCacheUsed += needed;
    }
    if (needed > 0 && needed <= cached.bytes) {
      cached.rect = rect;
      cached.valid =
          renderer.copyRegionToBuffer(rect.x, rect.y, rect.width, rect.height, cacheData + cached.offset, cached.bytes);
    }
  }
  return true;  // The cover slot is painted, including fallback art.
}
