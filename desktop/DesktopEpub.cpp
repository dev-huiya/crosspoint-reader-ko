#include "DesktopEpub.h"

#include <expat.h>

#include <cstring>
#include <filesystem>
#include <string_view>
#include <unordered_map>

namespace {
const char* localName(const char* name) {
  const char* colon = std::strchr(name, ':');
  return colon ? colon + 1 : name;
}

const char* attribute(const char** attributes, const char* key) {
  for (size_t i = 0; attributes[i]; i += 2) {
    if (std::strcmp(localName(attributes[i]), key) == 0) return attributes[i + 1];
  }
  return nullptr;
}

bool parseXml(const std::string& text, XML_StartElementHandler start, void* context) {
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) return false;
  XML_SetUserData(parser, context);
  XML_SetStartElementHandler(parser, start);
  const bool ok = XML_Parse(parser, text.data(), static_cast<int>(text.size()), XML_TRUE) == XML_STATUS_OK;
  XML_ParserFree(parser);
  return ok;
}

std::string resolveZipPath(const std::string& base, const std::string& href) {
  const auto path = (std::filesystem::path(base).parent_path() / std::filesystem::path(href)).lexically_normal();
  if (path.is_absolute()) return {};
  for (const auto& part : path) {
    if (part == "..") return {};
  }
  return path.generic_string();
}

struct PackageIndex {
  std::unordered_map<std::string, std::string> manifest;
  std::vector<std::string> spine;
};
}  // namespace

DesktopEpub::~DesktopEpub() {
  if (opened_) mz_zip_reader_end(&archive_);
}

bool DesktopEpub::readText(const std::string& entry, std::string& output) {
  const int index = mz_zip_reader_locate_file(&archive_, entry.c_str(), nullptr, 0);
  if (index < 0) return false;
  mz_zip_archive_file_stat info{};
  if (!mz_zip_reader_file_stat(&archive_, static_cast<mz_uint>(index), &info) || info.m_uncomp_size > 4 * 1024 * 1024)
    return false;
  output.resize(static_cast<size_t>(info.m_uncomp_size));
  return mz_zip_reader_extract_to_mem(&archive_, static_cast<mz_uint>(index), output.data(), output.size(), 0) != 0;
}

bool DesktopEpub::open(const char* archivePath) {
  if (opened_) mz_zip_reader_end(&archive_);
  archive_ = {};
  opened_ = mz_zip_reader_init_file(&archive_, archivePath, 0) != 0;
  chapters_.clear();
  if (!opened_) return false;

  std::string container;
  if (!readText("META-INF/container.xml", container)) return false;
  std::string opfPath;
  const auto rootfile = [](void* context, const XML_Char* name, const XML_Char** attributes) {
    if (std::strcmp(localName(name), "rootfile") != 0) return;
    const char* path = attribute(attributes, "full-path");
    if (path) *static_cast<std::string*>(context) = path;
  };
  if (!parseXml(container, rootfile, &opfPath) || opfPath.empty()) return false;

  std::string package;
  if (!readText(opfPath, package)) return false;
  PackageIndex index;
  index.manifest.reserve(64);
  index.spine.reserve(32);
  const auto packageElement = [](void* context, const XML_Char* name, const XML_Char** attributes) {
    auto& index = *static_cast<PackageIndex*>(context);
    if (std::strcmp(localName(name), "item") == 0) {
      const char* id = attribute(attributes, "id");
      const char* href = attribute(attributes, "href");
      const char* mediaType = attribute(attributes, "media-type");
      if (id && href && mediaType && (std::strcmp(mediaType, "application/xhtml+xml") == 0 ||
                                    std::strcmp(mediaType, "text/html") == 0))
        index.manifest.emplace(id, href);
    } else if (std::strcmp(localName(name), "itemref") == 0) {
      const char* id = attribute(attributes, "idref");
      if (id) index.spine.emplace_back(id);
    }
  };
  if (!parseXml(package, packageElement, &index)) return false;
  chapters_.reserve(index.spine.size());
  for (const auto& id : index.spine) {
    const auto item = index.manifest.find(id);
    if (item == index.manifest.end()) continue;
    const std::string path = resolveZipPath(opfPath, item->second);
    if (!path.empty() && mz_zip_reader_locate_file(&archive_, path.c_str(), nullptr, 0) >= 0)
      chapters_.push_back(path);
  }
  return !chapters_.empty();
}

bool DesktopEpub::extractChapter(size_t index, const char* outputPath) {
  if (index >= chapters_.size()) return false;
  const int file = mz_zip_reader_locate_file(&archive_, chapters_[index].c_str(), nullptr, 0);
  return file >= 0 && mz_zip_reader_extract_to_file(&archive_, static_cast<mz_uint>(file), outputPath, 0) != 0;
}
