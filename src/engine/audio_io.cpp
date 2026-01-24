#include "audio_io.h"

#include "core/debug.h"

namespace wb {

extern AudioIO2* create_audio_io_wasapi(); // See audio_io_wasapi.cpp
extern AudioIO2* create_audio_io_coreaudio(); // See audio_io_coreaudio.cpp

AudioIO2* AudioIO2::create(AudioIOType type) {
  switch (type) {
    case AudioIOType::NoAudio: break;
    case AudioIOType::WASAPI: return create_audio_io_wasapi();
    case AudioIOType::CoreAudio: return create_audio_io_coreaudio();
    default: assert(false && "Unknown Audio IO"); break;
  }
  return nullptr;
}

AudioIOType AudioIO2::get_platform_recommended_audio_io_type() {
#if defined(WB_PLATFORM_WINDOWS)
  return AudioIOType::WASAPI;
#elif defined(WB_PLATFORM_LINUX)
  return AudioIOType::PulseAudio;
#elif defined(WB_PLATFORM_MACOS)
  return AudioIOType::CoreAudio;
#else
  return AudioIOType::NoAudio;
#endif
}

}  // namespace wb