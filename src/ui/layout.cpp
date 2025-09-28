#include "layout.h"

#include <imgui_internal.h>
#include "core/debug.h"
#include "core/core_math.h"

namespace wb::layout {

struct Columns {
  float pos;
  float default_w;
  float min_w;
  float max_h;
};

struct ColumnsInstance {
  ImVector<Columns> cols;
  float x;
  float y;
  float frame_w;
  float frame_h;
  float current_x;
  float current_w;
  float scroll_x;
  float scroll_y;
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

  ImGuiID id = ImGui::GetID(str_id);
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ColumnsInstance* cols = cols_state.instances.GetOrAddByKey(id);
  cols->x = cursor_pos.x;
  cols->y = cursor_pos.y;
  cols->current_x = 0.0f;
  cols->max_columns = num_columns;
  cols->outer_columns = cols_state.current_instance;

  if (cols->first_time_setup)
    cols->cols.reserve(num_columns);

  cols_state.current_instance = cols;
  ImGui::BeginChild(id, size);
  available_space = ImGui::GetContentRegionAvail();
  cols->frame_w = available_space.x;
  cols->frame_h = available_space.y;
  cols->scroll_x = ImGui::GetScrollX();
  cols->scroll_y = ImGui::GetScrollY();
  //Log::debug("{}", available_space.x);
}

bool next_column(float default_width, float min_width) {
  assert(cols_state.current_instance != nullptr && "Not inside columns scope");
  ColumnsInstance* instance = cols_state.current_instance;
  constexpr float separator_size = 2.0f;
  constexpr float default_minimum_width = 30.0f;
  const float pos_abs_x = instance->x;
  const float pos_abs_y = instance->y;
  const float pos_x = instance->x - instance->scroll_x;
  const float pos_y = instance->y - instance->scroll_y;
  const float prev_x = instance->current_x;

  if (instance->counter == instance->max_columns)
    return false;

  // Close previous column scope
  if (instance->counter != 0) {
    uint32_t col_idx = instance->counter - 1;
    float height = ImGui::GetCursorPos().y - ImGui::GetWindowContentRegionMin().y;
    instance->cols[col_idx].max_h = height;
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
  }

  uint32_t col_idx = instance->counter++;
  float width = default_width;
  if (instance->first_time_setup) {
    if (instance->counter >= instance->max_columns)
      width = instance->frame_w - prev_x;
    instance->cols.push_back(Columns{
      .pos = prev_x + width,
      .default_w = width,
      .min_w = min_width,
    });
  } else {
    // float last_width = instance->max_w - prev_x;
    // width = instance->counter < instance->max_columns ? instance->cols[col_idx].pos - prev_x : last_width;
    if (instance->counter >= instance->max_columns)
      width = instance->frame_w - prev_x;
    else
      width = instance->cols[col_idx].pos - prev_x;
  }

  ImGui::PushID(col_idx);

  if (instance->counter < instance->max_columns) {
    float column_pos = instance->cols[col_idx].pos;
    ImGuiID separator_id = ImGui::GetID("__vsp");
    ImVec2 separator_pos(pos_abs_x + column_pos, pos_abs_y);
    ImRect separator_bb(separator_pos, separator_pos + ImVec2(separator_size, instance->frame_h));

    ImGui::ItemSize(ImVec2(separator_size, 0.0f));
    if (ImGui::ItemAdd(separator_bb, separator_id)) {
      bool hovered, held;
      ImGui::ButtonBehavior(separator_bb, separator_id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft);
      int color_idx = ImGuiCol_Separator;

      // Handle dragging
      if (held) {
        auto drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 1.0f);
        float old_pos = column_pos;
        float next_pos = instance->frame_w;

        if (instance->counter < instance->cols.size())
          next_pos = instance->cols[col_idx + 1].pos;

        float maximum_size = next_pos - prev_x - default_minimum_width;
        float minimum_size = math::max(instance->cols[col_idx].min_w, default_minimum_width);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        instance->cols[col_idx].pos = old_pos + drag_delta.x;
        width = math::clamp(instance->cols[col_idx].pos - prev_x, minimum_size, maximum_size - separator_size);
        color_idx = ImGuiCol_ResizeGripActive;
      } else if (hovered) {
        color_idx = ImGuiCol_ResizeGripHovered;
      }

      // Apply column width
      if (ImGui::IsItemDeactivated()) {
        float next_pos = instance->frame_w;
        if (instance->counter < instance->cols.size())
          next_pos = instance->cols[col_idx + 1].pos;
        float maximum_size = next_pos - prev_x - default_minimum_width;
        float minimum_size = math::max(instance->cols[col_idx].min_w, default_minimum_width);
        width = math::clamp(instance->cols[col_idx].pos - prev_x, minimum_size, maximum_size - separator_size);
        instance->cols[col_idx].pos = prev_x + width;
      }

      if (color_idx != ImGuiCol_Separator)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

      // Draw the separator
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImU32 color = ImGui::GetColorU32(color_idx);
      ImVec2 current_separator_pos(pos_abs_x + prev_x + width, pos_abs_y);
      ImRect current_separator_bb(current_separator_pos, current_separator_pos + ImVec2(separator_size, instance->frame_h));
      dl->AddRectFilled(current_separator_bb.Min, current_separator_bb.Max, color);
    }

    instance->current_x += separator_size;
  }

  instance->current_x += width;
  instance->current_w = width;

  constexpr uint32_t child_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  ImVec2 tmp_item_spacing = GImGui->Style.ItemSpacing;
  ImVec2 column_pos(pos_x + prev_x, pos_y);
  float height = math::max(instance->frame_h, instance->cols[col_idx].max_h);
  ImGui::SetCursorScreenPos(column_pos);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
  //Log::debug("{}", height);
  bool ret = ImGui::BeginChild("##__wb_col", ImVec2(width, height), 0, child_flags);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, tmp_item_spacing);
  return ret;
}

float get_current_column_width() {
  assert(cols_state.current_instance != nullptr && "Not inside columns scope");
  return cols_state.current_instance->current_w;
}

void end_columns() {
  assert(cols_state.current_instance != nullptr && "Not inside columns scope");
  ColumnsInstance* instance = cols_state.current_instance;

  if (instance->counter != 0) {
    uint32_t col_idx = instance->counter - 1;
    float height = ImGui::GetCursorPos().y - ImGui::GetWindowContentRegionMin().y;
    instance->cols[col_idx].max_h = height;
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
  }

  if (instance->first_time_setup)
    instance->first_time_setup = false;

  instance->counter = 0;
  cols_state.current_instance = instance->outer_columns;
  instance->outer_columns = nullptr;
  ImGui::EndChild();
}

}  // namespace wb::layout