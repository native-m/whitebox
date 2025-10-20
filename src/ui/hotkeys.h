#pragma once

#include "core/common.h"

namespace wb {

enum class Hotkey : uint16_t {
  Play,
  Undo,
  Redo,

  New,
  Open,
  Save,
  SaveAs,

  Delete,
  SelectAll,
  Duplicate,
  Mute,
  Unmute,

  PianoRollSelectTool,
  PianoRollDrawTool,
  PianoRollMarkerTool,
  PianoRollPaintTool,
  PianoRollSliceTool,

  Count,
};

void hkey_process();
bool hkey_pressed(Hotkey hkey);

}  // namespace wb