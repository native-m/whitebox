#pragma once

#include "core/common.h"
#include "core/color.h"
#include <string>

namespace wb {
struct Track;
bool track_context_menu(Track* track, int track_id, const std::string* tmp_name, const Color* tmp_color);
void track_input_context_menu(Track* track, uint32_t slot);
void track_plugin_context_menu(Track* track);
} // namespace wb
