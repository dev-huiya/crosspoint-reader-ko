#pragma once

#include <string>

#include "SiblingCoverName.h"

// A same-name image next to a book file (e.g. /books/foo.jpg for
// /books/foo.epub) takes priority over the book's own cover. Shared by the
// EPUB, XTC and TXT cover and thumbnail paths.
//
// The book's cache directory remembers the last lookup so a home render never
// rescans the book's folder:
//   sibling.src   "v2 <size>\n<path>" of the image the cached cover came from
//   cover.missing the last lookup found no image
// Nothing rescans on its own: Home's "Refresh cache" calls forget() and
// rebuilds. A lookup with `recheck` rescans the folder and, when the result
// differs from the record (image added, removed, renamed or resized),
// deletes the cached cover*.bmp / thumb_*.bmp so they are rebuilt.
namespace sibling_cover {

// One bounded pass over the book's folder; entries are not retained.
std::string findImage(const std::string& bookPath);

// The sibling image to use for the book, or "" to use the book's own cover.
std::string resolve(const std::string& bookPath, const std::string& cacheDir, bool recheck = false);

// Drops the recorded lookup and every cached cover*.bmp / thumb_*.bmp in the
// book's cache directory, so the next build looks for the image again.
void forget(const std::string& cacheDir);

// Full-size cover BMP, with the same crop / threshold options as the embedded path.
bool writeCoverBmp(const std::string& imagePath, const std::string& outPath, bool cropped, bool originalThresholds);

// 1-bit thumbnail for the home screen covering height * 0.6 x height, as the
// embedded covers' thumbnails do (JPEG/PNG converters; BMP downscaled here).
bool writeThumbBmp(const std::string& imagePath, const std::string& outPath, int height);

}  // namespace sibling_cover
