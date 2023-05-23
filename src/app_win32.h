#pragma once
#include "app.h"
#include <Windows.h>

namespace wb
{
    struct AppWin32 : public App
    {
        HWND hwnd;

        virtual ~AppWin32();
        void init(int argc, const char* argv[]) override;
        void shutdown() override;
        void new_frame() override;
    };
}