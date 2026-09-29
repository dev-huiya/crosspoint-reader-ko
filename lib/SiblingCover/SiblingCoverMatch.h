#pragma once

#include <Utf8.h>

#include <string>

#include "SiblingCoverName.h"

namespace sibling_cover {

// isSiblingCoverName() on NFC-composed names, so an image copied from macOS
// (decomposed Hangul, NFD) matches a book named in composed form and vice versa.
inline bool isSiblingCoverNameNfc(const std::string& candidate, const std::string& bookFileName) {
  return isSiblingCoverName(utf8ComposeNfc(candidate), utf8ComposeNfc(bookFileName));
}

}  // namespace sibling_cover
