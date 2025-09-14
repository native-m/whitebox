#include "fs.h"

#ifndef WB_PLATFORM_WINDOWS
#include <cstdio>

#include "bit_manipulation.h"

namespace wb {

File::File() : handle_(nullptr) {
}

File::~File() {
  if (handle_ == nullptr) {
    close();
  }
}

bool File::open(const std::filesystem::path& path, uint32_t flags) {
  std::string str = path.string();
  const char* mode = nullptr;

  if (flags == IOOpenMode::Read) {
    mode = "rb";
  } else if (flags == IOOpenMode::Write) {
    mode = "ab";
  } else if (contain_bit(flags, IOOpenMode::Write, IOOpenMode::Truncate)) {
    mode = "wb";
  } else if (contain_bit(flags, IOOpenMode::Read, IOOpenMode::Write)) {
    mode = "r+b";
  }

  if (mode == nullptr) {
    return false;
  }

  FILE* file = std::fopen(path.c_str(), mode);
  if (!file)
    return false;

  handle_ = file;
  return true;
}

bool File::seek(int64_t offset, IOSeekMode mode) {
  return std::fseek((FILE*)handle_, (long)offset, (int)mode) == 0;
}

uint64_t File::position() const {
  long pos = std::ftell((FILE*)handle_);
  if (pos < 0)
    return 0;
  return (uint64_t)pos;
}

uint32_t File::read(void* dest, size_t size) {
  uint32_t count = (uint32_t)std::fread(dest, size, 1, (FILE*)handle_);
  return count * size;
}

uint32_t File::write(const void* src, size_t size) {
  uint32_t count = (uint32_t)std::fwrite(src, size, 1, (FILE*)handle_);
  return count * size;
}

void File::close() {
  handle_ = nullptr;
}
}  // namespace wb
#endif