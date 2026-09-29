#pragma once

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct CapturedLayoutLine {
  std::vector<std::string> words;
  std::vector<int16_t> x;
};

extern std::vector<CapturedLayoutLine> capturedLayoutLines;

// The parser test links the real TextBlock and Page, so lines are recorded
// from the emitted blocks (a ParsedText line callback) or from the pages the
// parser completes, instead of from a stubbed TextBlock constructor.
inline void captureLayoutLine(const TextBlock& block) {
  CapturedLayoutLine line;
  line.words.reserve(block.wordCount());
  line.x.reserve(block.wordCount());
  for (uint16_t i = 0; i < block.wordCount(); ++i) {
    line.words.emplace_back(block.wordText(i));
    line.x.push_back(block.wordXpos(i));
  }
  capturedLayoutLines.push_back(std::move(line));
}

inline void captureLayoutPage(const Page& page) {
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const TextBlock* block = static_cast<const PageLine&>(*element).getBlock();
    if (block) captureLayoutLine(*block);
  }
}

// ctest runs every test case in its own process, in parallel, so a temp file
// shared by name would be rewritten under another case. Key it on the case.
inline std::string perTestTempPath(const char* stem) {
  const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
  std::string name = std::string(stem) + "-" + (info ? info->name() : "none");
  for (char& c : name) {
    if (c == '/' || c == '\\') c = '_';
  }
  return (std::filesystem::temp_directory_path() / (name + ".xhtml")).generic_string();
}
