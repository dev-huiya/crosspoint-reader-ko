#pragma once

#include <string>

// Saved reading percentage, or -1 when unavailable. Never builds reading caches.
int loadBookProgress(const std::string& path);

// The reader's own percentage when it closes a book; loadBookProgress() then
// returns it for that book without loading the book's metadata again (the
// home screen asks for the book just closed).
void rememberBookProgress(const std::string& path, int percent);
