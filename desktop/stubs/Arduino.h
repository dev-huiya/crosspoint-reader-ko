#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

inline uint32_t millis() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}
inline uint32_t micros() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}
inline void delay(uint32_t milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }
struct DesktopEsp {
  void restart() const {}
  size_t getFreeHeap() const { return 16 * 1024 * 1024; }
  size_t getMaxAllocHeap() const { return 16 * 1024 * 1024; }
};
inline DesktopEsp ESP;
