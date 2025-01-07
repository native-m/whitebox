#include "fs.h"
#include "debug.h"

#ifdef WB_PLATFORM_WINDOWS
#include "bit_manipulation.h"
#include <Windows.h>

namespace wb {

File::File() : handle_(INVALID_HANDLE_VALUE) {
}

File::~File() {
    close();
}

bool File::open(const std::filesystem::path& path, uint32_t flags) {
    DWORD desired_access = 0;
    DWORD creation_diposition = 0;

    if (has_bit(flags, IOOpenMode::Read)) {
        desired_access |= GENERIC_READ;
        creation_diposition = OPEN_EXISTING;
    }

    if (has_bit(flags, IOOpenMode::Write)) {
        desired_access |= GENERIC_WRITE;
        creation_diposition = OPEN_ALWAYS;
    }

    if (has_bit(flags, IOOpenMode::Truncate)) {
        creation_diposition = CREATE_ALWAYS;
    }

    HANDLE file = CreateFile(path.c_str(), desired_access, FILE_SHARE_READ, nullptr,
                             creation_diposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    handle_ = (void*)file;
    open_ = true;

    return true;
}

bool File::seek(int64_t offset, IOSeekMode mode) {
    LARGE_INTEGER ofs {.QuadPart = offset};
    return SetFilePointerEx((HANDLE)handle_, ofs, nullptr, (DWORD)mode);
}

uint64_t File::position() const {
    LARGE_INTEGER ofs {};
    LARGE_INTEGER output;
    if (!SetFilePointerEx((HANDLE)handle_, ofs, &output, FILE_CURRENT)) {
        return 0;
    }
    return output.QuadPart;
}

uint32_t File::read(void* dest, size_t size) {
    DWORD num_read;
    ReadFile((HANDLE)handle_, dest, (DWORD)size, &num_read, nullptr);
    return num_read;
}

uint32_t File::write(const void* src, size_t size) {
    DWORD num_written;
    WriteFile((HANDLE)handle_, src, (DWORD)size, &num_written, nullptr);
    return num_written;
}

void File::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

} // namespace wb
#endif