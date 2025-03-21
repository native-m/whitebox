#include "fs.h"

#ifndef WB_PLATFORM_WINDOWS
namespace wb {
File::File() : handle_(nullptr) {
}

File::~File() {
  if (handle_ == nullptr) {
    close();
  }
}

bool File::open(const std::filesystem::path& path, uint32_t flags) {
  return false;
}

bool File::seek(int64_t offset, IOSeekMode mode) {
  return false;
}

uint64_t File::position() const {
  return 0;
}

uint32_t File::read(void* dest, size_t size) {
  return 0;
}

uint32_t File::write(const void* src, size_t size) {
  return 0;
}

void File::close() {
  handle_ = nullptr;
}
}  // namespace wb
#endif