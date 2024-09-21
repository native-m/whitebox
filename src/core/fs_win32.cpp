#include "fs.h"

#ifdef WB_PLATFORM_WINDOWS
#include "bit_manipulation.h"
#include <Windows.h>
#include <winternl.h>

namespace wb {

File::File() : handle_(INVALID_HANDLE_VALUE) {
}

File::~File() {
    close();
}

bool File::open(const std::filesystem::path& path, uint32_t flags) {
    DWORD desired_access = 0;
    DWORD creation_diposition = 0;

    if (has_bit(flags, File::Read)) {
        desired_access |= GENERIC_READ;
        creation_diposition = OPEN_EXISTING;
    }

    if (has_bit(flags, File::Write)) {
        desired_access |= GENERIC_WRITE;
        creation_diposition = OPEN_ALWAYS;
    }

    if (has_bit(flags, File::Truncate)) {
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

bool File::seek(int64_t offset, uint32_t mode) {
    LARGE_INTEGER ofs {.QuadPart = offset};
    return SetFilePointerEx((HANDLE)handle_, ofs, nullptr, mode);
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