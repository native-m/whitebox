#pragma once

#include "core/common.h"
#include "engine/audio_io.h"

namespace wb {
struct App {
    bool running = true;
    bool settings_window_shown = false;

    uint32_t audio_io_type = 0;
    uint32_t audio_output_device_idx = 0;
    AudioDeviceSampleRate audio_sample_rate {};
    AudioFormat audio_output_format {};
    AudioFormat audio_input_format {};
    bool audio_exclusive_mode = false;

    virtual ~App();
    void run();
    void shutdown();
    void options_window();
    virtual void init();
    virtual void new_frame() = 0;
};
} // namespace wb