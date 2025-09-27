#include <SDL3/SDL_main.h>

#include "app.h"

#ifdef WB_PLATFORM_WINDOWS
struct MemoryLeakDetection {
  MemoryLeakDetection() {
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
  }

  ~MemoryLeakDetection() {
    _CrtDumpMemoryLeaks();
  }
};

static MemoryLeakDetection g_memleak_detection;
#endif

int main(int argc, char* argv[]) {
  return SDL_EnterAppMainCallbacks(argc, argv, wb::app_init, wb::app_iterate, wb::app_handle_event, wb::app_quit);
}