#include "memory.h"

#if defined(WB_PLATFORM_WINDOWS)
#include <Windows.h>
#endif

#if defined(WB_PLATFORM_LINUX)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace wb {

void* allocate_virtual(size_t size) noexcept {
  void* ptr = nullptr;
#if defined(WB_PLATFORM_WINDOWS)
  ptr = ::VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#elif defined(WB_PLATFORM_LINUX)
  ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
  return ptr;
}

void free_virtual(void* ptr, size_t size) noexcept {
#if defined(WB_PLATFORM_WINDOWS)
  ::VirtualFree(ptr, 0, MEM_RELEASE);
#elif defined(WB_PLATFORM_LINUX)
  ::munmap(ptr, size);
#endif
}

uint32_t get_virtual_page_size() noexcept {
#if defined(WB_PLATFORM_WINDOWS)
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  return system_info.dwPageSize;
#elif defined(WB_PLATFORM_LINUX)
  return sysconf(_SC_PAGE_SIZE);
#else
  return 0;
#endif
}

}  // namespace wb