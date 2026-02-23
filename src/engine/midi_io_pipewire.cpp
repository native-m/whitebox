#include "midi_io.h"

#ifdef WB_PLATFORM_LINUX

namespace wb {

MidiIO* create_midi_io_pipewire() {
    return new MidiIOPipeWire();
}

} // namespace wb

#endif // WB_PLATFORM_LINUX
