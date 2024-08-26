#include "fs.h"

#ifndef WB_PLATFORM_WINDOWS
namespace wb {
File::File() : handle_(nullptr) {
}

File::~File() {
    close();
}

bool File::open(const std::filesystem::path& path, uint32_t flags) {
    return false;
}

bool File::seek(int64_t offset, uint32_t mode) {
    return false;
}

uint32_t File::read(void* dest, uint32_t size) {
    return 0;
}

uint32_t File::write(const void* src, uint32_t size) {
    return 0;
}

void File::close() {
}
}
#endif