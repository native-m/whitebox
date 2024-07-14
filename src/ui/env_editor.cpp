#include "env_editor.h"
#include "core/debug.h"
#include "core/math.h"
#include <imgui_internal.h>

namespace wb {
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

EnvEditorWindow g_env_window;

void env_editor(EnvelopeState& state, const char* str_id, const ImVec2& size, double scroll_pos,
                double scale) {
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImVec2 global_mouse_pos = ImGui::GetMousePos();
    ImVec2 mouse_pos = global_mouse_pos - cursor_pos;
    size_t num_points = state.points.size();
    ImVector<EnvelopePoint>& points = state.points;

    ImGui::InvisibleButton(str_id, size);
    bool mouse_down = ImGui::IsItemActive();
    bool activated = ImGui::IsItemActivated();
    bool deactivated = ImGui::IsItemDeactivated();
    bool right_click = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    float end_y = cursor_pos.y + size.y;
    float view_height = size.y;

    if (num_points == 0) {
        if (right_click) {
            double x = (double)mouse_pos.x / scale;
            double y = 1.0 - (double)mouse_pos.y / (double)view_height;
            state.add_point({
                .point_type = EnvelopePointType::Linear,
                .param = 0.0f,
                .x = x,
                .y = y,
            });
        }
        return;
    }

    if (deactivated && state.move_point) {
        uint32_t move_index = state.move_point.value();
        EnvelopePoint& point = points[move_index];
        ImVec2 offset = mouse_pos - state.last_click_pos;
        point.x += (double)offset.x / scale;
        point.y -= (double)offset.y / (double)view_height;
        point.x = math::max(point.x, 0.0);
        point.y = clamp(point.y, 0.0, 1.0);
        if (move_index != 0) {
            point.x = math::max(points[move_index - 1].x, point.x);
        }
        if (num_points - 1 >= move_index + 1) {
            point.x = math::min(points[move_index + 1].x, point.x);
        }
        state.move_point.reset();
    }

    constexpr float click_dist_sq = 25.0f; // 4^2
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 white_pixel = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    double px = points[0].x;
    double py = points[0].y;

    if (state.move_point && state.move_point == 0) {
        ImVec2 offset = mouse_pos - state.last_click_pos;
        px += (double)offset.x / scale;
        py -= (double)offset.y / (double)view_height;
        px = math::max(px, 0.0);
        py = clamp(py, 0.0, 1.0);
        if (num_points - 1 >= 1) {
            px = math::min(points[1].x, px);
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    float x = cursor_pos.x + (float)(px * scale);
    float y = cursor_pos.y + (float)(1.0 - py) * view_height;
    ImVec2 last_pos(x, y);
    float dist = ImLengthSqr(last_pos - global_mouse_pos);

    if (dist <= click_dist_sq) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (activated) {
            state.move_point = 0;
            state.last_click_pos = mouse_pos;
        } else if (right_click) {
            state.context_menu_point = 0;
            ImGui::OpenPopup("env_editor_popup");
        }
    }

    draw_list->AddCircleFilled(last_pos, 4.0f, 0xE553A3F9);

    for (uint32_t i = 1; i < state.points.size(); i++) {
        const EnvelopePoint& point = state.points[i];
        double px = point.x;
        double py = point.y;

        if (state.move_point && state.move_point == i) {
            ImVec2 offset = mouse_pos - state.last_click_pos;
            px += (double)offset.x / scale;
            py -= (double)offset.y / (double)view_height;
            px = math::max(points[i - 1].x, px);
            py = clamp(py, 0.0, 1.0);
            if (num_points - 1 >= i + 1) {
                px = math::min(points[i + 1].x, px);
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }

        float x = cursor_pos.x + (float)(px * scale);
        float y = cursor_pos.y + (float)(1.0 - py) * view_height;
        ImVec2 pos(x, y);
        float dist = ImLengthSqr(pos - global_mouse_pos);

        if (dist <= click_dist_sq) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (activated) {
                state.move_point = i;
                state.last_click_pos = mouse_pos;
            } else if (right_click) {
                state.context_menu_point = i;
                ImGui::OpenPopup("env_editor_popup");
            }
        }

        draw_list->PrimReserve(6, 4);
        draw_list->PrimQuadUV(last_pos, pos, ImVec2(pos.x, end_y), ImVec2(last_pos.x, end_y),
                              white_pixel, white_pixel, white_pixel, white_pixel, 0x2F53A3F9);
        draw_list->AddLine(last_pos, pos, 0xFF53A3F9, 1.5f);
        draw_list->AddCircleFilled(pos, 4.0f, 0xFF53A3F9);
        draw_list->AddCircle(last_pos + (pos - last_pos) * 0.5f, 4.0f, 0xFF53A3F9);
        last_pos = pos;
    }

    bool popup_closed = true;
    if (ImGui::BeginPopup("env_editor_popup")) {
        uint32_t point_idx = state.context_menu_point.value();

        if (ImGui::MenuItem("Delete")) {
            state.delete_point(point_idx);
        }

        if (ImGui::MenuItem("Copy value")) {
            char str[32] {};
            const EnvelopePoint& point = points[point_idx];
            std::format_to_n(str, sizeof(str), "{}", point.y);
            ImGui::SetClipboardText(str);
        }

        if (ImGui::MenuItem("Paste value")) {
            EnvelopePoint& point = points[point_idx];
            std::string str(ImGui::GetClipboardText());
            point.y = std::stod(str);
        }
        
        popup_closed = false;
        ImGui::EndPopup();
    }

    if (right_click && popup_closed) {
        double x = (double)mouse_pos.x / scale;
        double y = 1.0 - (double)mouse_pos.y / (double)view_height;
        state.add_point({
            .point_type = EnvelopePointType::Linear,
            .param = 0.0f,
            .x = x,
            .y = y,
        });
    }
}

} // namespace wb