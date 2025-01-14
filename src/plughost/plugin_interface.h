#pragma once

#include "core/audio_buffer.h"
#include "core/common.h"
// #include "engine/param_changes.h"

#ifdef WB_PLATFORM_WINDOWS
#define WB_PLUG_API __stdcall
#else
#define WB_PLUG_API
#endif

#define WB_PLUG_FAIL(x) ((x) != PluginResult::Ok)

struct SDL_Window;

namespace wb {
using PluginUID = uint8_t[16];

static constexpr uint32_t plugin_name_size = 256;

enum class PluginResult {
    Ok,
    Failed = -1,
    Unimplemented = -2,
    Unsupported = -3,
};

enum class PluginFormat : uint8_t {
    Native,
    VST3,
    // CLAP
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

struct PluginParamFlags {
    enum {
        Automatable = 1 << 0,
        ReadOnly = 1 << 1,
        Hidden = 1 << 2,
    };
};

struct PluginParamInfo {
    uint32_t id;
    uint32_t flags;
    double default_normalized_value;
    char name[plugin_name_size];
};

struct PluginAudioBusInfo {
    uint32_t id;
    uint32_t channel_count;
    bool default_bus;
    char name[plugin_name_size];
};

struct PluginEventBusInfo {
    uint32_t id;
    char name[plugin_name_size];
};

struct PluginProcessInfo {
    // Audio buffer
    uint32_t sample_count;
    uint32_t input_buffer_count;
    uint32_t output_buffer_count;
    AudioBuffer<float>* input_buffer;
    AudioBuffer<float>* output_buffer;
    double sample_rate;
    int64_t project_time_in_samples;
    bool playing;
};

struct PluginParameterFn {
    void* userdata;
    double(WB_PLUG_API* plain_to_normalized_value)(void* userdata, uint32_t id, double plain);
    double(WB_PLUG_API* normalized_to_plain_value)(void* userdata, uint32_t id, double normalized);
    PluginResult(WB_PLUG_API* set_normalized_value)(void* userdata, uint32_t id, double normalized);
    double(WB_PLUG_API* get_normalized_value)(void* userdata, uint32_t id);
};

struct PluginUIDHash {
    size_t operator()(PluginUID uid) const;
};

struct PluginInterface {
    uint64_t module_hash;
    PluginFormat format;
    bool is_plugin_valid = false;
    SDL_Window* window_handle = nullptr;
    int last_window_x;
    int last_window_y;

    PluginInterface(uint64_t module_hash, PluginFormat format);
    virtual ~PluginInterface() {}
    virtual PluginResult init() = 0;
    virtual PluginResult shutdown() = 0;

    // Get counts
    virtual const char* get_name() const = 0;
    virtual uint32_t get_param_count() const = 0;
    virtual uint32_t get_audio_bus_count(bool is_output) const = 0;
    virtual uint32_t get_event_bus_count(bool is_output) const = 0;
    virtual uint32_t get_latency_samples() const = 0;
    virtual uint32_t get_tail_samples() const = 0;

    // Get plugin informations
    virtual PluginResult get_plugin_param_info(uint32_t index, PluginParamInfo* result) const = 0;
    virtual PluginResult get_audio_bus_info(bool is_output, uint32_t index, PluginAudioBusInfo* bus) const = 0;
    virtual PluginResult get_event_bus_info(bool is_output, uint32_t index, PluginEventBusInfo* bus) const = 0;

    // Busses
    virtual PluginResult activate_audio_bus(bool is_output, uint32_t index, bool state) = 0;

    // Processing
    virtual PluginResult init_processing(PluginProcessingMode mode, uint32_t max_samples_per_block,
                                         double sample_rate) = 0;
    virtual PluginResult start_processing() = 0;
    virtual PluginResult stop_processing() = 0;
    virtual PluginResult process(PluginProcessInfo& process_info) = 0;

    // UI
    virtual bool has_view() const = 0;
    virtual bool has_window_attached() const = 0;
    virtual PluginResult get_view_size(uint32_t* width, uint32_t* height) const = 0;
    virtual PluginResult attach_window(SDL_Window* handle) = 0;
    virtual PluginResult detach_window() = 0;
    virtual PluginResult render_ui() = 0;

    inline bool is_native_plugin() const { return format == PluginFormat::Native; }
};
} // namespace wb