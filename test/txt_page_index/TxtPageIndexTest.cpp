// TXT reader page index (src/activities/reader/TxtPageIndex.h): exact page
// numbers where the background build has reached, estimates beyond it.
#include <gtest/gtest.h>

#include "TxtPageIndex.h"

namespace {

constexpr uint32_t FILE_SIZE = 10000;

// Build `pages` pages of 1000 bytes each from the start.
txt_index::PageIndex indexWithPages(const int pages) {
  txt_index::PageIndex index;
  index.reset();
  for (int i = 0; i < pages; ++i) index.addPageEnd((i + 1) * 1000, FILE_SIZE);
  return index;
}

TEST(TxtPageIndex, FreshIndexKnowsOnlyTheFirstPageStart) {
  txt_index::PageIndex index;
  index.reset();
  EXPECT_EQ(index.knownPages(), 0);
  EXPECT_EQ(index.pageContaining(0), 0);
  EXPECT_EQ(index.pageContaining(1), -1);
  // No estimate at all yet: one page, and every offset is on it.
  EXPECT_EQ(index.totalPages(FILE_SIZE, 0), 1);
  EXPECT_EQ(index.pageFor(5000, 0), 0);
}

TEST(TxtPageIndex, FallbackSeedsTheEstimate) {
  txt_index::PageIndex index;
  index.reset();
  EXPECT_EQ(index.totalPages(FILE_SIZE, 1000), 10);
  EXPECT_EQ(index.pageFor(5000, 1000), 5);
  EXPECT_EQ(index.offsetForPage(5, FILE_SIZE, 1000), 5000u);
}

TEST(TxtPageIndex, PartialIndexIsExactWhereReached) {
  const auto index = indexWithPages(3);  // pages 0..2 known, page 3 starts at 3000
  EXPECT_FALSE(index.complete);
  EXPECT_EQ(index.knownPages(), 3);
  EXPECT_EQ(index.indexedEnd, 3000u);
  EXPECT_EQ(index.pageContaining(0), 0);
  EXPECT_EQ(index.pageContaining(999), 0);
  EXPECT_EQ(index.pageContaining(1000), 1);
  EXPECT_EQ(index.pageContaining(2999), 2);
  EXPECT_EQ(index.pageContaining(3000), 3);  // known start of the page under construction
  EXPECT_EQ(index.pageContaining(3001), -1);
}

TEST(TxtPageIndex, PartialIndexEstimatesFromItsOwnAverage) {
  const auto index = indexWithPages(3);
  // Average is 1000 bytes per page regardless of the fallback.
  EXPECT_EQ(index.bytesPerPage(1), 1000u);
  EXPECT_EQ(index.totalPages(FILE_SIZE, 1), 10);
  EXPECT_EQ(index.pageFor(3000, 1), 3);
  EXPECT_EQ(index.pageFor(3999, 1), 3);
  EXPECT_EQ(index.pageFor(4000, 1), 4);
  EXPECT_EQ(index.offsetForPage(2, FILE_SIZE, 1), 2000u);
  EXPECT_EQ(index.offsetForPage(3, FILE_SIZE, 1), 3000u);
  EXPECT_EQ(index.offsetForPage(7, FILE_SIZE, 1), 7000u);
  // Past the end clamps to the last byte.
  EXPECT_EQ(index.offsetForPage(50, FILE_SIZE, 1), FILE_SIZE - 1);
}

TEST(TxtPageIndex, UnevenPagesRoundTheTotalUp) {
  txt_index::PageIndex index;
  index.reset();
  index.addPageEnd(1500, FILE_SIZE);
  index.addPageEnd(2500, FILE_SIZE);  // average 1250; 7500 bytes remain -> 6 pages
  EXPECT_EQ(index.totalPages(FILE_SIZE, 1), 8);
}

TEST(TxtPageIndex, CompletionCoversTheFile) {
  auto index = indexWithPages(9);
  EXPECT_FALSE(index.complete);
  index.addPageEnd(FILE_SIZE, FILE_SIZE);
  EXPECT_TRUE(index.complete);
  EXPECT_EQ(index.knownPages(), 10);
  EXPECT_EQ(index.starts.size(), 10u);
  EXPECT_EQ(index.indexedEnd, FILE_SIZE);
  EXPECT_EQ(index.totalPages(FILE_SIZE, 1), 10);
  EXPECT_EQ(index.pageContaining(FILE_SIZE - 1), 9);
  EXPECT_EQ(index.pageFor(FILE_SIZE - 1, 1), 9);
  EXPECT_EQ(index.offsetForPage(9, FILE_SIZE, 1), 9000u);
  EXPECT_EQ(index.offsetForPage(10, FILE_SIZE, 1), FILE_SIZE - 1);
  // Further ends are ignored once complete.
  index.addPageEnd(FILE_SIZE + 5, FILE_SIZE);
  EXPECT_EQ(index.starts.size(), 10u);
}

TEST(TxtPageIndex, PageNumberNeverExceedsTheTotal) {
  const auto index = indexWithPages(3);
  const int total = index.totalPages(FILE_SIZE, 1);
  for (uint32_t offset = 0; offset < FILE_SIZE; offset += 250) {
    EXPECT_LT(index.pageFor(offset, 1), total) << "offset " << offset;
  }
}

}  // namespace
