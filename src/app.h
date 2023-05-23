#pragma once

#include "engine/audio_stream.h"
#include "engine/engine.h"

namespace wb
{
    struct App
    {
        bool running = true;

        virtual ~App() { }
        virtual void init(int argc, const char* argv[]);
        virtual void shutdown();
        virtual void new_frame() = 0;
        void run();

        static int run(int argc, const char* argv[]);
    };
}