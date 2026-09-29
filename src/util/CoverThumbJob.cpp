#include "CoverThumbJob.h"

#include <Arduino.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Txt.h>
#include <Xtc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "CrossPointState.h"

namespace cover_job {
namespace {

constexpr const char* CACHE_ROOT = "/.crosspoint";
// Content.opf parsing and the decoders keep their large state on the heap;
// this covers their call chains.
constexpr uint32_t TASK_STACK_BYTES = 10240;
// Below the render and main-loop tasks (priority 1): the job only runs while
// they wait, so page turns and input never queue behind a conversion.
constexpr UBaseType_t TASK_PRIORITY = tskIDLE_PRIORITY;
// Let the reader finish its first page and settle before competing for the SD card.
constexpr uint32_t START_DELAY_MS = 1500;
// A conversion needs its decoder plus headroom for the reader's own allocations.
constexpr size_t MIN_LARGEST_BLOCK = 48 * 1024;

SemaphoreHandle_t stateMutex() {
  static StaticSemaphore_t storage;
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&storage);
  return mutex;
}

// Guarded by stateMutex().
std::string queuedPath;
bool running = false;
volatile bool paused = false;

struct Lock {
  Lock() { xSemaphoreTake(stateMutex(), portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(stateMutex()); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

bool enoughHeap() {
  const auto heap = HalMemory::getInternalHeap();
  if (heap.largestBlockBytes >= MIN_LARGEST_BLOCK) return true;
  LOG_INF("COVJOB", "Skipping cover build: largest free block %zu bytes", heap.largestBlockBytes);
  return false;
}

void buildEpub(const std::string& path, const int height) {
  auto epub = makeUniqueNoThrow<Epub>(path, CACHE_ROOT);
  if (!epub) {
    LOG_ERR("COVJOB", "OOM: Epub");
    return;
  }
  (void)epub->siblingCoverImage(true);
  if (paused || height <= 0 || Storage.exists(epub->getThumbBmpPath(height).c_str()) || !enoughHeap()) return;
  (void)epub->generateThumbBmpFromSource(height);
}

void buildXtc(const std::string& path, const int height) {
  auto xtc = makeUniqueNoThrow<Xtc>(path, CACHE_ROOT);
  if (!xtc) {
    LOG_ERR("COVJOB", "OOM: Xtc");
    return;
  }
  const bool sibling = !xtc->siblingCoverImage(true).empty();
  if (paused || height <= 0 || Storage.exists(xtc->getThumbBmpPath(height).c_str()) || !enoughHeap()) return;
  if (sibling && xtc->generateThumbBmp(height)) return;
  if (!paused && xtc->load()) xtc->generateThumbBmp(height);
}

void buildTxt(const std::string& path, const int height) {
  Txt txt(path, CACHE_ROOT);
  if (!enoughHeap()) return;
  // Rechecks the folder and builds the sleep screen's cover.bmp.
  if (!txt.generateCoverBmp(true) || paused || height <= 0) return;
  (void)txt.generateThumbBmp(height);
}

void build(const std::string& path) {
  const int height = APP_STATE.homeCoverThumbHeight;
  const unsigned long start = millis();
  if (FsHelpers::hasEpubExtension(path)) {
    buildEpub(path, height);
  } else if (FsHelpers::hasXtcExtension(path)) {
    buildXtc(path, height);
  } else if (FsHelpers::hasTxtExtension(path)) {
    buildTxt(path, height);
  }
  LOG_INF("COVJOB", "Cover cache for %s (thumb %d px): %lu ms, stack left %u", path.c_str(), height,
          millis() - start, static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

// Takes the next queued book; clears `running` when there is none (or work is
// paused), in which case the task must end.
bool takeNext(std::string& path) {
  Lock lock;
  if (paused || queuedPath.empty()) {
    running = false;
    return false;
  }
  path = std::move(queuedPath);
  queuedPath.clear();
  return true;
}

void taskMain(void*) {
  vTaskDelay(pdMS_TO_TICKS(START_DELAY_MS));
  {
    // Scoped so the string is freed before vTaskDelete(), which never returns.
    std::string path;
    while (takeNext(path)) {
      build(path);
    }
  }
  vTaskDelete(nullptr);
}

// Caller holds the lock.
void startLocked() {
  if (running || paused || queuedPath.empty()) return;
  running = true;
  if (xTaskCreate(&taskMain, "CoverJob", TASK_STACK_BYTES, nullptr, TASK_PRIORITY, nullptr) != pdPASS) {
    running = false;
    LOG_ERR("COVJOB", "Could not start the cover task");
  }
}

}  // namespace

void schedule(const std::string& bookPath) {
  Lock lock;
  queuedPath = bookPath;
  startLocked();
}

bool pause(const uint32_t timeoutMs) {
  paused = true;
  const unsigned long start = millis();
  while (true) {
    {
      Lock lock;
      if (!running) return true;
    }
    if (millis() - start >= timeoutMs) {
      LOG_ERR("COVJOB", "Cover task still running after %lu ms", static_cast<unsigned long>(timeoutMs));
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void resume() {
  Lock lock;
  paused = false;
  startLocked();
}

}  // namespace cover_job
