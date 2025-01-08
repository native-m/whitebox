#pragma once

#include "plughost/plugin_manager.h"

struct ImGuiTableSortSpecs;

namespace wb {
struct GuiPluginManager {
    bool open = false;
    Vector<PluginInfo> plugin_infos;
    void render();
    void update_plugin_info_data(ImGuiTableSortSpecs* sort_specs);
};

extern GuiPluginManager g_plugin_manager;
} // namespace wb