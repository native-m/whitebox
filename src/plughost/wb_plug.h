#pragma once

#include "core/common.h"
#include "engine/param_changes.h"

namespace wb {

enum class PluginResult {
    Ok,
    Failed,
};

enum class PluginParamType {
    I32,
    U32,
    F32,
    F64,
    Normalized,
};

enum class PluginProcessingMode {
    Realtime,
    Offline,
};

union PluginParamValue {
    int32_t i32;
    uint32_t u32;
    float f32;
    double f64;
};

struct PluginParamInfo {
    uint32_t id;
    const char* name;
    PluginParamType type;
    int32_t step_count;
    PluginParamValue default_value;
};

struct PluginParamFn {
    void* userdata;
    double(PLUGIN_API *plain_to_normalized_value)(void* userdata, uint32_t id, double plain);
    double(PLUGIN_API *normalized_to_plain_value)(void* userdata, uint32_t id, double normalized);
    PluginResult(PLUGIN_API *set_normalized_value)(void* userdata, uint32_t id, double normalized);
    double(PLUGIN_API *get_normalized_value)(void* userdata, uint32_t id);
};

struct PluginInterface {
    virtual ~PluginInterface() {}
    virtual PluginResult init() = 0;
    virtual PluginResult get_plugin_param_info(uint32_t id, PluginParamInfo* result) = 0;
    virtual PluginResult set_param_value() = 0;
    virtual PluginResult init_processing(PluginProcessingMode mode) = 0;
    virtual PluginResult process() = 0;
    virtual PluginResult render_imgui() = 0;
};
} // namespace wb