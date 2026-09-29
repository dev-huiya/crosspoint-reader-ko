#include "Txt.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <SiblingCover.h>

Txt::Txt(std::string path, std::string cacheBasePath)
    : filepath(std::move(path)), cacheBasePath(std::move(cacheBasePath)) {
  // Generate cache path from file path hash
  const size_t hash = std::hash<std::string>{}(filepath);
  cachePath = this->cacheBasePath + "/txt_" + std::to_string(hash);
}

bool Txt::load() {
  if (loaded) {
    return true;
  }

  if (!Storage.exists(filepath.c_str())) {
    LOG_ERR("TXT", "File does not exist: %s", filepath.c_str());
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("TXT", filepath, file)) {
    LOG_ERR("TXT", "Failed to open file: %s", filepath.c_str());
    return false;
  }

  fileSize = file.size();
  file.close();

  loaded = true;
  LOG_DBG("TXT", "Loaded TXT file: %s (%zu bytes)", filepath.c_str(), fileSize);
  return true;
}

std::string Txt::getTitle() const {
  // Extract filename without path and extension
  size_t lastSlash = filepath.find_last_of('/');
  std::string filename = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1) : filepath;

  // Remove .txt extension
  if (FsHelpers::hasTxtExtension(filename)) {
    filename.resize(filename.length() - 4);
  }

  return filename;
}

void Txt::setupCacheDir() const {
  if (!Storage.exists(cacheBasePath.c_str())) {
    Storage.mkdir(cacheBasePath.c_str());
  }
  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  }
}

std::string Txt::findCoverImage() const { return sibling_cover::findImage(filepath); }

std::string Txt::siblingCoverImage(const bool recheck) const {
  return sibling_cover::resolve(filepath, cachePath, recheck);
}

std::string Txt::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

std::string Txt::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }

std::string Txt::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }

bool Txt::generateCoverBmp(bool bookOpen) const {
  if (!bookOpen && Storage.exists(getCoverBmpPath().c_str())) return true;
  // Opening the book rechecks the folder; a changed image drops the cached cover.
  const std::string imagePath = siblingCoverImage(bookOpen);
  if (Storage.exists(getCoverBmpPath().c_str())) return true;
  if (imagePath.empty()) {
    LOG_DBG("TXT", "No cover image found for TXT file");
    return false;
  }
  setupCacheDir();
  return sibling_cover::writeCoverBmp(imagePath, getCoverBmpPath(), true, false);
}

bool Txt::generateThumbBmp(int height) const {
  if (Storage.exists(getThumbBmpPath(height).c_str())) return true;
  const std::string imagePath = siblingCoverImage();
  if (imagePath.empty()) return false;
  setupCacheDir();
  return sibling_cover::writeThumbBmp(imagePath, getThumbBmpPath(height), height);
}

bool Txt::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("TXT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("TXT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("TXT", "Cache cleared successfully");
  return true;
}

bool Txt::readContent(uint8_t* buffer, size_t offset, size_t length) const {
  if (!loaded) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("TXT", filepath, file)) {
    return false;
  }

  if (!file.seek(offset)) {
    return false;
  }

  size_t bytesRead = file.read(buffer, length);
  return bytesRead > 0;
}
