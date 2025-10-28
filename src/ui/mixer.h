#pragma once

namespace wb {

struct MixerWindow {
  bool show_devices = false;
  bool show_sends = false;

  void render();
};

extern MixerWindow g_mixer;
}  // namespace wb