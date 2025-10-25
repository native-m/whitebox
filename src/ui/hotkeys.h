#pragma once

#include "core/common.h"

#ifdef WB_PLATFORM_MACOS
#define WB_HKEY_STR_SHIFT "\u21E7"
#else
#define WB_HKEY_MOD_SHIFT "Shift+"
#endif

#ifdef WB_PLATFORM_MACOS
#define WB_HKEY_STR_CTRL "\u2318"
#else
#define WB_HKEY_MOD_CTRL "Ctrl+"
#endif

#ifdef WB_PLATFORM_MACOS
#define WB_HKEY_STR_ALT "\u2325"
#else
#define WB_HKEY_MOD_ALT "Alt+"
#endif

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