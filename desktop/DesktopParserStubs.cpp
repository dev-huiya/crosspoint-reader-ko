#include <Epub.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <Epub/hyphenation/Hyphenator.h>

// Image decoding is not yet wired into the desktop reader. Text and layout use
// the real firmware parser, page objects, font metrics, and renderer.
ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : imagePath(imagePath), srcPath(srcPath), width(width), height(height) {}
bool ImageBlock::imageExists() const { return false; }
bool ImageBlock::hasValidCache() const { return false; }
bool ImageBlock::needsDecode() const { return false; }
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const {}
void ImageBlock::render(GfxRenderer&, int, int) {}
bool ImageBlock::serialize(HalFile&) { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(HalFile&) { return {}; }

bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageToFramebufferDecoder::validateAndStoreDimensions(int64_t, int64_t, ImageDimensions&, const char*) {
  return false;
}

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string&, bool) { return {}; }
bool Epub::readItemContentsToStream(const std::string&, Print&, size_t, bool) const { return false; }
