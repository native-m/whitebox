#ifdef __linux__

#include "audio_io.h"

namespace wb {
AudioIO* create_audio_io_pulseaudio() {
    return nullptr;
}
} // namespace wb

#endif