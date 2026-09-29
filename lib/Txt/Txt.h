#pragma once

#include <HalStorage.h>

#include <memory>
#include <string>

class Txt {
  std::string filepath;
  std::string cacheBasePath;
  std::string cachePath;
  bool loaded = false;
  size_t fileSize = 0;

 public:
  explicit Txt(std::string path, std::string cacheBasePath);

  bool load();
  [[nodiscard]] const std::string& getPath() const { return filepath; }
  [[nodiscard]] const std::string& getCachePath() const { return cachePath; }
  [[nodiscard]] std::string getTitle() const;
  [[nodiscard]] size_t getFileSize() const { return fileSize; }

  void setupCacheDir() const;
  bool clearCache() const;

  // Cover image: the same-name image beside the file (lib/SiblingCover). The
  // folder is scanned again only when the book opens (bookOpen).
  [[nodiscard]] std::string getCoverBmpPath() const;
  [[nodiscard]] std::string getThumbBmpPath() const;
  [[nodiscard]] std::string getThumbBmpPath(int height) const;
  [[nodiscard]] bool generateCoverBmp(bool bookOpen = false) const;
  [[nodiscard]] bool generateThumbBmp(int height) const;
  [[nodiscard]] std::string findCoverImage() const;
  // Cached lookup of findCoverImage() (lib/SiblingCover); `recheck` rescans.
  [[nodiscard]] std::string siblingCoverImage(bool recheck = false) const;

  // Read content from file
  [[nodiscard]] bool readContent(uint8_t* buffer, size_t offset, size_t length) const;
};
