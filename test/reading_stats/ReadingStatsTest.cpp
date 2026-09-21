#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "ReadingStats.h"
#include "ReadingTimer.h"

uint32_t fakeMillis = 0;

TEST(ReadingStats, ReadsLegacyV1AndAccumulatesOnlyActiveSeconds) {
  const auto dir = std::filesystem::temp_directory_path() / "crosspoint-reading-stats-test";
  std::filesystem::create_directories(dir);
  const auto path = dir / "reading_stats.bin";
  const uint8_t legacy[] = {'T', 'I', 'M', 'E', 1, 0, 0, 0, 120, 0, 0, 0, 2, 0, 0, 0};
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(legacy), sizeof(legacy));
  }

  fakeMillis = 0;
  ReadingTimer timer;
  timer.start(dir.generic_string());
  EXPECT_EQ(timer.totalSeconds(), 120u);
  for (int i = 0; i < 31; ++i) {
    fakeMillis += 1000;
    timer.tick();
  }
  timer.pause();
  fakeMillis += 20000;
  timer.pause();
  timer.notifyInput();
  fakeMillis += 1000;
  timer.tick();
  timer.stop();

  ReadingStats saved;
  ASSERT_TRUE(ReadingStats::load(dir.generic_string(), saved));
  EXPECT_EQ(saved.totalSeconds, 152u);
  EXPECT_EQ(saved.sessionCount, 3u);
  std::filesystem::remove(path);
  std::filesystem::remove(dir);
}
