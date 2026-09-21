#pragma once
#include <cstdint>
#include <fstream>
#include <string>

class HalFile {
 public:
  std::fstream file;
  int read(uint8_t* data, size_t length) {
    file.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(length));
    return static_cast<int>(file.gcount());
  }
  size_t write(const uint8_t* data, size_t length) {
    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
    return file ? length : 0;
  }
};

class HalStorage {
 public:
  bool openFileForRead(const char*, const std::string& path, HalFile& out) {
    out.file.open(path, std::ios::binary | std::ios::in);
    return out.file.is_open();
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& out) {
    out.file.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
    return out.file.is_open();
  }
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
};
#define Storage HalStorage::getInstance()
