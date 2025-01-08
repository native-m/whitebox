#pragma once

#include "core/common.h"
#include "core/vector.h"
#include <string>

namespace wb {
using PluginHandle = uint32_t;

static constexpr uint32_t plugin_info_version = 1;

enum class PluginType : uint8_t {
    Native,
    VST3,
    // CLAP
};

struct PluginFlags {
    enum : uint32_t {
        Effect = 1 << 0,
        Instrument = 1 << 1,
        Analyzer = 1 << 2,
        Hidden = 1 << 3,
    };
};

struct PluginUID {
    uint8_t data[16];
};

struct PluginInfo {
    uint8_t plugin_uid[16]; // or plugin DB key
    uint32_t version;
    std::string descriptor_id; // ID used by 3rd-party plugin, unused in whitebox native plugin
    std::string name;
    std::string vendor;
    std::string version;
    std::string path;
    uint32_t flags;
    PluginType type;
};

void init_plugin_manager();
Vector<PluginInfo> load_plugin_info();
void scan_plugins();
void register_builtin_plugins();
void shutdown_plugin_manager();
} // namespace wb