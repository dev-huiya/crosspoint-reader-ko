#include "Txt.h"

#include <FsHelpers.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <PngToBmpConverter.h>

#include <string_view>

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

std::string Txt::findCoverImage() const {
  // Get the folder containing the txt file
  size_t lastSlash = filepath.find_last_of('/');
  std::string folder = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash) : "";
  if (folder.empty()) {
    folder = "/";
  }

  // Get the base filename without extension (e.g., "mybook" from "/books/mybook.txt")
  std::string baseName = getTitle();

  // One bounded directory pass, without retaining its entries in RAM.
  auto dir = Storage.open(folder.c_str());
  if (!dir || !dir.isDirectory()) return "";
  char name[128];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(name, sizeof(name));
    const std::string_view candidate{name};
    const size_t dot = candidate.find_last_of('.');
    if (dot == std::string_view::npos || candidate.substr(0, dot) != baseName) continue;
    if (FsHelpers::hasBmpExtension(candidate) || FsHelpers::hasJpgExtension(candidate) ||
        FsHelpers::hasPngExtension(candidate)) {
      return folder + (folder == "/" ? "" : "/") + name;
    }
  }

  return "";
}

std::string Txt::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

std::string Txt::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }

std::string Txt::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }

bool Txt::generateCoverBmp(bool bookOpen) const {
  // Already generated, return true
  if (Storage.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  const std::string missingPath = cachePath + "/cover.missing";
  if (!bookOpen && Storage.exists(missingPath.c_str())) return false;
  const auto markAttempted = [this, &missingPath]() {
    setupCacheDir();
    HalFile missing;
    if (!Storage.openFileForWrite("TXT", missingPath, missing))
      LOG_ERR("TXT", "Failed to cache unavailable cover state");
  };

  std::string coverImagePath = findCoverImage();
  if (coverImagePath.empty()) {
    LOG_DBG("TXT", "No cover image found for TXT file");
    markAttempted();
    return false;
  }

  if (Storage.exists(missingPath.c_str())) Storage.remove(missingPath.c_str());

  // Setup cache directory
  setupCacheDir();

  if (FsHelpers::hasBmpExtension(coverImagePath)) {
    // Copy BMP file to cache
    LOG_DBG("TXT", "Copying BMP cover image to cache");
    HalFile src, dst;
    if (!Storage.openFileForRead("TXT", coverImagePath, src)) {
      markAttempted();
      return false;
    }
    if (!Storage.openFileForWrite("TXT", getCoverBmpPath(), dst)) {
      markAttempted();
      return false;
    }
    uint8_t buffer[128];
    while (src.available()) {
      size_t bytesRead = src.read(buffer, sizeof(buffer));
      if (!bytesRead || dst.write(buffer, bytesRead) != bytesRead) {
        src.close();
        dst.close();
        Storage.remove(getCoverBmpPath().c_str());
        markAttempted();
        return false;
      }
    }
    LOG_DBG("TXT", "Copied BMP cover to cache");
    return true;
  } else if (FsHelpers::hasJpgExtension(coverImagePath)) {
    // Convert JPG/JPEG to BMP (same approach as Epub)
    LOG_DBG("TXT", "Generating BMP from JPG cover image");
    HalFile coverJpg, coverBmp;
    if (!Storage.openFileForRead("TXT", coverImagePath, coverJpg)) {
      markAttempted();
      return false;
    }
    if (!Storage.openFileForWrite("TXT", getCoverBmpPath(), coverBmp)) {
      markAttempted();
      return false;
    }
    const bool success = JpegToBmpConverter::jpegFileToBmpStream(coverJpg, coverBmp);

    if (!success) {
      LOG_ERR("TXT", "Failed to generate BMP from JPG cover image");
      coverJpg.close();
      coverBmp.close();
      Storage.remove(getCoverBmpPath().c_str());
      markAttempted();
    } else {
      LOG_DBG("TXT", "Generated BMP from JPG cover image");
    }
    return success;
  } else if (FsHelpers::hasPngExtension(coverImagePath)) {
    HalFile coverPng, coverBmp;
    if (!Storage.openFileForRead("TXT", coverImagePath, coverPng) ||
        !Storage.openFileForWrite("TXT", getCoverBmpPath(), coverBmp)) {
      markAttempted();
      return false;
    }
    const bool success = PngToBmpConverter::pngFileToBmpStream(coverPng, coverBmp);
    if (!success) {
      LOG_ERR("TXT", "Failed to generate BMP from PNG cover image");
      coverPng.close();
      coverBmp.close();
      Storage.remove(getCoverBmpPath().c_str());
      markAttempted();
    }
    return success;
  }

  LOG_ERR("TXT", "Cover image format not supported");
  markAttempted();
  return false;
}

bool Txt::generateThumbBmp(int height) const {
  const std::string thumbPath = getThumbBmpPath(height);
  if (Storage.exists(thumbPath.c_str())) return true;
  std::string imagePath = findCoverImage();
  if (imagePath.empty() && Storage.exists(getCoverBmpPath().c_str())) imagePath = getCoverBmpPath();
  if (imagePath.empty()) return false;
  HalFile image, thumb;
  if (!Storage.openFileForRead("TXT", imagePath, image) ||
      !Storage.openFileForWrite("TXT", thumbPath, thumb)) return false;
  bool success = false;
  if (FsHelpers::hasJpgExtension(imagePath)) {
    success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(image, thumb, height * 3 / 5, height);
  } else if (FsHelpers::hasPngExtension(imagePath)) {
    success = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(image, thumb, height * 3 / 5, height);
  } else if (FsHelpers::hasBmpExtension(imagePath)) {
    uint8_t buffer[128];
    success = true;
    while (image.available()) {
      const size_t read = image.read(buffer, sizeof(buffer));
      if (!read || thumb.write(buffer, read) != read) { success = false; break; }
    }
  }
  image.close();
  thumb.close();
  if (!success) Storage.remove(thumbPath.c_str());
  return success;
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
