#include "audio_io.h"

#ifdef __linux__

namespace wb {
AudioIO* create_audio_io_pulseaudio() {
    return nullptr;
}
} // namespace wb

#else

namespace wb {
AudioIO* create_audio_io_pulseaudio() {
    return nullptr;
}
} // namespace wb

#endif