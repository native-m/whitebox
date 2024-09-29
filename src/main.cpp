#include "app.h"

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
    wb::app_init();
    wb::app_run_loop();
    wb::app_shutdown();
    return 0;
}