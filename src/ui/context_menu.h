#pragma once

#include <string>

#include "core/color.h"
#include "core/common.h"

namespace wb {

struct Track;

enum class TrackContextMenuResult {
  None,
  Rename,
  ChangeColor,
  Done,
};

TrackContextMenuResult
track_context_menu(Track* track, int32_t track_id, const std::string* tmp_name, const Color* tmp_color);

void track_input_context_menu(Track* track, uint32_t slot);
void track_plugin_context_menu(Track* track);

}  // namespace wb
