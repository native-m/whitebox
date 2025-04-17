#pragma once
#include <filesystem>
#include "midi.h"

namespace wb {

bool load_notes_from_file(MidiData& result, const std::filesystem::path& path);
double get_midi_file_content_length(const std::filesystem::path& path);

}  // namespace wb