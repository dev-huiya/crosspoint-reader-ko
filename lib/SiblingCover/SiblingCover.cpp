#include "SiblingCover.h"

#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace sibling_cover {
namespace {

constexpr const char* MARKER_NAME = "/sibling.src";
constexpr const char* MISSING_NAME = "/cover.missing";
// Directory entry names and the marker hold full long file names (UTF-8, up
// to 255 bytes), so their buffers live on the heap for the call.
constexpr size_t NAME_BUFFER_SIZE = 256;
constexpr size_t MARKER_BUFFER_SIZE = NAME_BUFFER_SIZE + 16;

struct Record {
  bool known = false;  // a lookup result is cached
  std::string path;    // "" when the last lookup found no image
  size_t size = 0;
};

size_t fileSizeOf(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("SIB", path, file)) return 0;
  return file.size();
}

Record readRecord(const std::string& cacheDir) {
  Record record;
  if (Storage.exists((cacheDir + MISSING_NAME).c_str())) {
    record.known = true;
    return record;
  }
  HalFile marker;
  if (!Storage.exists((cacheDir + MARKER_NAME).c_str()) ||
      !Storage.openFileForRead("SIB", cacheDir + MARKER_NAME, marker)) {
    return record;
  }
  auto buffer = makeUniqueNoThrow<char[]>(MARKER_BUFFER_SIZE);
  if (!buffer) {
    LOG_ERR("SIB", "OOM: cover marker");
    return record;
  }
  const int read = marker.read(buffer.get(), MARKER_BUFFER_SIZE - 1);
  if (read <= 0) return record;
  buffer[read] = '\0';
  char* newline = nullptr;
  const unsigned long size = strtoul(buffer.get(), &newline, 10);
  if (!newline || *newline != '\n' || newline[1] != '/') return record;
  record.known = true;
  record.size = size;
  record.path = newline + 1;
  return record;
}

void writeRecord(const std::string& cacheDir, const std::string& imagePath, const size_t size) {
  Storage.mkdir(cacheDir.c_str());
  if (imagePath.empty()) {
    Storage.remove((cacheDir + MARKER_NAME).c_str());
    HalFile missing;
    if (!Storage.openFileForWrite("SIB", cacheDir + MISSING_NAME, missing)) {
      LOG_ERR("SIB", "Failed to record missing cover in %s", cacheDir.c_str());
    }
    return;
  }
  Storage.remove((cacheDir + MISSING_NAME).c_str());
  HalFile marker;
  if (!Storage.openFileForWrite("SIB", cacheDir + MARKER_NAME, marker)) {
    LOG_ERR("SIB", "Failed to record cover source in %s", cacheDir.c_str());
    return;
  }
  char header[16];
  const int length = snprintf(header, sizeof(header), "%u\n", static_cast<unsigned>(size));
  marker.write(header, static_cast<size_t>(length));
  marker.write(imagePath.data(), imagePath.size());
}

// Removes the covers and thumbnails built from the previous source, one entry
// per directory pass: removing entries while iterating is not safe, and a
// cache directory holds only a handful of them.
void clearCachedCovers(const std::string& cacheDir) {
  char name[64];
  int removed = 0;
  for (; removed < 32; ++removed) {
    bool found = false;
    {
      auto dir = Storage.open(cacheDir.c_str());
      if (!dir || !dir.isDirectory()) break;
      for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
        if (entry.isDirectory()) continue;
        entry.getName(name, sizeof(name));
        const std::string_view view{name};
        const bool bmp = view.size() > 4 && equalsIgnoreAsciiCase(view.substr(view.size() - 4), ".bmp");
        if (bmp && (view.rfind("cover", 0) == 0 || view.rfind("thumb_", 0) == 0)) {
          found = true;
          break;
        }
      }
    }
    if (!found || !Storage.remove((cacheDir + "/" + name).c_str())) break;
  }
  LOG_DBG("SIB", "Cleared %d cached covers in %s", removed, cacheDir.c_str());
}

bool copyFile(HalFile& src, HalFile& dst) {
  uint8_t buffer[128];
  while (src.available()) {
    const int read = src.read(buffer, sizeof(buffer));
    if (read <= 0 || dst.write(buffer, static_cast<size_t>(read)) != static_cast<size_t>(read)) return false;
  }
  return true;
}

}  // namespace

std::string findImage(const std::string& bookPath) {
  const size_t slash = bookPath.find_last_of('/');
  const std::string folder = slash == std::string::npos || slash == 0 ? "/" : bookPath.substr(0, slash);
  const std::string_view bookFileName = fileNameOf(bookPath);
  auto name = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!name) {
    LOG_ERR("SIB", "OOM: cover lookup");
    return "";
  }
  auto dir = Storage.open(folder.c_str());
  if (!dir || !dir.isDirectory()) return "";
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    entry.getName(name.get(), NAME_BUFFER_SIZE);
    if (isSiblingCoverName(name.get(), bookFileName)) {
      return folder + (folder == "/" ? "" : "/") + name.get();
    }
  }
  return "";
}

std::string resolve(const std::string& bookPath, const std::string& cacheDir, const bool recheck) {
  const Record record = readRecord(cacheDir);
  if (!recheck && record.known) {
    if (record.path.empty()) return "";
    // The recorded image is still there unchanged; otherwise look again.
    if (Storage.exists(record.path.c_str()) && fileSizeOf(record.path) == record.size) return record.path;
  }
  std::string image = findImage(bookPath);
  // The marker could not hold the path; a cover that cannot be recorded
  // would be rebuilt on every render, so such an image is not used.
  if (image.size() > MARKER_BUFFER_SIZE - 17) {
    LOG_ERR("SIB", "Cover path too long: %s", image.c_str());
    image.clear();
  }
  const size_t size = image.empty() ? 0 : fileSizeOf(image);
  const bool changed = image != record.path || size != record.size;
  // Covers cached before any lookup came from the book itself; they stay
  // valid when there is no sibling image either.
  if (changed && (record.known || !image.empty())) clearCachedCovers(cacheDir);
  if (changed || !record.known) writeRecord(cacheDir, image, size);
  return image;
}

bool writeCoverBmp(const std::string& imagePath, const std::string& outPath, const bool cropped,
                   const bool originalThresholds) {
  HalFile image, bmp;
  if (!Storage.openFileForRead("SIB", imagePath, image) || !Storage.openFileForWrite("SIB", outPath, bmp)) {
    return false;
  }
  bool success = false;
  switch (imageTypeOf(imagePath)) {
    case ImageType::Jpeg:
      success = JpegToBmpConverter::jpegFileToBmpStream(image, bmp, cropped, originalThresholds);
      break;
    case ImageType::Png:
      success = PngToBmpConverter::pngFileToBmpStream(image, bmp, cropped, originalThresholds);
      break;
    case ImageType::Bmp:
      success = copyFile(image, bmp);
      break;
    case ImageType::None:
      break;
  }
  image.close();
  bmp.close();
  if (!success) {
    LOG_ERR("SIB", "Failed to convert cover %s", imagePath.c_str());
    Storage.remove(outPath.c_str());
  }
  return success;
}

bool writeThumbBmp(const std::string& imagePath, const std::string& outPath, const int height) {
  HalFile image, thumb;
  if (!Storage.openFileForRead("SIB", imagePath, image) || !Storage.openFileForWrite("SIB", outPath, thumb)) {
    return false;
  }
  const int width = height * 3 / 5;
  bool success = false;
  switch (imageTypeOf(imagePath)) {
    case ImageType::Jpeg:
      success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(image, thumb, width, height);
      break;
    case ImageType::Png:
      success = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(image, thumb, width, height);
      break;
    case ImageType::Bmp:
      success = copyFile(image, thumb);
      break;
    case ImageType::None:
      break;
  }
  image.close();
  thumb.close();
  if (!success) {
    LOG_ERR("SIB", "Failed to build thumbnail from %s", imagePath.c_str());
    Storage.remove(outPath.c_str());
  }
  return success;
}

}  // namespace sibling_cover
