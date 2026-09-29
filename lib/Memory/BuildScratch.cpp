#include "BuildScratch.h"

#include <Logging.h>

#include <atomic>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace buildscratch {
namespace {
uint8_t* block = nullptr;
size_t blockLen = 0;
// atomic exchange so an opportunistic claim from another task can never
// double-hand-out the block (single core, but FreeRTOS preempts).
std::atomic<bool> claimed{false};
#ifdef ESP_PLATFORM
// Only the lending task may claim: the framebuffer comes back when its loan
// ends, which another task (e.g. the background cover job) cannot see.
TaskHandle_t lender = nullptr;
bool fromLender() { return xTaskGetCurrentTaskHandle() == lender; }
#else
bool fromLender() { return true; }
#endif
}  // namespace

void lend(uint8_t* buf, const size_t len) {
  if (block) {
    LOG_ERR("SCR", "Build scratch lent twice; ignoring second lend");
    return;
  }
  block = buf;
  blockLen = len;
#ifdef ESP_PLATFORM
  lender = xTaskGetCurrentTaskHandle();
#endif
  claimed.store(false);
}

void reclaim() {
  if (claimed.load()) {
    // A consumer still holds the block. The storage stays valid (it is the
    // framebuffer allocation, never freed) but its contents are about to be
    // clobbered; the consumer's output will be garbage. Loud log so a
    // lifetime bug is visible instead of a silent corrupt decode.
    LOG_ERR("SCR", "Build scratch reclaimed while still claimed");
  }
  block = nullptr;
  blockLen = 0;
  claimed.store(false);
}

uint8_t* claim(const size_t minLen, size_t* lenOut) {
  if (!block || blockLen < minLen || !fromLender()) return nullptr;
  bool expected = false;
  if (!claimed.compare_exchange_strong(expected, true)) return nullptr;
  if (lenOut) *lenOut = blockLen;
  return block;
}

void release(const uint8_t* p) {
  if (p && p == block) claimed.store(false);
}

}  // namespace buildscratch
