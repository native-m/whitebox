#pragma once

namespace wb {

enum class Hotkey : uint16_t {
  Play,
  Undo,
  Redo,

  New,
  Open,
  Save,
  SaveAs,

  SelectAll,
  Delete,
  Duplicate,
  Mute,

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