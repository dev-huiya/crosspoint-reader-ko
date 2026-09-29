#pragma once

#include <string>

// Rebuilds a book's home thumbnail at thumbHeight: the same-name image beside
// the book first (lib/SiblingCover), then the book's own cover (EPUB, XTC).
// The recorded image lookup and every cached cover*.bmp / thumb_*.bmp of the
// book are dropped first, so the sleep screen's cover is rebuilt from the same
// source the next time it is needed. Reading progress and section caches are
// untouched. Returns false when the book has no usable cover.
bool refreshBookCover(const std::string& bookPath, int thumbHeight);
