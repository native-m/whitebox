#pragma once

#include "core/bitset.h"
#include "plughost/plugin_manager.h"

struct ImGuiTableSortSpecs;

namespace wb {
struct GuiPluginManager {
    bool open = false;
    Vector<PluginInfo> plugin_infos;
    uint32_t num_selected_plugins = 0;
    BitSet selected_plugins;
    std::string search_text;
    float search_timeout = 0.0;

    void render();
    void update_plugin_info_data(ImGuiTableSortSpecs* sort_specs);
};

extern GuiPluginManager g_plugin_manager;
} // namespace wb