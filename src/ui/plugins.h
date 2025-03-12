#pragma once

#include <string>

#include "core/vector.h"
#include "plughost/plugin_interface.h"

namespace wb {
struct PluginItem {
  PluginFormat format;
  uint32_t flags;
  std::string name;
  std::string vendor;
  PluginUID uid;
};

struct GuiPlugins {
  Vector<PluginItem> items;
  std::string search_text;
  float search_timeout = 0.0;
  bool force_refresh = false;
  bool refit_table_column = false;
  bool first_time = true;

  void render();
  void update_plugin_info_data(ImGuiTableSortSpecs* sort_specs);
};

extern GuiPlugins g_plugins_window;
}  // namespace wb