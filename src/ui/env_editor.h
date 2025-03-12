#pragma once

#include "engine/envelope_storage.h"

namespace wb {

struct EnvEditorWindow {
  EnvelopeState env_storage;
  void render();
};

extern EnvEditorWindow g_env_window;

void env_editor(EnvelopeState& storage, const char* str_id, const ImVec2& size, double scroll_pos, double scale);
}  // namespace wb