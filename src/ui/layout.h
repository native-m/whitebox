#pragma once

#include <imgui.h>

#include "core/common.h"

namespace wb::layout {

void begin_columns(const char* str_id, uint32_t num_columns, const ImVec2& size = ImVec2());
bool next_column(float default_width);
float get_current_column_width();
void end_columns();

}  // namespace wb::layout