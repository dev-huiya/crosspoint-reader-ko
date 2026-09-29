#include "BookCoverRefresh.h"

#include <Arduino.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>
#include <SiblingCover.h>
#include <Txt.h>
#include <Xtc.h>

namespace {
constexpr const char* CACHE_ROOT = "/.crosspoint";
}  // namespace

bool refreshBookCover(const std::string& bookPath, const int thumbHeight) {
  if (thumbHeight <= 0) return false;
  const unsigned long start = millis();
  bool built = false;
  // Book objects exceed the stack budget; one lives at a time.
  if (FsHelpers::hasEpubExtension(bookPath)) {
    auto epub = makeUniqueNoThrow<Epub>(bookPath, CACHE_ROOT);
    if (!epub) {
      LOG_ERR("COVER", "OOM: Epub");
      return false;
    }
    sibling_cover::forget(epub->getCachePath());
    built = epub->generateThumbBmpFromSource(thumbHeight);
  } else if (FsHelpers::hasXtcExtension(bookPath)) {
    auto xtc = makeUniqueNoThrow<Xtc>(bookPath, CACHE_ROOT);
    if (!xtc) {
      LOG_ERR("COVER", "OOM: Xtc");
      return false;
    }
    sibling_cover::forget(xtc->getCachePath());
    // A sibling image needs no parsed book.
    built = !xtc->siblingCoverImage().empty() && xtc->generateThumbBmp(thumbHeight);
    if (!built) built = xtc->load() && xtc->generateThumbBmp(thumbHeight);
  } else if (FsHelpers::hasTxtExtension(bookPath)) {
    Txt txt(bookPath, CACHE_ROOT);
    sibling_cover::forget(txt.getCachePath());
    built = txt.generateThumbBmp(thumbHeight);
  }
  LOG_DBG("COVER", "Refresh %s (thumb %d px): %s, %lu ms", bookPath.c_str(), thumbHeight, built ? "built" : "no cover",
          millis() - start);
  return built;
}
