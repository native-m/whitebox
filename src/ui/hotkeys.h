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

  CommonDelete,
  CommonDuplicate,
  CommonMute,

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