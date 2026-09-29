#pragma once

#include <cstdint>
#include <string_view>

// Name rules for a book's sibling cover image: /books/foo.jpg is the cover of
// /books/foo.epub. Hardware-free so the host tests can cover them.
namespace sibling_cover {

enum class ImageType : uint8_t { None, Jpeg, Png, Bmp };

constexpr char asciiLower(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

constexpr bool equalsIgnoreAsciiCase(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (asciiLower(a[i]) != asciiLower(b[i])) return false;
  }
  return true;
}

constexpr std::string_view fileNameOf(const std::string_view path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// Name without its last extension; a name without a dot is its own stem.
constexpr std::string_view stemOf(const std::string_view fileName) {
  const size_t dot = fileName.find_last_of('.');
  return dot == std::string_view::npos ? fileName : fileName.substr(0, dot);
}

constexpr ImageType imageTypeOf(const std::string_view fileName) {
  const size_t dot = fileName.find_last_of('.');
  if (dot == std::string_view::npos) return ImageType::None;
  const std::string_view ext = fileName.substr(dot);
  if (equalsIgnoreAsciiCase(ext, ".jpg") || equalsIgnoreAsciiCase(ext, ".jpeg")) return ImageType::Jpeg;
  if (equalsIgnoreAsciiCase(ext, ".png")) return ImageType::Png;
  if (equalsIgnoreAsciiCase(ext, ".bmp")) return ImageType::Bmp;
  return ImageType::None;
}

// True when directory entry `candidate` is a supported image whose name,
// without the extension, equals the book file's (ASCII case ignored, as FAT
// lookups do).
constexpr bool isSiblingCoverName(const std::string_view candidate, const std::string_view bookFileName) {
  const std::string_view bookStem = stemOf(bookFileName);
  return !bookStem.empty() && imageTypeOf(candidate) != ImageType::None &&
         !equalsIgnoreAsciiCase(candidate, bookFileName) && equalsIgnoreAsciiCase(stemOf(candidate), bookStem);
}

}  // namespace sibling_cover
