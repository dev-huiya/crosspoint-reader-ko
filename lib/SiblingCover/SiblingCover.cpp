#include "SiblingCover.h"

#include "SiblingCoverMatch.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace sibling_cover {
namespace {

constexpr const char* MARKER_NAME = "/sibling.src";
constexpr const char* MISSING_NAME = "/cover.missing";
// Marker format version. A marker of another version counts as no lookup, so
// the next recheck rebuilds the covers; v1 thumbnails of BMP images were full
// size copies.
constexpr const char* MARKER_VERSION = "v2 ";
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
  const size_t versionLength = strlen(MARKER_VERSION);
  if (strncmp(buffer.get(), MARKER_VERSION, versionLength) != 0) return record;
  char* newline = nullptr;
  const unsigned long size = strtoul(buffer.get() + versionLength, &newline, 10);
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
  char header[24];
  const int length = snprintf(header, sizeof(header), "%s%u\n", MARKER_VERSION, static_cast<unsigned>(size));
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

void writeLE16(HalFile& out, const uint16_t value) {
  const uint8_t bytes[] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  out.write(bytes, sizeof(bytes));
}

void writeLE32(HalFile& out, const uint32_t value) {
  const uint8_t bytes[] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                           static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  out.write(bytes, sizeof(bytes));
}

// 1-bit BMP header, palette 0 = black, 1 = white (as the JPEG/PNG thumbnails).
void writeBmp1BitHeader(HalFile& out, const int width, const int height, const bool topDown) {
  const uint32_t rowBytes = static_cast<uint32_t>(width + 31) / 32 * 4;
  const uint32_t imageSize = rowBytes * static_cast<uint32_t>(height);
  out.write("BM", 2);
  writeLE32(out, 62 + imageSize);
  writeLE32(out, 0);
  writeLE32(out, 62);
  writeLE32(out, 40);
  writeLE32(out, static_cast<uint32_t>(width));
  writeLE32(out, static_cast<uint32_t>(topDown ? -height : height));
  writeLE16(out, 1);
  writeLE16(out, 1);
  writeLE32(out, 0);
  writeLE32(out, imageSize);
  writeLE32(out, 2835);
  writeLE32(out, 2835);
  writeLE32(out, 2);
  writeLE32(out, 2);
  const uint8_t palette[] = {0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0};
  out.write(palette, sizeof(palette));
}

// Downscales a BMP to a 1-bit thumbnail that covers width x height, like the
// JPEG/PNG thumbnails (the draw clips the overflow). Each output pixel
// averages its source box, then a 4x4 ordered dither sets it. Rows are
// written in the source's order, so a bottom-up source stays bottom-up.
bool scaleBmpThumb(HalFile& image, HalFile& thumb, const int width, const int height) {
  Bitmap bitmap(image);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  const int srcW = bitmap.getWidth();
  const int srcH = bitmap.getHeight();
  if (srcW <= 0 || srcH <= 0 || width <= 0 || height <= 0) return false;
  const float scale = std::max(static_cast<float>(width) / srcW, static_cast<float>(height) / srcH);
  // Already small enough: the image itself is the thumbnail.
  if (scale >= 1.0f) {
    image.seek(0);
    return copyFile(image, thumb);
  }
  const int outW = std::max(1, static_cast<int>(srcW * scale));
  const int outH = std::max(1, static_cast<int>(srcH * scale));
  const int outRowBytes = (outW + 31) / 32 * 4;
  auto srcRow = makeUniqueNoThrow<uint8_t[]>(bitmap.getRowBytes());
  auto grayRow = makeUniqueNoThrow<uint8_t[]>((srcW + 3) / 4);
  auto outRow = makeUniqueNoThrow<uint8_t[]>(outRowBytes);
  auto sums = makeUniqueNoThrow<uint32_t[]>(outW);
  auto counts = makeUniqueNoThrow<uint16_t[]>(outW);
  if (!srcRow || !grayRow || !outRow || !sums || !counts) {
    LOG_ERR("SIB", "OOM: BMP thumbnail (%dx%d)", srcW, outW);
    return false;
  }
  static constexpr uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const auto clearBox = [&] {
    std::fill(sums.get(), sums.get() + outW, 0u);
    std::fill(counts.get(), counts.get() + outW, static_cast<uint16_t>(0));
  };
  const auto flush = [&](const int outY) {
    std::fill(outRow.get(), outRow.get() + outRowBytes, static_cast<uint8_t>(0));
    for (int x = 0; x < outW; ++x) {
      // Source levels are 0 (black) .. 3 (white).
      const uint32_t lum = counts[x] ? sums[x] * 255 / (3u * counts[x]) : 255;
      if (lum >= BAYER[outY & 3][x & 3] * 16u + 8u) outRow[x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
    }
    return thumb.write(outRow.get(), outRowBytes) == static_cast<size_t>(outRowBytes);
  };
  writeBmp1BitHeader(thumb, outW, outH, bitmap.isTopDown());
  clearBox();
  int currentY = 0;
  for (int y = 0; y < srcH; ++y) {
    if (bitmap.readNextRow(grayRow.get(), srcRow.get()) != BmpReaderError::Ok) return false;
    const int outY = static_cast<int>(static_cast<int64_t>(y) * outH / srcH);
    if (outY != currentY) {
      if (!flush(currentY)) return false;
      clearBox();
      currentY = outY;
    }
    for (int x = 0; x < srcW; ++x) {
      const int outX = static_cast<int>(static_cast<int64_t>(x) * outW / srcW);
      sums[outX] += (grayRow[x / 4] >> (6 - (x % 4) * 2)) & 0x3;
      ++counts[outX];
    }
  }
  return flush(currentY);
}

}  // namespace

std::string findImage(const std::string& bookPath) {
  const size_t slash = bookPath.find_last_of('/');
  const std::string folder = slash == std::string::npos || slash == 0 ? "/" : bookPath.substr(0, slash);
  const std::string bookFileName{fileNameOf(bookPath)};
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
    if (isSiblingCoverNameNfc(name.get(), bookFileName)) {
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

void forget(const std::string& cacheDir) {
  Storage.remove((cacheDir + MARKER_NAME).c_str());
  Storage.remove((cacheDir + MISSING_NAME).c_str());
  clearCachedCovers(cacheDir);
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
      success = scaleBmpThumb(image, thumb, width, height);
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
