#include "panning_law.h"
#include "core_math.h"
#include <numbers>

namespace wb {

PanningCoefficient calculate_panning_coefs(float p, PanningLaw law) {
    double boost = 0.0f;
    double left = 0.0f;
    double right = 0.0f;
    double x = 0.5 * ((double)p + 1.0); // Rescale to [-1.0, 1.0)

    switch (law) {
        case PanningLaw::Linear:
            left = (1.0 - x) * 0.5;
            right = x * 0.5;
            boost = 2.0;
            break;
        case PanningLaw::Balanced:
            break;
        case PanningLaw::ConstantPower_3db:
            left = std::sin(0.5 * std::numbers::pi * (1.0 - x));
            right = std::sin(0.5 * std::numbers::pi * x);
            boost = std::sqrt(2.0);
            break;
        case PanningLaw::ConstantPower_4_5db:
            break;
        case PanningLaw::ConstantPower_6db:
            break;
    }

    return {(float)(left * boost), (float)(right * boost)};
}

} // namespace wb