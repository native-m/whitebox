#include "audio_io.h"

#include "core/debug.h"

namespace wb {

AudioIO* g_audio_io;

extern AudioIO* create_audio_io_wasapi();
extern AudioIO* create_audio_io_pulseaudio();
extern AudioIO* create_audio_io_asio();
extern AudioIO2* create_audio_io_wasapi2();

AudioIO* AudioIO::create(AudioIOType type) {
  switch (type) {
    case AudioIOType::WASAPI: return create_audio_io_wasapi();
    case AudioIOType::PulseAudio: return create_audio_io_pulseaudio();
    default: assert(false && "Unknown Audio IO"); break;
  }
  return nullptr;
}

AudioIO2* AudioIO2::create(AudioIOType type) {
  switch (type) {
    case AudioIOType::NoAudio: break;
    case AudioIOType::WASAPI: return create_audio_io_wasapi2();
    default: assert(false && "Unknown Audio IO"); break;
  }
  return nullptr;
}

AudioIOType AudioIO2::get_platform_recommended_audio_io_type() {
#if defined(WB_PLATFORM_WINDOWS)
  return AudioIOType::WASAPI;
#elif defined(WB_PLATFORM_LINUX)
  return AudioIOType::PulseAudio;
#else
  return AudioIOType::NoAudio;
#endif
}

void init_audio_io(AudioIOType type) {
  Log::info("Initializing audio I/O...");
  switch (type) {
    case AudioIOType::WASAPI: g_audio_io = create_audio_io_wasapi(); break;
    case AudioIOType::PulseAudio: g_audio_io = create_audio_io_pulseaudio(); break;
    default: assert(false && "Unknown Audio IO");
  }
}

void shutdown_audio_io() {
  if (!g_audio_io)
    return;
  g_audio_io->close_device();
  delete g_audio_io;
  g_audio_io = nullptr;
}

}  // namespace wb