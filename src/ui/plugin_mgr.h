#pragma once

#include "core/bitset.h"
#include "plughost/plugin_manager.h"
#include <unordered_set>

struct ImGuiTableSortSpecs;

namespace wb {
struct GuiPluginManager {
    Vector<PluginInfo> plugin_infos;
    std::unordered_set<uint32_t> selected_plugin_set;
    uint32_t num_selected_plugins = 0;
    BitSet selected_plugins;
    std::string search_text;
    float search_timeout = 0.0;

    void render();
    void update_plugin_info_data(ImGuiTableSortSpecs* sort_specs);
    void delete_selected();
};

extern GuiPluginManager g_plugin_manager;
} // namespace wb