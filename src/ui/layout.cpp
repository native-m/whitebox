#include "layout.h"

namespace wb::layout {

struct ColumnsInstance {
  ImVector<float> column_pos;
  float x;
  float y;
  float max_w;
  float max_h;
  float current_x;
  float current_w;
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
      ImRect current_separator_bb(current_separator_pos, current_separator_pos + ImVec2(separator_size, instance->max_h));
      dl->AddRectFilled(current_separator_bb.Min, current_separator_bb.Max, color);
    }

    instance->current_x += separator_size;
  }

  instance->current_x += width;
  instance->current_w = width;

  ImGui::SetCursorScreenPos(ImVec2(pos_x + prev_x, pos_y));
  return ImGui::BeginChild("##wb_column", ImVec2(width, instance->max_h));
}

float get_current_column_width() {
  assert(cols_state.current_instance != nullptr && "Not inside columns scope");
  return cols_state.current_instance->current_w;
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

}  // namespace wb::layout