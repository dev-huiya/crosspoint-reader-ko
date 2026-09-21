#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CapturedLayoutLine {
  std::vector<std::string> words;
  std::vector<int16_t> x;
};

extern std::vector<CapturedLayoutLine> capturedLayoutLines;
