#include "app_sdl2.h"

#ifdef WB_PLATFORM_WINDOWS

struct MemoryLeakDetection {
    MemoryLeakDetection() {
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
    }

    ~MemoryLeakDetection() { _CrtDumpMemoryLeaks(); }
};

MemoryLeakDetection g_memleak_detection;

#endif

int main() {
    wb::AppSDL2 app;
    app.init();
    app.run();
    return 0;
}