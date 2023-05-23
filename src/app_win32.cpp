#include "app_win32.h"
#include <combaseapi.h>

namespace wb
{
    AppWin32::~AppWin32()
    {
    }
    
    void AppWin32::init(int argc, const char* argv[])
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }
    
    void AppWin32::shutdown()
    {
    }

    void AppWin32::new_frame()
    {
    }
}