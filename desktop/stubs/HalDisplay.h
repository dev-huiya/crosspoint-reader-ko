#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include "DesktopBoardProfile.h"

class HalDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  static constexpr uint16_t DISPLAY_WIDTH = DESKTOP_PANEL_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = DESKTOP_PANEL_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  void begin() { clearScreen(); }
  uint8_t* getFrameBuffer() { return buffer_.data(); }
  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }
  void clearScreen(uint8_t color = 0xff) { buffer_.fill(color); }
  void drawImage(const uint8_t* image, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    blit(image, x, y, width, height, false);
  }
  void drawImageTransparent(const uint8_t* image, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    blit(image, x, y, width, height, true);
  }
  void displayBuffer(RefreshMode mode = FAST_REFRESH, bool = false) { lastRefresh_ = mode; }
  void displayBufferAsync(RefreshMode mode = FAST_REFRESH) { lastRefresh_ = mode; }
  RefreshMode lastRefresh() const { return lastRefresh_; }
  void waitRefreshComplete() {}
  bool supportsAsyncRefresh() const { return false; }
  bool isInverted() const { return false; }
  uint8_t* lendFrameBufferStorage(uint32_t* length) {
    *length = BUFFER_SIZE;
    return buffer_.data();
  }
  void returnFrameBufferStorage() {}
  void displayGrayscaleBase(RefreshMode = HALF_REFRESH, bool = false) {}
  void preconditionGrayscale() {}
  void preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}
  void copyGrayscaleLsbBuffers(const uint8_t*) {}
  void copyGrayscaleMsbBuffers(const uint8_t*) {}
  void displayGrayBuffer(bool = false) {}
  void writeGrayscalePlaneStrip(bool, const uint8_t*, uint16_t, uint16_t) {}
  bool supportsStripGrayscale() const { return false; }
  bool combinesGrayscaleBase() const { return false; }
  void cleanupGrayscaleBuffers(const uint8_t*) {}

 private:
  void blit(const uint8_t* image, uint16_t x, uint16_t y, uint16_t width, uint16_t height, bool transparent) {
    const uint16_t sourceWidthBytes = (width + 7) / 8;
    for (uint16_t row = 0; row < height && y + row < DISPLAY_HEIGHT; ++row) {
      for (uint16_t col = 0; col < width && x + col < DISPLAY_WIDTH; ++col) {
        const uint8_t source = image[row * sourceWidthBytes + col / 8];
        const bool white = (source & (0x80u >> (col % 8))) != 0;
        if (transparent && white) continue;
        uint8_t& target = buffer_[(y + row) * DISPLAY_WIDTH_BYTES + (x + col) / 8];
        const uint8_t mask = 0x80u >> ((x + col) % 8);
        if (white) target |= mask;
        else target &= static_cast<uint8_t>(~mask);
      }
    }
  }
  RefreshMode lastRefresh_ = FAST_REFRESH;
  std::array<uint8_t, BUFFER_SIZE> buffer_{};
};
