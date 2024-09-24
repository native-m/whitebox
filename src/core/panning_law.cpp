#include "panning_law.h"
#include "core_math.h"
#include <numbers>

namespace wb {

PanningCoefficient calculate_panning_coefs(float p, PanningLaw law) {
    float boost = 0.0f;
    float left = 0.0f;
    float right = 0.0f;

    switch (law) {
        case PanningLaw::Linear:
            left = 1.0f - p;
            right = p;
            boost = 1.0f;
            break;
        case PanningLaw::Balanced:
            break;
        case PanningLaw::ConstantPower_3db:
            left = std::sin(0.5f * std::numbers::pi_v<float> * (1.0f - p));
            right = std::sin(0.5f * std::numbers::pi_v<float> * p);
            boost = std::sqrt(2.0f);
            break;
        case PanningLaw::ConstantPower_4_5db:
            break;
        case PanningLaw::ConstantPower_6db:
            break;
    }

    return { left * boost, right * boost };
}

} // namespace wb