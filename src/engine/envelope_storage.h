#pragma once

#include <imgui.h>

#include <algorithm>
#include <optional>

#include "core/vector.h"

namespace wb {

enum class EnvelopePointType {
  Hold,
  Linear,
  ExpSingle,
  ExpDual,
  ExpAltSingle,
  ExpAltDual,
  PowSingle,
  PowDual,
  Step,
};

struct EnvelopePoint {
  EnvelopePointType point_type;
  float tension;
  double x;
  double y;
};

struct EnvelopeState {
  ImVector<EnvelopePoint> points;
  ImVec2 last_click_pos;
  float last_tension_value = 1.0f;
  bool holding_point = false;
  std::optional<uint32_t> move_control_point;
  std::optional<uint32_t> move_tension_point;
  std::optional<uint32_t> context_menu_point;

  void add_point(const EnvelopePoint& point) {
    points.push_back(point);
    std::sort(points.begin(), points.end(), [](const EnvelopePoint& a, const EnvelopePoint& b) { return a.x < b.x; });
  }

  void delete_point(uint32_t index) {
    points.erase(points.begin() + index);
  }
};

}  // namespace wb