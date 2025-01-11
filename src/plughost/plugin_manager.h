#pragma once

#include "core/common.h"
#include "core/vector.h"
#include "plugin_interface.h"
#include <string>

namespace wb {
using PluginHandle = uint32_t;
static constexpr uint32_t plugin_info_version = 1;

struct PluginFlags {
    enum : uint32_t {
        Effect = 1 << 0,
        Instrument = 1 << 1,
        Analyzer = 1 << 2,
        Hidden = 1 << 3,
    };
};

struct PluginInfo {
    uint8_t uid[16]; // or plugin DB key
    uint32_t structure_version;
    std::string descriptor_id; // ID used by 3rd-party plugin, unused in whitebox native plugin
    std::string name;
    std::string vendor;
    std::string version;
    std::string path;
    uint32_t flags;
    PluginFormat format;
};

using PluginFetchFn = void (*)(void* userdata, PluginInfo&& info);

void init_plugin_manager();
void shutdown_plugin_manager();

void pm_fetch_registered_plugins(const std::string& name_search, void* userdata, PluginFetchFn fn);
void pm_update_plugin_info(const PluginInfo& info);
void pm_delete_plugin(uint8_t plugin_uid[16]);
void pm_scan_plugins();
void register_builtin_plugins();

PluginInterface* pm_open_plugin(PluginUID uid);
void pm_close_plugin(PluginInterface* handle);
} // namespace wb