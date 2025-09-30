#include <CoreFoundation/CFURL.h>
#include "fs.h"

#ifdef WB_PLATFORM_MACOS

#include <CoreFoundation/CoreFoundation.h>

#include "defer.h"

namespace wb {

Vector<DirectoryEntry> enumerate_directory(const std::filesystem::path& path) {
  CFStringRef path_str = CFStringCreateWithCString(kCFAllocatorDefault, path.c_str(), kCFStringEncodingUTF8);
  defer(CFRelease(path_str));

  CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path_str, kCFURLPOSIXPathStyle, true);
  defer(CFRelease(url));
  if (url == nullptr) {
    return {};
  }

  CFURLEnumeratorRef enumerator =
      CFURLEnumeratorCreateForDirectoryURL(kCFAllocatorDefault, url, kCFURLEnumeratorDefaultBehavior, nullptr);
  defer(CFRelease(enumerator));
  if (enumerator == nullptr) {
    return {};
  }

  Vector<DirectoryEntry> entries;
  CFURLRef url_item{};
  while (CFURLEnumeratorGetNextURL(enumerator, &url_item, NULL) == kCFURLEnumeratorSuccess) {
    CFStringRef file_type{};
    CFURLCopyResourcePropertyForKey(url_item, kCFURLFileResourceTypeKey, &file_type, nullptr);
    defer(CFRelease(file_type));

    uint32_t type;
    int64_t file_size_value = 0;
    if (file_type == kCFURLFileResourceTypeDirectory) {
      type = DirectoryEntry::Folder;
    } else if (file_type == kCFURLFileResourceTypeRegular) {
      CFNumberRef file_size{};
      CFURLCopyResourcePropertyForKey(url_item, kCFURLFileSizeKey, &file_size, nullptr);
      CFNumberGetValue(file_size, kCFNumberSInt64Type, &file_size_value);
      type = DirectoryEntry::File;
      CFRelease(file_size);
    } else if (file_type == kCFURLFileResourceTypeSymbolicLink) {
      type = DirectoryEntry::Symlink;
    } else {
      continue;
    }

    CFStringRef filename = nullptr;
    CFURLCopyResourcePropertyForKey(url_item, kCFURLNameKey, &filename, nullptr);

    CFIndex len = CFStringGetLength(filename) + 1;
    std::filesystem::path::string_type filename_str(len - 1, '\x00');
    CFStringGetCString(filename, filename_str.data(), len, kCFStringEncodingUTF8);
    entries.emplace_back(std::move(filename_str), (size_t)file_size_value, type);

    CFRelease(filename);
  }

  std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
    auto ch_pred = [](char32_t a, char32_t b) {
      a = std::tolower(a);
      b = std::tolower(b);
      return a < b;
    };
    const auto& path_a = a.name.native();
    const auto& path_b = b.name.native();
    return std::lexicographical_compare(path_a.begin(), path_a.end(), path_b.begin(), path_b.end(), ch_pred);
  });

  return entries;
}

}  // namespace wb

#endif
