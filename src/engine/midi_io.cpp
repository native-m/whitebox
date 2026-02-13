#include "midi_io.h"

#ifdef WB_PLATFORM_LINUX
namespace wb {
MidiIO* create_midi_io_alsa();
}
#endif

namespace wb {

MidiIO* MidiIO::create(AudioIOType type) {
  switch (type) {
    case AudioIOType::NoAudio:
      return nullptr;

#ifdef WB_PLATFORM_WINDOWS

#endif

#ifdef WB_PLATFORM_MACOS

#endif

#ifdef WB_PLATFORM_LINUX
    case AudioIOType::PipeWire:
    case AudioIOType::ALSA:
      return create_midi_io_alsa();
#endif

    default:
      break;
  }

  return nullptr;
}

} // namespace wb