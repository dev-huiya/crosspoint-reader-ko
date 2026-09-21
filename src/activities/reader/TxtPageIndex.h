#pragma once

// Page index for the TXT reader, kept free of the renderer so the host tests
// can drive it. The reader navigates by byte offset (as KO 1.5 did) and this
// index is filled in the background, one page at a time, so a page number can
// be exact once the index has passed the reader's position and a bytes-per-page
// estimate before that.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace txt_index {

struct PageIndex {
  // Byte offset where each known page starts; starts[0] is 0. The last entry
  // is the page the builder is on next: its start is known, its end is not.
  std::vector<uint32_t> starts;
  // Bytes covered so far: every page that starts before this is fully known.
  // Equals starts.back() while building and fileSize once complete.
  uint32_t indexedEnd = 0;
  bool complete = false;

  void reset() {
    starts.assign(1, 0);
    indexedEnd = 0;
    complete = false;
  }

  bool empty() const { return starts.empty(); }

  // Pages whose extent is known (the page under construction is not counted).
  int knownPages() const {
    if (starts.empty()) return 0;
    return complete ? static_cast<int>(starts.size()) : static_cast<int>(starts.size()) - 1;
  }

  // Record that the page starting at starts.back() ends at `nextStart`.
  void addPageEnd(const uint32_t nextStart, const uint32_t fileSize) {
    if (complete || starts.empty()) return;
    if (nextStart >= fileSize) {
      indexedEnd = fileSize;
      complete = true;
      return;
    }
    starts.push_back(nextStart);
    indexedEnd = nextStart;
  }

  // Index of the page containing `offset`, or -1 when the index has not
  // reached it. A page start counts as reached as soon as it is known.
  int pageContaining(const uint32_t offset) const {
    if (starts.empty() || offset > indexedEnd) return -1;
    const auto it = std::upper_bound(starts.begin(), starts.end(), offset);
    if (it == starts.begin()) return -1;
    return static_cast<int>(it - starts.begin()) - 1;
  }

  // Average bytes of a known page; `fallback` when nothing is known yet.
  uint32_t bytesPerPage(const uint32_t fallback) const {
    const int known = knownPages();
    if (known <= 0 || indexedEnd == 0) return fallback;
    const uint32_t average = indexedEnd / static_cast<uint32_t>(known);
    return average > 0 ? average : fallback;
  }

  int totalPages(const uint32_t fileSize, const uint32_t fallbackBytesPerPage) const {
    if (complete) return std::max(1, static_cast<int>(starts.size()));
    const uint32_t perPage = bytesPerPage(fallbackBytesPerPage);
    if (perPage == 0) return std::max(1, static_cast<int>(starts.size()));
    const uint32_t remaining = fileSize > indexedEnd ? fileSize - indexedEnd : 0;
    const int estimated = knownPages() + static_cast<int>((remaining + perPage - 1) / perPage);
    return std::max(1, estimated);
  }

  // 0-based page holding `offset`: exact when reached, estimated otherwise.
  int pageFor(const uint32_t offset, const uint32_t fallbackBytesPerPage) const {
    const int exact = pageContaining(offset);
    if (exact >= 0) return exact;
    const uint32_t perPage = bytesPerPage(fallbackBytesPerPage);
    if (perPage == 0) return knownPages();
    return knownPages() + static_cast<int>((offset - indexedEnd) / perPage);
  }

  // Byte offset for 0-based `page`: exact when known, extrapolated otherwise.
  // The caller snaps an extrapolated offset to a line start.
  uint32_t offsetForPage(const int page, const uint32_t fileSize, const uint32_t fallbackBytesPerPage) const {
    if (page <= 0 || starts.empty()) return 0;
    if (page < static_cast<int>(starts.size())) return starts[page];
    const uint32_t perPage = bytesPerPage(fallbackBytesPerPage);
    const uint32_t beyond = static_cast<uint32_t>(page - knownPages());
    const uint64_t estimated = static_cast<uint64_t>(indexedEnd) + static_cast<uint64_t>(beyond) * perPage;
    const uint32_t last = fileSize > 0 ? fileSize - 1 : 0;
    return estimated >= last ? last : static_cast<uint32_t>(estimated);
  }
};

}  // namespace txt_index
