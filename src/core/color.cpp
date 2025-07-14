#include "color.h"

#include <imgui.h>

namespace wb {

Color::Color(const ImVec4& vec4) : r(vec4.x), g(vec4.y), b(vec4.z), a(vec4.w) {
}

ImVec4 Color::to_vec4() const {
  return ImVec4(r, g, b, a);
}

}  // namespace wb