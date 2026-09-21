#pragma once
#include <Print.h>

#include <cstdint>
#include <cstdio>
#include <string>

class HalFile : public Print {
 public:
  ~HalFile() { close(); }
  bool open(const char* path, const char* mode) {
    close();
    file_ = std::fopen(path, mode);
    return file_ != nullptr;
  }
  int read() { return file_ ? std::fgetc(file_) : -1; }
  int available() { return file_ ? static_cast<int>(size() - position()) : 0; }
  size_t read(void* bytes, size_t length) { return file_ ? std::fread(bytes, 1, length, file_) : 0; }
  size_t write(const void* bytes, size_t length) { return file_ ? std::fwrite(bytes, 1, length, file_) : 0; }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  bool flush() { return file_ && std::fflush(file_) == 0; }
  bool seekSet(size_t offset) { return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_SET) == 0; }
  bool seek(size_t offset) { return seekSet(offset); }
  bool seekCur(size_t offset) { return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_CUR) == 0; }
  size_t position() const { return file_ ? static_cast<size_t>(std::ftell(file_)) : 0; }
  size_t size() {
    if (!file_) return 0;
    const long pos = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const long end = std::ftell(file_);
    std::fseek(file_, pos, SEEK_SET);
    return end > 0 ? static_cast<size_t>(end) : 0;
  }
  bool isOpen() const { return file_ != nullptr; }
  explicit operator bool() const { return isOpen(); }
  bool close() {
    if (!file_) return false;
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    return ok;
  }

 private:
  std::FILE* file_ = nullptr;
};
class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path, "rb"); }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "rb"); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "wb"); }
  bool exists(const char* path) const {
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
  }
  bool remove(const std::string& path) { return std::remove(path.c_str()) == 0; }
  bool rename(const char* from, const char* to) { return std::rename(from, to) == 0; }
};
#define Storage HalStorage::getInstance()
