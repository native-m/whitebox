#include "controls.h"

#include <fmt/format.h>

#include "core/bit_manipulation.h"
#include "core/color.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "core/queue.h"
#include "engine/engine.h"
#include "font.h"
#include "gfx/draw.h"

namespace wb::controls {

void push_style_compact() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, (float)(int)(style.FramePadding.y * 0.60f));
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, (float)(int)(style.ItemSpacing.y * 0.60f));
}

void pop_style_compact() {
  ImGui::PopStyleVar(2);
}

bool begin_window(const char* title, bool* p_open, ImGuiWindowFlags flags) {
  ImGui::PushID(title);
  auto state_storage = ImGui::GetStateStorage();
  bool* hide_background = state_storage->GetBoolRef(ImGui::GetID("no_bg"));
  bool* external_viewport = state_storage->GetBoolRef(ImGui::GetID("ext_vp"));
  float border_size = GImGui->Style.WindowBorderSize;

  if (*hide_background) {
    flags |= ImGuiWindowFlags_NoBackground;
    border_size = 0.0f;
  }

  // ImGuiWindowClass window_class {};
  // window_class.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
  // ImGui::SetNextWindowClass(&window_class);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, border_size);

  bool ret = ImGui::Begin(title, p_open, flags);
  ImGuiViewport* viewport = ImGui::GetWindowViewport();
  ImGuiDockNode* node = ImGui::GetWindowDockNode();
  if (viewport) {
    *external_viewport = viewport->ParentViewportId != 0;
  }

  if (ret && node) {
    if (node->HostWindow) {
      // Don't draw background when the background is already drawn by the host window
      *hide_background = (node->HostWindow->Flags & ImGuiWindowFlags_NoBackground) == ImGuiWindowFlags_NoBackground;
    } else {
      *hide_background = false;
    }
  } else {
    *hide_background = false;
  }

  ImGui::PopStyleVar();
  return ret;
}

void end_window() {
  ImGui::End();
  ImGui::PopID();
}

bool begin_floating_window(const char* str_id, const ImVec2& pos) {
  static constexpr uint32_t window_flags = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
  ImGui::SetNextWindowPos(pos, ImGuiCond_None);
  return ImGui::Begin(str_id, nullptr, window_flags);
}

void end_floating_window() {
  ImGui::End();
}

void song_position() {
  // Playhead
  double playhead = g_engine.playhead_pos();
  float bar = IM_TRUNC(playhead * 0.25) + 1.0f;
  float beat = IM_TRUNC(std::fmod(playhead, 4.0)) + 1.0f;
  float tick = IM_TRUNC(math::fract(playhead) * g_engine.ppq);
  char buf[32]{};
  fmt::format_to(buf, "{}:{}:{:03}", bar, beat, tick);

  ImFont* font = ImGui::GetFont();
  ImVec2 padding = GImGui->Style.FramePadding;
  ImVec2 position = ImGui::GetCursorScreenPos();
  ImVec2 text_size = ImGui::CalcTextSize(buf);
  ImVec2 size(120.0f + padding.x * 2.0f, text_size.y + padding.y * 2.0f);
  ImRect bb(position, position + size);
  ImGuiID id = ImGui::GetID("##song_position");
  ImDrawList* draw_list = GImGui->CurrentWindow->DrawList;
  uint32_t text_color = ImGui::GetColorU32(ImGuiCol_Text);

  ImGui::ItemSize(size);
  if (!ImGui::ItemAdd(bb, id))
    return;
  
  ImVec2 text_pos = position + (size - text_size) * 0.5f;
  draw_list->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_Button), 2.0f);
  draw_list->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), buf);
}

void item_tooltip(const char* str) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)) {
    ImFont* font = ImGui::GetFont();
    set_current_font(FontType::Normal);  // Force tooltip to use main font
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
    ImGui::SetTooltip(str);
    ImGui::PopStyleVar();
    ImGui::SetCurrentFont(font);
  }
}

bool toggle_button(const char* str, bool value, const ImVec4& toggled_color, const ImVec2& size) {
  return toggle_button(str, &value, toggled_color, size);
}

bool toggle_button(const char* str, bool* value, const ImVec4& toggled_color, const ImVec2& size) {
  if (*value)
    ImGui::PushStyleColor(ImGuiCol_Button, toggled_color);
  bool ret = ImGui::Button(str, size);
  if (*value)
    ImGui::PopStyleColor();
  return ret;
}

bool small_toggle_button(const char* str, bool value, const ImVec4& toggled_color) {
  return small_toggle_button(str, &value, toggled_color);
}

bool small_toggle_button(const char* str, bool* value, const ImVec4& toggled_color) {
  if (*value)
    ImGui::PushStyleColor(ImGuiCol_Button, toggled_color);
  bool ret = ImGui::SmallButton(str);
  if (*value)
    ImGui::PopStyleColor();
  return ret;
}

// bool hsplitter(uint32_t id, float* size, float default_size, float min_size = 0.0f, float max_size = 0.0f) {
//     return hsplitter(ImGui::GetID(id), size, default_size, min_size, max_size);
// }

bool hsplitter(ImGuiID id, float* size, float default_size, float min_size, float max_size, float width) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window->SkipItems)
    return false;

  ImGuiID real_id = ImGui::GetID(id);
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 cur_pos = ImGui::GetCursorScreenPos();
  ImGuiStyle& style = ImGui::GetStyle();
  ImGuiCol color = ImGuiCol_Separator;
  const float splitter_padding = 2.0f;

  width = (width == 0.0f) ? ImGui::GetWindowContentRegionMax().x : width;
  ImRect bb(cur_pos, ImVec2(cur_pos.x + width, cur_pos.y + splitter_padding));
  ImGui::ItemSize(ImVec2(width, splitter_padding));
  if (!ImGui::ItemAdd(bb, real_id)) {
    return false;
  }

  bool is_separator_hovered;
  ImGui::ButtonBehavior(bb, real_id, &is_separator_hovered, nullptr, 0);
  bool is_separator_active = ImGui::IsItemActive();

  if (size) {
    if (is_separator_hovered || is_separator_active) {
      if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        *size = default_size;
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }

    if (is_separator_active) {
      auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
      *size = std::clamp(*size + drag_delta.y, min_size, max_size);
      color = ImGuiCol_SeparatorActive;
    } else if (is_separator_hovered) {
      color = ImGuiCol_SeparatorHovered;
    }
  }

  draw_list->AddLine(
      ImVec2(cur_pos.x, cur_pos.y + 0.5f), ImVec2(cur_pos.x + width, cur_pos.y + 0.5f), ImGui::GetColorU32(color), 2.0f);

  return is_separator_active;
}

bool param_drag_db(
    const char* str_id,
    float* value,
    float speed,
    float min_db,
    float max_db,
    const char* format,
    ImGuiSliderFlags flags) {
  char tmp[16]{};
  const char* str_value = tmp;
  flags |= ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat;

  if (*value > min_db) {
    str_value = "%.2fdb";
  } else {
    str_value = "-INFdB";
  }

  return ImGui::DragFloat(str_id, value, 0.1f, min_db, max_db, str_value, flags);
}

bool param_drag_panning(const char* str_id, float* value, float speed, ImGuiSliderFlags flags) {
  float pan = *value * 100.0f;
  char pan_value[16]{};
  if (pan < 0) {
    fmt::format_to(pan_value, "{:.3}%% L", pan);
  } else if (pan > 0) {
    fmt::format_to(pan_value, "{:.3}%% R", pan);
  } else {
    fmt::format_to(pan_value, "Center");
  }
  flags |= ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat;
  bool ret = ImGui::DragFloat(str_id, &pan, speed, -100.0f, 100.0f, pan_value, flags);
  if (ret) {
    if (!math::near_equal_to_zero(pan, 0.1f * speed)) {
      *value = pan * 0.01f;
    } else {
      *value = 0.0f;
    }
  }
  return ret;
}

bool param_slider_db(
    const SliderProperties& properties,
    const char* str_id,
    const ImVec2& size,
    const Color& color,
    float* value,
    const NonLinearRange& db_range,
    float default_value) {
  const char* format = *value > db_range.min_val ? "%.3fdb" : "-INFdb";
  return slider2(properties, str_id, size, color, value, db_range, default_value, format);
}

bool mixer_label(const char* caption, const float height, const Color& color) {
  float font_size = GImGui->FontSize;
  ImVec2 padding = GImGui->Style.FramePadding;
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImRect bb(cursor_pos, ImVec2(cursor_pos.x + font_size + 10.0f, cursor_pos.y + height));
  ImGuiID id = ImGui::GetID("##mixer_lbl");

  ImGui::ItemSize(bb);
  if (!ImGui::ItemAdd(bb, id))
    return false;

  auto header_color = color.brighten(0.25f).change_alpha(0.7f).to_uint32();
  auto draw_list = GImGui->CurrentWindow->DrawList;
  draw_list->AddRectFilled(bb.Min, ImVec2(bb.Max.x - 3.0f, bb.Max.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
  draw_list->AddRectFilled(ImVec2(bb.Max.x - 3.0f, bb.Min.y), bb.Max, header_color);
  im_draw_vertical_text(
      draw_list, caption, ImVec2(bb.Min.x + 2.0f, bb.Max.y - 4.0f), ImVec4(), ImGui::GetColorU32(ImGuiCol_Text));

  return true;
}

struct VUMeterRange {
  float max;
  float min;
  ImU32 color;
};

static constexpr float min_vu_db = -45.0f;
static constexpr float max_vu_db = 6.0f;
static VUMeterRange vu_ranges[] = {
  {
    math::normalize_value(-12.0f, min_vu_db, max_vu_db),
    math::normalize_value(-45.0f, min_vu_db, max_vu_db),
    ImColor(105, 221, 56),
  },
  {
    math::normalize_value(0.0f, min_vu_db, max_vu_db),
    math::normalize_value(-12.0f, min_vu_db, max_vu_db),
    ImColor(195, 255, 70),
  },
  {
    math::normalize_value(6.0f, min_vu_db, max_vu_db),
    math::normalize_value(0.0f, min_vu_db, max_vu_db),
    ImColor(255, 83, 79),
  },
};

void level_meter_options() {
  int i = 0;
  for (auto& range : vu_ranges) {
    ImGui::PushID(i);
    ImVec4 col = ImGui::ColorConvertU32ToFloat4(range.color);
    ImGui::Text("Color %i", i);
    if (ImGui::ColorEdit3("Color", (float*)&col)) {
      range.color = ImGui::ColorConvertFloat4ToU32(col);
    }
    ImGui::PopID();
    i++;
  }
}

void level_meter(
    const char* str_id,
    const ImVec2& size,
    uint32_t count,
    VUMeter* channels,
    LevelMeterColorMode color_mode,
    bool border) {
  // static const ImU32 channel_color = ImColor(121, 166, 91);
  static constexpr float min_db = -45.0f;
  static constexpr float max_db = 6.0f;
  static const float min_amplitude = math::db_to_linear(min_db);
  static const float max_amplitude = math::db_to_linear(max_db);

  ImVec2 start_pos = ImGui::GetCursorScreenPos();
  ImVec2 end_pos = start_pos + size;
  ImRect bb(start_pos, end_pos);
  float inner_start_y = start_pos.y + 1.0f;
  float inner_end_y = end_pos.y - 1.0f;
  float inner_height = inner_end_y - inner_start_y;
  float channel_size = size.x / (float)count;
  auto draw_list = GImGui->CurrentWindow->DrawList;
  ImU32 border_col;
  ImGuiID id = ImGui::GetID(str_id);

  ImGui::ItemSize(bb);
  if (!ImGui::ItemAdd(bb, id))
    return;

  if (border) {
    border_col = ImGui::GetColorU32(ImGuiCol_Border);
    draw_list->AddRect(start_pos, end_pos, border_col);
  } else {
    border_col = ImGui::GetColorU32(ImGuiCol_FrameBg);
  }

  float pos_x = start_pos.x;
  for (uint32_t i = 0; i < count; i++) {
    float level = math::clamp(channels[i].get_value(), min_amplitude, max_amplitude);
    float channel_pos_x = pos_x;
    pos_x += channel_size;

    if (!border) {
      draw_list->AddRectFilled(
          ImVec2(channel_pos_x + 1.0, start_pos.y + 1.0), ImVec2(pos_x - 1.0, end_pos.y - 1.0), border_col);
    }

    if (level > min_amplitude) {
      float level_db = math::linear_to_db(level);
      float level_norm = math::normalize_value(level_db, min_db, max_db);
      switch (color_mode) {
        case LevelMeterColorMode::Normal: {
          for (const auto& range : vu_ranges) {
            if (level_norm < range.min)
              break;
            float level_start = (1.0f - range.min) * inner_height;
            float level_height = (1.0f - std::min(level_norm, range.max)) * inner_height;
            draw_list->AddRectFilled(
                ImVec2(channel_pos_x + 1.0f, level_height + inner_start_y),
                ImVec2(pos_x - 1.0f, level_start + inner_start_y),
                range.color);
          }
          break;
        }
        case LevelMeterColorMode::Line: {
          ImU32 color = 0;
          for (const auto& range : vu_ranges) {
            if (level_norm <= range.max) {
              color = range.color;
              break;
            }
          }
          float level_height = (1.0f - level_norm) * inner_height;
          draw_list->AddRectFilled(
              ImVec2(channel_pos_x + 1.0f, level_height + inner_start_y), ImVec2(pos_x - 1.0f, end_pos.y - 1.0f), color);
          break;
        }
      }
    }
  }
}

}  // namespace wb::controls