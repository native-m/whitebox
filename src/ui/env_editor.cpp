#include "env_editor.h"

#include <imgui_internal.h>

#include "core/algorithm.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "window_manager.h"

namespace wb {

EnvEditorWindow g_env_window;

void EnvEditorWindow::render() {
  ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
  if (!ImGui::Begin("Env Editor")) {
    ImGui::PopStyleVar();
    ImGui::End();
    return;
  }

  ImGui::PopStyleVar();

  ImVec2 size = ImGui::GetContentRegionAvail();
  env_editor(env_storage, "ENV_EDITOR", size, 0.0, 1.0);

  ImGui::End();
}

float dist_point_line(const ImVec2& a, const ImVec2& b, const ImVec2& p) {
  float dx = b.x - a.x;
  float dy = b.y - a.y;
  float num = math::abs((dy * p.x - dx * p.y) + (b.x * a.y - b.y * a.x));
  float den = std::sqrt(dx * dx + dy * dy);
  return num / den;
}

template<typename Fn>
void subdivide_curve(
    ImDrawList* draw_list,
    ImVec2 offset,
    float start,
    float mid,
    float end,
    float width,
    float height,
    Fn&& curve_fn) {
  static constexpr float tolerance = 0.4f;

  if (math::near_equal(start, mid)) {
    return;
  }

  float inv_max_x = 1.0f / width;
  float left_y = curve_fn(start * inv_max_x) * height;
  float middle_y = curve_fn(mid * inv_max_x) * height;
  float right_y = curve_fn(end * inv_max_x) * height;

  if (dist_point_line(ImVec2(start, left_y), ImVec2(mid, middle_y), ImVec2(end, right_y)) < tolerance) {
    draw_list->PathLineTo(offset + ImVec2(mid, middle_y));
  } else {
    subdivide_curve(draw_list, offset, start, (start + mid) * 0.5f, mid, width, height, curve_fn);
    subdivide_curve(draw_list, offset, mid, (mid + end) * 0.5f, end, width, height, curve_fn);
  }
}

// This takes path data and draw them as trapezoids
void draw_curve_area(ImDrawList* draw_list, float end_y, uint32_t col) {
  if (draw_list->_Path.Size > 2) {
    draw_list->PrimReserve(6 * (draw_list->_Path.Size - 1), 2 * draw_list->_Path.Size);

    ImVec2 last_pos = draw_list->_Path[0];
    uint32_t idx = draw_list->_VtxCurrentIdx;
    ImDrawVert* vtx_write_ptr = draw_list->_VtxWritePtr;
    ImVec2 white_pixel = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    uint32_t* idx_write_ptr = draw_list->_IdxWritePtr;
    vtx_write_ptr[0].pos = last_pos;
    vtx_write_ptr[0].uv = white_pixel;
    vtx_write_ptr[0].col = col;
    vtx_write_ptr[1].pos.x = last_pos.x;
    vtx_write_ptr[1].pos.y = end_y;
    vtx_write_ptr[1].uv = white_pixel;
    vtx_write_ptr[1].col = col;
    vtx_write_ptr += 2;

    for (uint32_t i = 1; i < draw_list->_Path.Size; i++) {
      const ImVec2& pos = draw_list->_Path[i];
      vtx_write_ptr[0].pos = pos;
      vtx_write_ptr[0].uv = white_pixel;
      vtx_write_ptr[0].col = col;
      vtx_write_ptr[1].pos.x = pos.x;
      vtx_write_ptr[1].pos.y = end_y;
      vtx_write_ptr[1].uv = white_pixel;
      vtx_write_ptr[1].col = col;
      idx_write_ptr[0] = idx;
      idx_write_ptr[1] = (ImDrawIdx)(idx + 1);
      idx_write_ptr[2] = (ImDrawIdx)(idx + 3);
      idx_write_ptr[3] = idx;
      idx_write_ptr[4] = (ImDrawIdx)(idx + 3);
      idx_write_ptr[5] = (ImDrawIdx)(idx + 2);
      idx_write_ptr += 6;
      vtx_write_ptr += 2;
      idx += 2;
      last_pos = pos;
    }

    draw_list->_VtxCurrentIdx = idx + 2;
    draw_list->_VtxWritePtr = vtx_write_ptr;
    draw_list->_IdxWritePtr = idx_write_ptr;
  }
}

template<typename Fn>
inline static void draw_curve(
    ImDrawList* draw_list,
    const ImVec2& p0,
    const ImVec2& p1,
    float end_y,
    uint32_t fill_col,
    uint32_t col,
    ImVec2* tension_point_pos,
    Fn&& curve_fn) {
  float width = p1.x - p0.x;
  float height = p1.y - p0.y;
  float middle_y = curve_fn(0.5f) * height;  // math::exponential_ease(0.5f, power) * height;
  *tension_point_pos = p0 + ImVec2(width * 0.5f, middle_y);
  draw_list->PathLineTo(p0);
  subdivide_curve(draw_list, p0, 0.0f, width * 0.5f, width, width, height, curve_fn);
  draw_list->PathLineTo(p1);
  draw_curve_area(draw_list, end_y, fill_col);
  draw_list->PathStroke(0xFF53A3F9, 0, 1.25f);
  draw_list->AddCircle(*tension_point_pos, 4.0f, col);
}

void env_editor(EnvelopeState& state, const char* str_id, const ImVec2& size, double scroll_pos, double scale) {
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  ImVec2 global_mouse_pos = ImGui::GetMousePos();
  ImVec2 mouse_pos = global_mouse_pos - cursor_pos;
  size_t num_points = state.points.size();
  ImVector<EnvelopePoint>& points = state.points;

  ImGui::InvisibleButton(str_id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  bool hovered = ImGui::IsItemHovered();
  bool mouse_down = ImGui::IsItemActive();
  bool left_click = ImGui::IsItemClicked(ImGuiMouseButton_Left);
  bool right_click = ImGui::IsItemClicked(ImGuiMouseButton_Right);
  bool deactivated = ImGui::IsItemDeactivated();
  bool right_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
  bool moving_point = state.move_control_point || state.move_tension_point;
  float end_y = cursor_pos.y + size.y;
  float view_height = size.y;

  if (num_points == 0) {
    if (right_click) {
      const double x = (double)mouse_pos.x / scale;
      const double y = 1.0 - (double)mouse_pos.y / (double)view_height;
      state.last_click_pos = mouse_pos;
      state.move_control_point = 0;
      state.add_point({
        .point_type = EnvelopePointType::ExpSingle,
        .tension = 0.0f,
        .x = x,
        .y = y,
      });
    }
    return;
  }

  int mouse_rel_x, mouse_rel_y;
  if (state.move_tension_point) {
    wm_get_relative_mouse_state(&mouse_rel_x, &mouse_rel_y);
  }

  if (deactivated && state.move_control_point) {
    const ImVec2 offset = mouse_pos - state.last_click_pos;
    const uint32_t move_index = state.move_control_point.value();
    EnvelopePoint& point = points[move_index];
    point.x += (double)offset.x / scale;
    point.y -= (double)offset.y / (double)view_height;
    point.x = math::max(point.x, 0.0);
    point.y = math::clamp(point.y, 0.0, 1.0);
    if (move_index != 0) {
      point.x = math::max(points[move_index - 1].x, point.x);
    }
    if (num_points - 1 >= move_index + 1) {
      point.x = math::min(points[move_index + 1].x, point.x);
    }
    state.last_tension_value = point.tension;
    state.move_control_point.reset();
  }

  if (deactivated && state.move_tension_point) {
    const uint32_t move_index = state.move_tension_point.value();
    const EnvelopePoint& next_point = points[move_index + 1];
    EnvelopePoint& point = points[move_index];
    state.last_tension_value = point.tension;
    state.move_tension_point.reset();

    // Set new cursor position
    float mid_y = 0.0;
    switch (point.point_type) {
      case EnvelopePointType::ExpSingle: mid_y = math::exponential_ease(0.5f, point.tension * -30.0f); break;
      case EnvelopePointType::ExpAltSingle: mid_y = math::exponential_ease2(0.5f, point.tension * -0.99f); break;
      default: mid_y = (point.y + next_point.y) * 0.5f; break;
    }

    const float x0 = cursor_pos.x + (float)(point.x * scale);
    const float x1 = cursor_pos.x + (float)(next_point.x * scale);
    const float slope = float(point.y - next_point.y) * view_height;
    const float mouse_x = (x0 + x1) * 0.5f;
    const float mouse_y = cursor_pos.y + (1.0f - float(next_point.y)) * view_height - mid_y * slope;
    wm_enable_relative_mouse_mode(false);
    wm_set_mouse_pos((int)mouse_x, (int)mouse_y);
  }

  static constexpr uint32_t fill_col = 0x2F53A3F9;
  static constexpr uint32_t col = 0xFF53A3F9;
  constexpr float click_dist_sq = 25.0f;  // 5^2
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 white_pixel = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
  double px = points[0].x;
  double py = points[0].y;
  int32_t hovered_point = -1;
  bool tension_point_hovered = false;

  if (state.move_control_point && state.move_control_point == 0) {
    ImVec2 offset = mouse_pos - state.last_click_pos;
    px += (double)offset.x / scale;
    py -= (double)offset.y / (double)view_height;
    px = math::max(px, 0.0);
    py = math::clamp(py, 0.0, 1.0);
    if (num_points - 1 >= 1) {
      px = math::min(points[1].x, px);
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
  }

  const float x = cursor_pos.x + (float)(px * scale);
  const float y = cursor_pos.y + (float)(1.0 - py) * view_height;
  EnvelopePointType last_point_type = points[0].point_type;
  ImVec2 last_pos(x, y);

  if (x < global_mouse_pos.x && hovered) {
    hovered_point = 0;
  }

  float dist = ImLengthSqr(last_pos - global_mouse_pos);
  if (dist <= click_dist_sq) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (left_click) {
      state.move_control_point = 0;
      state.last_click_pos = mouse_pos;
    } else if (right_click) {
      state.context_menu_point = 0;
      ImGui::OpenPopup("env_editor_popup");
    }
  }

  draw_list->AddCircleFilled(last_pos, 4.0f, col);

  for (uint32_t i = 1; i < state.points.size(); i++) {
    const EnvelopePoint& point = state.points[i];
    EnvelopePoint& last_point = state.points[i - 1];
    float normalized_tension = last_point.tension;
    double px = point.x;
    double py = point.y;

    // Move the control point
    if (state.move_control_point && state.move_control_point == i) {
      ImVec2 offset = mouse_pos - state.last_click_pos;
      px += (double)offset.x / scale;
      py -= (double)offset.y / (double)view_height;
      px = math::max(points[i - 1].x, px);
      py = math::clamp(py, 0.0, 1.0);
      if (num_points - 1 >= i + 1) {
        px = math::min(points[i + 1].x, px);
      }
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    // Move last curve tension point
    if (state.move_tension_point && state.move_tension_point == (i - 1)) {
      float inc = mouse_rel_y / 500.0f;
      if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
        inc *= 0.25f;
      if (point.y < last_point.y)
        inc = -inc;
      normalized_tension = math::clamp(normalized_tension + inc, -1.0f, 1.0f);
      last_point.tension = normalized_tension;
    }

    const float x = cursor_pos.x + (float)(px * scale);
    const float y = cursor_pos.y + (float)(1.0 - py) * view_height;
    const ImVec2 pos(x, y);

    if (x < global_mouse_pos.x && hovered) {
      hovered_point = (int32_t)i;
    }

    // Draw the curve
    bool has_control_point = false;
    ImVec2 tension_point_pos;
    switch (last_point_type) {
      case EnvelopePointType::Linear: {
        draw_list->PrimReserve(6, 4);
        draw_list->PrimQuadUV(
            last_pos,
            pos,
            ImVec2(pos.x, end_y),
            ImVec2(last_pos.x, end_y),
            white_pixel,
            white_pixel,
            white_pixel,
            white_pixel,
            fill_col);
        draw_list->AddLine(last_pos, pos, col, 1.25f);
        break;
      }
      case EnvelopePointType::ExpSingle: {
        constexpr float max_tension = 30.0f;
        float power = normalized_tension * max_tension;
        auto curve_fn = [=](float pos) { return math::exponential_ease(pos, power); };
        draw_curve(draw_list, last_pos, pos, end_y, fill_col, col, &tension_point_pos, curve_fn);
        has_control_point = true;
        break;
      }
      case EnvelopePointType::ExpAltSingle: {
        constexpr float max_tension = 0.99f;
        float power = normalized_tension * max_tension;
        auto curve_fn = [=](float pos) { return math::exponential_ease2(pos, power); };
        draw_curve(draw_list, last_pos, pos, end_y, fill_col, col, &tension_point_pos, curve_fn);
        has_control_point = true;
        break;
      }
      case EnvelopePointType::PowSingle: {
        has_control_point = true;
        break;
      }
      default: break;
    }

    draw_list->AddCircleFilled(pos, 4.0f, col);

    // Check if the control point is clicked
    float dist = ImLengthSqr(pos - global_mouse_pos);
    bool control_point_hovered = false;
    if (dist <= click_dist_sq && !moving_point) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
      control_point_hovered = true;
      if (left_click) {
        state.move_control_point = i;
        state.last_click_pos = mouse_pos;
      } else if (right_click) {
        state.context_menu_point = i;
        ImGui::OpenPopup("env_editor_popup");
      }
    }

    // Check if the tension point is clicked
    if (has_control_point && !moving_point && !control_point_hovered) {
      float dist = ImLengthSqr(tension_point_pos - global_mouse_pos);
      if (dist <= click_dist_sq) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        tension_point_hovered = true;
        if (left_click) {
          state.move_tension_point = i - 1;
          state.last_click_pos = mouse_pos;
          wm_set_mouse_pos((int)global_mouse_pos.x, (int)global_mouse_pos.y);
          wm_reset_relative_mouse_state();
          wm_enable_relative_mouse_mode(true);
        } else if (right_click) {
          last_point.tension = 0.0f;
          state.last_tension_value = 0.0f;
        }
      }
    }

    last_point_type = point.point_type;
    last_pos = pos;
  }

  bool popup_closed = true;
  if (ImGui::BeginPopup("env_editor_popup")) {
    uint32_t point_idx = state.context_menu_point.value();

    if (ImGui::MenuItem("Delete")) {
      state.delete_point(point_idx);
    }

    if (ImGui::MenuItem("Copy value")) {
      char str[32]{};
      const EnvelopePoint& point = points[point_idx];
      fmt::format_to_n(str, sizeof(str), "{}", point.y);
      ImGui::SetClipboardText(str);
    }

    if (ImGui::MenuItem("Paste value")) {
      EnvelopePoint& point = points[point_idx];
      std::string str(ImGui::GetClipboardText());
      point.y = std::stod(str);
    }

    if (point_idx != 0) {
      EnvelopePointType& point_type = points[point_idx - 1].point_type;
      bool linear = point_type == EnvelopePointType::Linear;
      bool exp_single = point_type == EnvelopePointType::ExpSingle;
      bool exp_alt_single = point_type == EnvelopePointType::ExpAltSingle;
      ImGui::Separator();
      ImGui::MenuItem("Curve type", nullptr, nullptr, false);
      if (ImGui::MenuItem("Linear", nullptr, &linear))
        point_type = EnvelopePointType::Linear;
      if (ImGui::MenuItem("Exponential", nullptr, &exp_single))
        point_type = EnvelopePointType::ExpSingle;
      if (ImGui::MenuItem("Exponential Alt.", nullptr, &exp_alt_single))
        point_type = EnvelopePointType::ExpAltSingle;
    }

    popup_closed = false;
    ImGui::EndPopup();
  }

  if (right_click && popup_closed && !tension_point_hovered) {
    double x = (double)mouse_pos.x / scale;
    double y = 1.0 - (double)mouse_pos.y / (double)view_height;
    float hovered_tension = 0.0f;
    state.move_control_point = hovered_point + 1;
    state.last_click_pos = mouse_pos;
    if (hovered_point > -1) {
      hovered_tension = state.points[hovered_point].tension;
      state.points[hovered_point].tension = state.last_tension_value;
    }
    state.add_point({
      .point_type = EnvelopePointType::ExpSingle,
      .tension = hovered_tension,
      .x = x,
      .y = y,
    });
  }
}

}  // namespace wb