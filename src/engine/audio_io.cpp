#include "audio_io.h"
#include "core/debug.h"

namespace wb {
AudioIO* g_audio_io;

extern AudioIO* create_audio_io_wasapi();
extern AudioIO* create_audio_io_pulseaudio();
extern AudioIO* create_audio_io_pulseaudio2();
extern AudioIO* create_audio_io_asio();

void init_audio_io(AudioIOType type) {
    Log::info("Initializing audio I/O...");
    switch (type) {
        case AudioIOType::WASAPI:
            g_audio_io = create_audio_io_wasapi();
            break;
        case AudioIOType::PulseAudio:
            g_audio_io = create_audio_io_pulseaudio2();
            break;
        default:
            assert(false && "Unimplemented Audio IO");
    }
}

void shutdown_audio_io() {
    if (!g_audio_io)
        return;
    g_audio_io->close_device();
    delete g_audio_io;
    g_audio_io = nullptr;
}

} // namespace wb