#pragma once

namespace wb {

enum class PanningLaw {
  Linear,
  Balanced,
  ConstantPower_3db,
  ConstantPower_4_5db,
  ConstantPower_6db,
};

struct PanningCoefficient {
  float left;
  float right;
};

PanningCoefficient calculate_panning_coefs(float x, PanningLaw law);

}  // namespace wb