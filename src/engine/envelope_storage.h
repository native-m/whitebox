#pragma once

#include "core/vector.h"
#include <algorithm>
#include <imgui.h>
#include <optional>

namespace wb {

enum class EnvelopePointType {
    Linear,
    ExpUnipolar,
    ExpBipolar,
    PowUnipolar,
    PowBipolar,
    Step,
};

struct EnvelopePoint {
    EnvelopePointType point_type;
    float param;
    double x;
    double y;
};

struct EnvelopeState {
    ImVector<EnvelopePoint> points;

    ImVec2 last_click_pos;
    std::optional<uint32_t> move_point;
    std::optional<uint32_t> context_menu_point;

    void add_point(const EnvelopePoint& point) {
        points.push_back(point);
        std::sort(points.begin(), points.end(),
                  [](const EnvelopePoint& a, const EnvelopePoint& b) { return a.x < b.x; });
    }

    void delete_point(uint32_t index) { points.erase(points.begin() + index); }
};
} // namespace wb