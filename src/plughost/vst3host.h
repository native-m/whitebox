#pragma once

#include "core/bitset.h"
#include "core/span.h"
#include "core/vector.h"
#include "engine/event_list.h"
#include "engine/param_changes.h"
#include "plugin_manager.h"
#include <optional>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/vsttypes.h>
#include <public.sdk/source/vst/hosting/connectionproxy.h>
#include <public.sdk/source/vst/hosting/eventlist.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/module.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>
#include <public.sdk/source/vst/hosting/plugprovider.h>

namespace wb {

struct VST3PluginWrapper;

struct VST3Module {
    uint64_t hash;
    VST3::Hosting::Module::Ptr mod_ptr;
    Vector<PluginParamInfo> param_cache; // Let's not waste memory by caching the parameter information
    uint32_t ref_count = 1;
    VST3Module(uint64_t hash, VST3::Hosting::Module::Ptr&& mod) : hash(hash), mod_ptr(std::move(mod)) {}
};

class VST3HostApplication : public Steinberg::Vst::HostApplication {
  public:
    Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override;
};

class VST3ParameterChanges : public Steinberg::Vst::IParameterChanges {
  public:
    ParamChanges param_changes;

    Steinberg::int32 PLUGIN_API getParameterCount() override { return param_changes.changes_count; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override { return nullptr; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id,
                                                                  Steinberg::int32& index) override {
        return nullptr;
    }

    // Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData()
};

class VST3InputEventList : public Steinberg::Vst::IEventList {
  public:
    MidiEventList* event_list_ = nullptr;

    inline void set_event_list(MidiEventList* event_list) { event_list_ = event_list; }

    Steinberg::int32 PLUGIN_API getEventCount() SMTG_OVERRIDE { return event_list_ ? event_list_->size() : 0; }

    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, Steinberg::Vst::Event& e) SMTG_OVERRIDE;

    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) SMTG_OVERRIDE;

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
    // we do not care here of the ref-counting. A plug-in call of release should not destroy this
    // class!
    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }
};

struct VST3PluginWrapper : public PluginInterface,
                           public Steinberg::Vst::IComponentHandler,
                           public Steinberg::IPlugFrame {
    std::string name_;
    Steinberg::Vst::IComponent* component_ {};
    Steinberg::Vst::IAudioProcessor* processor_ {};
    Steinberg::Vst::IEditController* controller_ {};
    Steinberg::IPlugView* editor_view_ {};

    uint32_t sample_size_ = Steinberg::Vst::kSample32;
    uint32_t max_samples_per_block_ = 0;
    int32_t current_process_mode_ = Steinberg::Vst::kRealtime;
    bool single_component_ = false;
    bool has_view_ = false;

    Steinberg::Vst::IConnectionPoint* component_icp_;
    Steinberg::Vst::IConnectionPoint* controller_icp_;

    std::optional<Steinberg::Vst::ConnectionProxy> component_cp_;
    std::optional<Steinberg::Vst::ConnectionProxy> controller_cp_;
    Vector<Steinberg::Vst::AudioBusBuffers> input_bus_buffers_;
    Vector<Steinberg::Vst::AudioBusBuffers> output_bus_buffers_;
    Steinberg::Vst::ParameterChanges input_param_changes_ {};
    VST3InputEventList input_events_;
    Steinberg::Vst::EventList output_events_;
    Span<PluginParamInfo> params;

    VST3PluginWrapper(uint64_t module_hash, const std::string& name, Steinberg::Vst::IComponent* component,
                      Steinberg::Vst::IEditController* controller);
    virtual ~VST3PluginWrapper();
    PluginResult init() override;
    PluginResult shutdown() override;

    const char* get_name() const override;
    uint32_t get_param_count() const override;
    uint32_t get_audio_bus_count(bool is_output) const override;
    uint32_t get_event_bus_count(bool is_output) const override;
    uint32_t get_latency_samples() const override;
    uint32_t get_tail_samples() const override;

    PluginResult get_plugin_param_info(uint32_t id, PluginParamInfo* result) const override;
    PluginResult get_audio_bus_info(bool is_output, uint32_t index, PluginAudioBusInfo* bus) const override;
    PluginResult get_event_bus_info(bool is_output, uint32_t index, PluginEventBusInfo* bus) const override;

    PluginResult activate_audio_bus(bool is_output, uint32_t index, bool state) override;
    PluginResult activate_event_bus(bool is_output, uint32_t index, bool state) override;

    PluginResult init_processing(PluginProcessingMode mode, uint32_t max_samples_per_block,
                                 double sample_rate) override;
    PluginResult start_processing() override;
    PluginResult stop_processing() override;
    void transfer_param(uint32_t param_id, double normalized_value) override;
    PluginResult process(PluginProcessInfo& process_info) override;

    bool has_view() const override;
    bool has_window_attached() const override;
    PluginResult get_view_size(uint32_t* width, uint32_t* height) const override;
    PluginResult attach_window(SDL_Window* handle) override;
    PluginResult detach_window() override;
    PluginResult render_ui() override { return PluginResult::Unimplemented; }

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override;

    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue valueNormalized) override;

    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override;

    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;

    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* rect) override;

    void disconnect_components_();

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
    // we do not care here of the ref-counting. A plug-in call of release should not destroy this
    // class!
    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }
};

PluginInterface* vst3_open_plugin(PluginUID uid, const PluginInfo& info);
void vst3_close_plugin(PluginInterface* plugin);
VST3HostApplication* get_vst3_host_application();

} // namespace wb