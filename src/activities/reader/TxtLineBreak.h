#pragma once

// Line breaking for the TXT reader, kept free of the renderer so the host
// tests can drive it with fake metrics. `measure(prefix)` returns the pixel
// width of a UTF-8 prefix of the line.

#include <cstddef>
#include <string>

namespace txt_layout {

// Move `pos` back to the start of the UTF-8 sequence it points into.
inline size_t findUtf8Boundary(const std::string& str, size_t pos) {
  if (pos >= str.length()) return str.length();
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) pos--;
  return pos;
}

// Byte offset at which `line` must break so the prefix fits `maxWidth`.
// Returns line.length() when the whole line fits. Binary search over UTF-8
// boundaries: every probe costs a text measurement, which for SD fonts is a
// glyph-table walk. With character wrap (the Korean default) the break lands
// on the last fitting character; otherwise the last fitting space wins so
// Latin words stay whole, as upstream did.
template <typename Measure>
size_t findBreakPosition(const std::string& line, const int maxWidth, const bool characterWrap, Measure&& measure) {
  if (line.empty()) return 0;
  if (measure(line) <= maxWidth) return line.length();

  // Always consume one whole codepoint: splitting a sequence would leave
  // continuation bytes at the head of the next page.
  size_t firstCharEnd = 1;
  while (firstCharEnd < line.length() && (static_cast<unsigned char>(line[firstCharEnd]) & 0xC0) == 0x80) {
    firstCharEnd++;
  }

  size_t low = firstCharEnd;
  size_t high = line.length();
  size_t bestFit = firstCharEnd;
  while (low < high) {
    const size_t mid = findUtf8Boundary(line, (low + high + 1) / 2);
    if (mid <= low) break;
    if (measure(line.substr(0, mid)) <= maxWidth) {
      bestFit = mid;
      low = mid;
    } else {
      high = findUtf8Boundary(line, mid - 1);
      if (high < low) high = low;
    }
  }

  if (!characterWrap && bestFit < line.length()) {
    const size_t spacePos = line.rfind(' ', bestFit);
    if (spacePos != std::string::npos && spacePos > 0) return spacePos;
  }
  return bestFit;
}

// Number of codepoints, for spreading justification slack between glyphs.
inline int utf8Length(const std::string& text) {
  int count = 0;
  for (const char c : text) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
  }
  return count;
}

}  // namespace txt_layout
