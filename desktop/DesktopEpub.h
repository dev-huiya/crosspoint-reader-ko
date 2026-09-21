#pragma once

#include <miniz.h>

#include <cstddef>
#include <string>
#include <vector>

class DesktopEpub {
 public:
  ~DesktopEpub();
  bool open(const char* archivePath);
  bool extractChapter(size_t index, const char* outputPath);
  size_t chapterCount() const { return chapters_.size(); }

 private:
  bool readText(const std::string& entry, std::string& output);
  mz_zip_archive archive_{};
  bool opened_ = false;
  std::vector<std::string> chapters_;
};
