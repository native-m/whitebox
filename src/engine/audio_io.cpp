#include "audio_io.h"

#include "core/debug.h"

namespace wb {

#ifdef WB_PLATFORM_WINDOWS
extern AudioIO2* create_audio_io_wasapi(); // See audio_io_wasapi.cpp
#endif
#ifdef WB_PLATFORM_MACOS
extern AudioIO2* create_audio_io_coreaudio(); // See audio_io_coreaudio.cpp
#endif
#ifdef WB_PLATFORM_LINUX
extern AudioIO2* create_audio_io_pipewire();
#endif

AudioIO2* AudioIO2::create(AudioIOType type) {
  switch (type) {
    case AudioIOType::NoAudio: break;
      #ifdef WB_PLATFORM_WINDOWS
    case AudioIOType::WASAPI: return create_audio_io_wasapi();
      #endif
      #ifdef WB_PLATFORM_MACOS
    case AudioIOType::CoreAudio: return create_audio_io_coreaudio();
      #endif
#ifdef WB_PLATFORM_LINUX
    case AudioIOType::PipeWire:
      return create_audio_io_pipewire();
#endif


    default: assert(false && "Unknown Audio IO"); break;
  }
  return nullptr;
}

AudioIOType AudioIO2::get_platform_recommended_audio_io_type() {
#if defined(WB_PLATFORM_WINDOWS)
  return AudioIOType::WASAPI;
#elif defined(WB_PLATFORM_LINUX)
  return AudioIOType::PipeWire;
#elif defined(WB_PLATFORM_MACOS)
  return AudioIOType::CoreAudio;
#else
  return AudioIOType::NoAudio;
#endif
}

}  // namespace wb