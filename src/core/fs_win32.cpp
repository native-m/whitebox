#include "debug.h"
#include "fs.h"

#ifdef WB_PLATFORM_WINDOWS
#include <Windows.h>

#include "bit_manipulation.h"

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

  HANDLE file = CreateFile(
      path.c_str(), desired_access, FILE_SHARE_READ, nullptr, creation_diposition, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  handle_ = (void*)file;
  open_ = true;

  return true;
}

bool File::seek(int64_t offset, IOSeekMode mode) {
  LARGE_INTEGER ofs{ .QuadPart = offset };
  return SetFilePointerEx((HANDLE)handle_, ofs, nullptr, (DWORD)mode);
}

uint64_t File::position() const {
  LARGE_INTEGER ofs{};
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

Vector<DirectoryEntry> enumerate_directory(const std::filesystem::path& path) {
  if (path.empty())
    return {};

  using PathStringType = std::filesystem::path::string_type;
  std::filesystem::path current_path = to_system_preferred_path(path.native());
  PathStringType path_str = current_path.native();
  path_str.reserve(path_str.size() + 6);
  path_str.append(L"\\*");
  path_str.insert(0, L"\\\\?\\");

  int32_t control_directory_counter = 0;
  Vector<DirectoryEntry> entries;
  WIN32_FIND_DATA ffd;
  HANDLE ffh =
      FindFirstFileEx(path_str.c_str(), FindExInfoStandard, &ffd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);

  do {
    uint32_t type = 0;
    size_t file_size = 0;
    std::wstring_view filename(ffd.cFileName);

    if (control_directory_counter < 2) {
      if (filename == L"." || filename == L"..") {
        control_directory_counter++;
        continue;
      }
    }

    if (has_bit(ffd.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY)) {
      type = DirectoryEntry::Folder;
    } else {
      file_size = (size_t)ffd.nFileSizeLow | ((size_t)ffd.nFileSizeHigh << 32);
      type = DirectoryEntry::File;
    }

    entries.emplace_back(filename, file_size, type);
  } while (FindNextFile(ffh, &ffd));

  FindClose(ffh);

  return entries;
}

}  // namespace wb
#endif