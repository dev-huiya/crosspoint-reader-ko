#pragma once

#include <cstdint>

enum class ReaderTapZone : uint8_t { None, Previous, Menu, Next };

// Logical screen coordinates keep the same layout in every display orientation.
constexpr ReaderTapZone readerTapZone(int width, int height, int x, int y, uint8_t layout) {
  if (width <= 0 || height <= 0 || x < 0 || y < 0 || x >= width || y >= height) return ReaderTapZone::None;
  switch (layout) {
    case 1:
      if (x >= width * 3 / 8 && x < width * 5 / 8 && y >= height * 3 / 8 && y < height * 5 / 8)
        return ReaderTapZone::Menu;
      return y < height * 2 / 3 ? ReaderTapZone::Previous : ReaderTapZone::Next;
    case 2:
      if (x >= width * 3 / 8 && x < width * 5 / 8 && y >= height / 2 && y < height * 3 / 4)
        return ReaderTapZone::Menu;
      return y < height * 2 / 3 ? ReaderTapZone::Previous : ReaderTapZone::Next;
    case 3:
      if (y >= height * 7 / 8) return ReaderTapZone::Menu;
      return x < width / 2 ? ReaderTapZone::Previous : ReaderTapZone::Next;
    default:
      if (x < width / 3) return ReaderTapZone::Previous;
      if (x >= width - width / 3) return ReaderTapZone::Next;
      return ReaderTapZone::Menu;
  }
}
