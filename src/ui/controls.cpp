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

struct ColumnsInstance {
  ImVector<float> column_pos;
  float x;
  float y;
  float max_w;
  float max_h;
  float current_x;
  uint32_t counter;
  uint32_t max_columns;
  uint32_t gc_timeout = 32;
  bool first_time_setup = true;
  ColumnsInstance* outer_columns;
};

struct ColumnsState {
  ImPool<ColumnsInstance> instances;
  ColumnsInstance* current_instance;
};

static ColumnsState cols_state;

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

void begin_columns(const char* str_id, uint32_t num_columns, const ImVec2& size) {
  assert(num_columns > 1 && "There must be at least 2 columns");
  ImVec2 available_space = ImGui::GetContentRegionAvail();
  float w = size.x == 0.0f ? available_space.x : size.x;
  float h = size.y == 0.0f ? available_space.y : size.y;

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImGuiID id = ImGui::GetID(str_id);
  ColumnsInstance* cols = cols_state.instances.GetOrAddByKey(id);
  cols->x = cursor_pos.x;
  cols->y = cursor_pos.y;
  cols->max_w = w;
  cols->max_h = h;
  cols->current_x = 0.0f;
  cols->max_columns = num_columns;
  cols->outer_columns = cols_state.current_instance;

  if (cols->first_time_setup) {
    cols->column_pos.reserve(num_columns - 1);
  }

  ImGui::BeginGroup();
  ImGui::Dummy(size);
  ImGui::SetCursorScreenPos(cursor_pos);
  cols_state.current_instance = cols;
}

bool next_column(float default_width) {
  assert(cols_state.current_instance != nullptr && "Not inside columns scope");
  ColumnsInstance* instance = cols_state.current_instance;
  constexpr float separator_size = 2.0f;
  constexpr float minimum_size = 30.0f;
  const float pos_x = instance->x;
  const float pos_y = instance->y;
  const float prev_x = instance->current_x;

  if (instance->counter == instance->max_columns)
    return false;

  // Close previous column scope
  if (instance->counter != 0) {
    ImGui::EndChild();
    ImGui::PopID();
  }

  uint32_t col_idx = instance->counter++;
  float width = default_width;
  if (instance->first_time_setup && instance->counter < instance->max_columns) {
    instance->column_pos.push_back(prev_x + width);
  } else {
    float last_width = instance->max_w - prev_x;
    width = instance->counter < instance->max_columns ? instance->column_pos[col_idx] - prev_x : last_width;
  }

  ImGui::PushID(col_idx);

  if (instance->counter < instance->max_columns) {
    float column_pos = instance->column_pos[col_idx];
    ImGuiID separator_id = ImGui::GetID("vsp");
    ImVec2 separator_pos(pos_x + column_pos, pos_y);
    ImRect separator_bb(separator_pos, separator_pos + ImVec2(separator_size, instance->max_h));

    ImGui::ItemSize(ImVec2(separator_size, 0.0f));
    if (ImGui::ItemAdd(separator_bb, separator_id)) {
      bool hovered, held;
      ImGui::ButtonBehavior(separator_bb, separator_id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft);
      int color_idx = ImGuiCol_Separator;

      // Handle dragging
      if (held) {
        auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
        float old_pos = column_pos;
        float next_pos = instance->max_w;

        if (instance->counter < instance->column_pos.size())
          next_pos = instance->column_pos[col_idx + 1];

        float maximum_size = next_pos - prev_x - minimum_size;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        instance->column_pos[col_idx] = old_pos + drag_delta.x;
        width = math::clamp(instance->column_pos[col_idx] - prev_x, minimum_size - separator_size, maximum_size);
        color_idx = ImGuiCol_ResizeGripActive;
      } else if (hovered) {
        color_idx = ImGuiCol_ResizeGripHovered;
      }

      // Apply column width
      if (ImGui::IsItemDeactivated()) {
        float next_pos = instance->max_w;
        if (instance->counter < instance->column_pos.size())
          next_pos = instance->column_pos[col_idx + 1];
        float maximum_size = next_pos - prev_x - minimum_size;
        width = math::clamp(instance->column_pos[col_idx] - prev_x, minimum_size - separator_size, maximum_size);
        instance->column_pos[col_idx] = prev_x + width;
      }

      if (color_idx != ImGuiCol_Separator)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

      // Draw the separator
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImU32 color = ImGui::GetColorU32(color_idx);
      ImVec2 current_separator_pos(pos_x + prev_x + width, pos_y);
      ImRect current_separator_bb(
          current_separator_pos, current_separator_pos + ImVec2(separator_size, instance->max_h));
      dl->AddRectFilled(current_separator_bb.Min, current_separator_bb.Max, color);
    }

    instance->current_x += separator_size;
  }

  instance->current_x += width;

  ImGui::SetCursorScreenPos(ImVec2(pos_x + prev_x, pos_y));
  return ImGui::BeginChild("##wb_column", ImVec2(width, instance->max_h));
}

void end_columns() {
  assert(cols_state.current_instance != nullptr && "Not inside columns scope");
  ColumnsInstance* instance = cols_state.current_instance;

  if (instance->counter != 0) {
    ImGui::EndChild();
    ImGui::PopID();
  }

  if (instance->first_time_setup)
    instance->first_time_setup = false;

  instance->counter = 0;
  cols_state.current_instance = instance->outer_columns;
  instance->outer_columns = nullptr;
  ImGui::EndGroup();
}

void song_position() {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window->SkipItems)
    return;

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
  static constexpr uint32_t hover_flags = ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_Stationary |
                                          ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay;
  if (ImGui::IsItemHovered(hover_flags)) {
    ImFont* font = ImGui::GetFont();
    set_current_font(FontType::Normal);  // Force tooltip to use main font
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
    ImGui::SetTooltip(str);
    ImGui::PopStyleVar();
    ImGui::SetCurrentFont(font);
  }
}

bool timeline_scrollbar(double* start_time, double* end_time) {
  return true;
}

bool toggle_button(const char* str, bool value, const ImVec4& toggled_color, const ImVec2& size) {
  return toggle_button(str, &value, toggled_color, size);
}

bool toggle_button(const char* str, bool* value, const ImVec4& toggled_color, const ImVec2& size) {
  if (*value)
    ImGui::PushStyleColor(ImGuiCol_Button, toggled_color);
  bool last_value = *value;
  bool ret = ImGui::Button(str, size);
  if (ret)
    *value = !last_value;
  if (last_value)
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

bool icon_toggle_button(const char* str, bool value, const ImVec4& toggled_color) {
  return icon_toggle_button(str, &value, toggled_color);
}

bool icon_toggle_button(const char* str, bool* value, const ImVec4& toggled_color) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window->SkipItems)
    return false;

  ImGuiContext& g = *GImGui;
  const ImGuiStyle& style = g.Style;
  const ImGuiID id = window->GetID(str);
  const ImVec2 label_size = ImGui::CalcTextSize(str, NULL, true);

  ImVec2 pos = window->DC.CursorPos;
  /*if ((flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset)
    pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;*/
  ImVec2 size(label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

  const ImRect bb(pos, pos + size);
  ImGui::ItemSize(size, style.FramePadding.y);
  if (!ImGui::ItemAdd(bb, id))
    return false;

  bool last_value = *value;
  bool hovered, held;
  bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);

  if (pressed)
    *value = !last_value;

  const char* text_display_end = ImGui::FindRenderedTextEnd(str, nullptr);
  const int text_len = (int)(text_display_end - str);
  if (text_len == 0)
    return pressed;

  ImU32 base_color = ImGui::GetColorU32(toggled_color);
  ImU32 color = last_value ? base_color : WB_IM_COLOR_U32_SET_ALPHA(ImGui::GetColorU32(ImGuiCol_Text), 0x80);

  if (hovered)
    window->DrawList->AddRect(bb.Min, bb.Max, base_color, 4.0f, 0, 1.5f);
  window->DrawList->AddText(nullptr, 0.0f, bb.Min + style.FramePadding, color, str, text_display_end, 0.0f, nullptr);

  return pressed;
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

bool musical_unit_drags(const char* label, double* value) {
  static constexpr uint32_t drag_flags = ImGuiSliderFlags_NoInput | ImGuiSliderFlags_Vertical;
  double beats = *value;
  double bars = beats / 4.0;
  double sixteenths = beats * 4.0;
  int32_t length[3]{
    (int32_t)bars,
    (int32_t)beats % 4,
    (int32_t)sixteenths % 4,
  };

  ImGui::BeginGroup();
  ImGui::PushID(label);
  ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());

  const char* bars_str;
  ImFormatStringToTempBuffer(&bars_str, nullptr, "%d", length[0]);  // Format manually to hide jumping values
  ImGui::PushID(1);
  bool bars_changed = ImGui::DragScalar("", ImGuiDataType_S32, &length[0], 0.25f, nullptr, nullptr, bars_str, drag_flags);
  ImGui::PopItemWidth();
  ImGui::PopID();

  const char* beats_str;
  ImFormatStringToTempBuffer(&beats_str, nullptr, "%d", length[1]);
  ImGui::PushID(2);
  ImGui::SameLine(0.0f, GImGui->Style.ItemInnerSpacing.x);
  bool beats_changed = ImGui::DragScalar("", ImGuiDataType_S32, &length[1], 0.25f, nullptr, nullptr, beats_str, drag_flags);
  ImGui::PopItemWidth();
  ImGui::PopID();

  const char* sixteenths_str;
  ImFormatStringToTempBuffer(&sixteenths_str, nullptr, "%d", length[2]);
  ImGui::PushID(3);
  ImGui::SameLine(0.0f, GImGui->Style.ItemInnerSpacing.x);
  bool sixteenths_changed =
      ImGui::DragScalar("", ImGuiDataType_S32, &length[2], 0.25f, nullptr, nullptr, sixteenths_str, drag_flags);
  ImGui::PopItemWidth();
  ImGui::PopID();

  ImGui::SameLine(0.0f, GImGui->Style.ItemInnerSpacing.x);
  ImGui::TextEx(label, ImGui::FindRenderedTextEnd(label), ImGuiTextFlags_NoWidthForLargeClippedText);

  ImGui::PopID();
  ImGui::EndGroup();

  if (bars_changed || beats_changed || sixteenths_changed) {
    double min_length = 4.0;
    if (beats_changed)
      min_length = 1.0;
    else if (sixteenths_changed)
      min_length = 1.0 / 4.0;
    double new_length = ((double)length[0]) * 4.0 + (double)length[1] + ((double)length[2] / 4.0);
    *value = math::max(new_length, min_length);
    return true;
  }

  return false;
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
    float* value,
    const NonLinearRange& db_range,
    float default_value) {
  const char* format = *value > db_range.min_val ? "%.3fdb" : "-INFdb";
  return slider2(properties, str_id, size, value, db_range, default_value, format);
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