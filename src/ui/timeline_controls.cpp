#include "timeline_controls.h"

#include <bit>

#include "core/debug.h"
#include "grid.h"
#include "window_manager.h"

namespace wb {

void TimelineViewState::zoom(float acc, float zoom_pos, double song_length, double view_scale) {
  if (end > 1.0) {
    double dist = end - 1.0;
    start -= dist;
    end -= dist;
  }
  double zoom_target = ((float)zoom_pos / song_length * view_scale) + start;
  double dist_from_start = zoom_target - start;
  double dist_to_end = end - zoom_target;
  start = math::clamp(start + dist_from_start * (double)acc, 0.0, end);
  end = math::clamp(end - dist_to_end * (double)acc, start, 1.0);
}

bool TimelineViewState::scroll(float scroll_delta, double song_length, double view_scale) {
  double norm_drag_delta = ((double)scroll_delta / song_length) * view_scale;
  if (scroll_delta != 0.0f) {
    double new_start_pos = start + norm_drag_delta;
    double new_end_pos = end + norm_drag_delta;
    if (new_start_pos >= 0.0f) {
      start = new_start_pos;
      end = new_end_pos;
      return true;
    } else if (new_start_pos < 0.0) {
      start = 0.0;
      end = new_end_pos + math::abs(new_start_pos);
      return true;
    }
  }
  return false;
}

}  // namespace wb

namespace wb::controls {

struct TimelineScrollbarInstance {
  enum {
    None,
    ScrollLeftSide,
    ScrollRightSide,
    ScrollBothSide,
  };

  uint32_t state;
  double old_scroll_pos;
};

static ImPool<TimelineScrollbarInstance> timeline_scrollbars;

bool timeline_scrollbar(
    const char* str_id,
    float width,
    double step_size,
    double max_length,
    TimelineViewState* view_range) {
  assert(view_range != nullptr);
  constexpr float grab_padding = 1.0f;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGuiStyle& style = ImGui::GetStyle();
  float font_size = ImGui::GetFontSize();
  ImGuiID id = ImGui::GetID(str_id);
  TimelineScrollbarInstance* instance = timeline_scrollbars.GetOrAddByKey(id);
  ImVec2 btn_size(font_size + style.FramePadding.x * 2.0f, font_size + style.FramePadding.y * 2.0f);
  ImVec2 grab_size(width - (btn_size.x + grab_padding) * 2.0f, btn_size.y);
  ImVec2 grab_position;
  bool ret = false;

  ImGui::PushID(id);
  ImGui::BeginGroup();
  ImGui::PushButtonRepeat(true);

  if (ImGui::Button("<", btn_size)) {
    double new_scroll_pos = math::max(view_range->start - step_size, 0.0);
    view_range->end = new_scroll_pos + (view_range->end - view_range->start);
    view_range->start = new_scroll_pos;
    ret = true;
  }

  ImGui::SameLine(0.0f, grab_padding);
  grab_position = ImGui::GetCursorScreenPos();
  ImGui::Dummy(grab_size);
  ImGui::SameLine(0.0f, grab_padding);

  if (ImGui::Button(">", btn_size)) {
    view_range->start += step_size;
    view_range->end += step_size;
    ret = true;
  }

  ImGui::PopButtonRepeat();
  ImGui::SetCursorScreenPos(grab_position);
  ImGui::InvisibleButton("_tls_grab", grab_size, ImGuiButtonFlags_MouseButtonLeft);

  ImGuiID state_id = ImGui::GetID("_tls_state");
  int32_t state = instance->state;
  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();
  bool scrolling = state != 0;

  if (scrolling)
    ret = true;

  if (!active && scrolling) {
    instance->state = TimelineScrollbarInstance::None;
    state = TimelineScrollbarInstance::None;
    ImGui::ResetMouseDragDelta();
  }

  double start_pos_val = view_range->start;
  double end_pos_val = view_range->end;
  double scroll_bar_max_length = (double)grab_size.x;
  double min_space = 4.0 / scroll_bar_max_length;
  float grab_start_pos = (float)math::round(math::min(start_pos_val, 1.0 - min_space) * scroll_bar_max_length);
  float grab_end_pos = (float)math::round(math::min(end_pos_val, 1.0) * scroll_bar_max_length);
  float grab_length = grab_end_pos - grab_start_pos;

  if (grab_length < 4.0f) {
    grab_end_pos = grab_start_pos + 4.0f;
  }

  float lhs_x = grab_position.x + grab_start_pos;
  float rhs_x = grab_position.x + grab_end_pos;
  ImVec2 lhs_min(lhs_x, grab_position.y);
  ImVec2 lhs_max(lhs_x + 2.0f, grab_position.y + btn_size.y);
  ImVec2 rhs_min(rhs_x - 2.0f, grab_position.y);
  ImVec2 rhs_max(rhs_x, grab_position.y + btn_size.y);

  if (!scrolling && ImGui::IsMouseHoveringRect(lhs_min, lhs_max)) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (active && state != TimelineScrollbarInstance::ScrollLeftSide) {
      instance->state = TimelineScrollbarInstance::ScrollLeftSide;
      instance->old_scroll_pos = start_pos_val;
    }
  } else if (!scrolling && ImGui::IsMouseHoveringRect(rhs_min, rhs_max)) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (active && state != TimelineScrollbarInstance::ScrollRightSide) {
      instance->state = TimelineScrollbarInstance::ScrollRightSide;
      instance->old_scroll_pos = end_pos_val;
    }
  } else if (!scrolling && active && ImGui::IsMouseHoveringRect(lhs_min, rhs_max)) {
    instance->state = TimelineScrollbarInstance::ScrollBothSide;
    instance->old_scroll_pos = start_pos_val;
  } else if (ImGui::IsItemActivated()) {
    double target_pos = (double)(ImGui::GetMousePos().x - grab_position.x) / scroll_bar_max_length;
    double dist = end_pos_val - start_pos_val;
    double half_dist = dist * 0.5;
    double new_start_pos = math::clamp(target_pos - half_dist, 0.0, 1.0 - dist);
    view_range->start = new_start_pos;
    view_range->end = new_start_pos + dist;
    ret = true;
  }

  switch (state) {
    case TimelineScrollbarInstance::ScrollLeftSide: {
      double old_scroll_pos = instance->old_scroll_pos;
      ImVec2 drag_delta = ImGui::GetMouseDragDelta();
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      view_range->start = math::clamp(old_scroll_pos + drag_delta.x / scroll_bar_max_length, 0.0, end_pos_val - min_space);
      break;
    }
    case TimelineScrollbarInstance::ScrollRightSide: {
      double old_scroll_pos = instance->old_scroll_pos;
      ImVec2 drag_delta = ImGui::GetMouseDragDelta();
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      view_range->end = math::max(old_scroll_pos + drag_delta.x / scroll_bar_max_length, start_pos_val + min_space);
      break;
    }
    case TimelineScrollbarInstance::ScrollBothSide: {
      ImVec2 drag_delta = ImGui::GetMouseDragDelta();
      double dist = end_pos_val - start_pos_val;
      double old_scroll_pos = instance->old_scroll_pos;
      double new_scroll_pos = math::max(old_scroll_pos + drag_delta.x / scroll_bar_max_length, 0.0);
      view_range->start = new_scroll_pos;
      view_range->end = new_scroll_pos + dist;
      break;
    }
    default: break;
  }

  dl->AddRectFilled(lhs_min, rhs_max, ImGui::GetColorU32(ImGuiCol_Button), style.GrabRounding);
  if (hovered || active) {
    uint32_t color = active ? ImGui::GetColorU32(ImGuiCol_FrameBgActive) : ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
    dl->AddRect(lhs_min, rhs_max, color, style.GrabRounding);
  }

  ImGui::EndGroup();
  ImGui::PopID();

  return ret;
}

TimelineRulerResult timeline_ruler(
    const char* str_id,
    int32_t grid_mode,
    bool triplet,
    float width,
    double song_length,
    double* time_pos,
    TimelineViewState* view_range) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGuiStyle& style = ImGui::GetStyle();
  float font_size = ImGui::GetFontSize();
  ImVec2 mouse_pos = ImGui::GetMousePos();
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImVec2 size(width, font_size + style.FramePadding.y * 2.0f);
  double view_scale = view_range->get_view_scale(song_length, width);

  ImGui::InvisibleButton(str_id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
  bool is_active = ImGui::IsItemActive();
  bool is_hovered = ImGui::IsItemHovered();
  bool lmb_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
  bool mmb_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Middle);
  bool holding_lmb = is_active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
  bool holding_mmb = is_active && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  bool mmb_released = ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Middle);
  TimelineRulerResult ret{};

  if (lmb_clicked) {
    double target_pos = (double)(mouse_pos.x - cursor_pos.x) / song_length * view_scale + view_range->start;
    double new_time_pos = math::max(target_pos * song_length, 0.0);
    *time_pos = round_to_grid(new_time_pos, 1.0 / view_scale, grid_mode, triplet);
    ret = TimelineRulerResult::TimePositionChanged;
  }

  auto zoom = [&](float mouse_pos_x, float direction, float rate) {
    float zoom_position = mouse_pos_x - cursor_pos.x;
    view_range->zoom(direction * rate, zoom_position, song_length, view_scale);
    view_scale = view_range->get_view_scale(song_length, width);
  };

  float mouse_wheel = ImGui::GetIO().MouseWheel;
  if (is_hovered && mouse_wheel != 0.0f) {
    static constexpr float zoom_rate = 0.12f;
    zoom(mouse_pos.x, mouse_wheel, zoom_rate);
    ret = TimelineRulerResult::Zoom;
  }

  if (mmb_clicked) {
    ImVec2 pos = ImGui::GetMousePos();
    GImGui->ColorPickerRef.x = mouse_pos.x;
    GImGui->ColorPickerRef.y = mouse_pos.y;
    // Reset relative mouse state to prevent jumping
    wm_set_mouse_pos((int)pos.x, (int)pos.y);
    wm_reset_relative_mouse_state();
    wm_enable_relative_mouse_mode(ImGui::GetWindowViewport(), true);
  }

  if (holding_mmb) {
    int x, y;
    wm_get_relative_mouse_state(&x, &y);
    if (y != 0) {
      constexpr float zoom_rate = 0.01f;
      zoom(GImGui->ColorPickerRef.x, y, zoom_rate);
      ret = TimelineRulerResult::Zoom;
    }
  }

  if (mmb_released) {
    wm_enable_relative_mouse_mode(ImGui::GetWindowViewport(), false);
    wm_set_mouse_pos((int)GImGui->ColorPickerRef.x, (int)GImGui->ColorPickerRef.y);
    view_scale = view_range->get_view_scale(song_length, width);
    ret = TimelineRulerResult::Zoom;
  }

  ImFont* font = ImGui::GetFont();
  ImU32 time_point_color = ImGui::GetColorU32(ImGuiCol_Text);
  ImU32 tick_color = ImGui::GetColorU32(ImGuiCol_Separator, 1.0f);
  double mult = math::max(std::exp2(math::round(std::log2(view_scale * 12.0))), 1.0);
  double inv_view_scale = 1.0 / view_scale;
  double bar = 4.0 * inv_view_scale;
  float grid_inc_x = (float)(bar * mult);
  float inv_grid_inc_x = 1.0f / grid_inc_x;
  float scroll_pos_x = (float)std::round(view_range->start * song_length * inv_view_scale);
  float gridline_pos_x = cursor_pos.x - std::fmod(scroll_pos_x, grid_inc_x);
  float scroll_offset = cursor_pos.x - scroll_pos_x;
  int tick_count = (uint32_t)(size.x * inv_grid_inc_x) + 1;
  int count_offset = (uint32_t)(scroll_pos_x * inv_grid_inc_x);

  ImVec2 max_bb = cursor_pos + size;
  dl->PushClipRect(cursor_pos, max_bb);

  // Draw time points
  uint32_t step = (uint32_t)mult;
  float tick_pos_y = cursor_pos.y + size.y;
  for (int i = 0; i <= tick_count; i++) {
    char digits[24]{};
    int bar_point = i + count_offset;
    float rounded_gridline_pos_x = math::round(gridline_pos_x);
    fmt::format_to_n(digits, sizeof(digits), "{}", bar_point * step + 1);
    dl->AddText(
        ImVec2(rounded_gridline_pos_x + 4.0f, cursor_pos.y + style.FramePadding.y * 2.0f - 2.0f), time_point_color, digits);
    dl->AddLine(
        ImVec2(rounded_gridline_pos_x, tick_pos_y - 8.0f), ImVec2(rounded_gridline_pos_x, tick_pos_y - 3.0f), tick_color);
    gridline_pos_x += grid_inc_x;
  }

  // Draw playhead arrow
  constexpr ImU32 playhead_color = 0xE553A3F9;
  float playhead_size = size.y;
  float playhead_half_size = size.y * 0.5f;
  float playhead_pos = (float)math::round(scroll_offset + *time_pos * inv_view_scale) - playhead_half_size;
  if (math::in_range(playhead_pos, cursor_pos.x - playhead_size, max_bb.x + playhead_size)) {
    dl->AddTriangleFilled(
        ImVec2(playhead_pos, cursor_pos.y + 2.5f),
        ImVec2(playhead_pos + playhead_size, cursor_pos.y + 2.5f),
        ImVec2(playhead_pos + playhead_half_size, cursor_pos.y + playhead_size - 2.5f),
        playhead_color);
  }

  dl->PopClipRect();

  return ret;
}

Pair<double, double>
timeline_zoom(float acc, float zoom_pos, double song_length, double view_scale, double start_pos, double end_pos) {
  if (end_pos > 1.0) {
    double dist = end_pos - 1.0;
    start_pos -= dist;
    end_pos -= dist;
  }

  double zoom_target = ((float)zoom_pos / song_length * view_scale) + start_pos;
  double dist_from_start = zoom_target - start_pos;
  double dist_to_end = end_pos - zoom_target;
  start_pos = math::clamp(start_pos + dist_from_start * (double)acc, 0.0, end_pos);
  end_pos = math::clamp(end_pos - dist_to_end * (double)acc, start_pos, 1.0);

  return { start_pos, end_pos };
}

Pair<double, double> timeline_scroll(float drag_delta, double max_length, double acc, double start_pos, double end_pos) {
  double norm_drag_delta = ((double)drag_delta / max_length) * acc;
  if (drag_delta != 0.0f) {
    double new_start_pos = start_pos + norm_drag_delta;
    double new_end_pos = end_pos + norm_drag_delta;
    if (new_start_pos >= 0.0f) {
      return { new_start_pos, new_end_pos };
    } else if (new_start_pos < 0.0) {
      return { 0.0, new_end_pos + math::abs(new_start_pos) };
    }
  }
  return { start_pos, end_pos };
}

}  // namespace wb::controls