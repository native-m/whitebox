#include "hotkeys.h"

#include <fmt/ranges.h>
#include <imgui.h>

#include "core/debug.h"

namespace wb {

struct alignas(8) HotkeyItem {
  Hotkey id;
  uint16_t mod;
  uint16_t key;
};

static HotkeyItem hotkey_table[(uint32_t)Hotkey::Count] = {
  { Hotkey::Play, ImGuiMod_None, ImGuiKey_Space },
  { Hotkey::Undo, ImGuiMod_Ctrl, ImGuiKey_Z },
  { Hotkey::Redo, ImGuiMod_Ctrl, ImGuiKey_Y },

  { Hotkey::New, ImGuiMod_Ctrl, ImGuiKey_N },
  { Hotkey::Open, ImGuiMod_Ctrl, ImGuiKey_O },
  { Hotkey::Save, ImGuiMod_Ctrl, ImGuiKey_S },
  { Hotkey::SaveAs, ImGuiMod_Ctrl | ImGuiMod_Shift, ImGuiKey_S },

  { Hotkey::CommonDelete, ImGuiMod_None, ImGuiKey_Delete },
  { Hotkey::CommonDuplicate, ImGuiMod_Ctrl, ImGuiKey_D },
  { Hotkey::CommonMute, ImGuiMod_Ctrl, ImGuiKey_M },

  { Hotkey::PianoRollSelectTool, ImGuiMod_None, ImGuiKey_Z },
  { Hotkey::PianoRollDrawTool, ImGuiMod_None, ImGuiKey_X },
  { Hotkey::PianoRollMarkerTool, ImGuiMod_None, ImGuiKey_C },
  { Hotkey::PianoRollPaintTool, ImGuiMod_None, ImGuiKey_V },
  { Hotkey::PianoRollSliceTool, ImGuiMod_None, ImGuiKey_B },
};

static bool hkey_map[(uint32_t)Hotkey::Count];

void hkey_process() {
  uint16_t mod_mask = GImGui->IO.KeyMods;
  std::memset(hkey_map, 0, sizeof(hkey_map));  // Clear
  if (!GImGui->IO.WantTextInput) {
    for (uint32_t i = 0; auto [_, mod, key] : hotkey_table) {
      bool mod_down = mod == mod_mask;
      bool key_pressed = ImGui::IsKeyPressed((ImGuiKey)key, 0);
      hkey_map[i] = mod_down && key_pressed;
      i++;
    }
  }
}

bool hkey_pressed(Hotkey hkey) {
  assert(hkey < Hotkey::Count);
  return hkey_map[(uint32_t)hkey];
}

}  // namespace wb