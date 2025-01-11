#pragma once

#include "plughost/plugin_interface.h"
#include "core/vector.h"
#include <string>

namespace wb {
struct PluginItem {
    PluginFormat format;
    uint32_t flags;
    std::string name;
    std::string vendor;
    PluginUID uid;
};

struct GuiPlugins {
    bool open = true;
    Vector<PluginItem> items;
    std::string search_text;
    float search_timeout = 0.0;

    void render();
    void update_plugin_info_data(ImGuiTableSortSpecs* sort_specs);
};

extern GuiPlugins g_plugins_window;
} // namespace wb