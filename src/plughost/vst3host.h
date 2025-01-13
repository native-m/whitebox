#pragma once

#include "core/span.h"
#include "core/vector.h"
#include "plugin_manager.h"
#include <optional>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/vsttypes.h>
#include <public.sdk/source/vst/hosting/connectionproxy.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/module.h>
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

class VST3ComponentHandler : public Steinberg::Vst::IComponentHandler {
  public:
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override;

    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue valueNormalized) override;

    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override;

    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;

  private:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*_iid*/, void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    // we do not care here of the ref-counting. A plug-in call of release should not destroy this
    // class!
    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }
};

struct VST3PluginWrapper : public PluginInterface {
    std::string name_;
    Steinberg::Vst::IComponent* component_ {};
    Steinberg::Vst::IAudioProcessor* processor_ {};
    Steinberg::Vst::IEditController* controller_ {};
    Steinberg::IPlugView* editor_view_ {};
    VST3ComponentHandler component_handler_;
    bool single_component_ = false;

    std::optional<Steinberg::Vst::ConnectionProxy> component_cp_;
    std::optional<Steinberg::Vst::ConnectionProxy> controller_cp_;

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
    PluginResult get_audio_bus_info(bool is_input, uint32_t index, PluginAudioBusInfo* bus) const override;
    PluginResult get_event_bus_info(bool is_input, uint32_t index, PluginEventBusInfo* bus) const override;

    uint32_t get_latency_samples() const override;
    PluginResult init_processing(PluginProcessingMode mode, uint32_t max_samples_per_block,
                                 double sample_rate) override;
    PluginResult process(const AudioBuffer<float>& input, AudioBuffer<float>& output) override;

    bool has_view() const override;
    PluginResult get_view_size(uint32_t* width, uint32_t* height) const override;
    PluginResult attach_window(SDL_Window* handle) override;
    PluginResult detach_window() override;
    PluginResult render_ui() override { return PluginResult::Unimplemented; }

    void disconnect_components_();
};

struct VST3Host {
    VST3::Hosting::Module::Ptr module_;
    std::vector<VST3::Hosting::ClassInfo> class_infos_;
    Steinberg::IPtr<Steinberg::Vst::PlugProvider> plug_provider_;
    Steinberg::OPtr<Steinberg::Vst::IComponent> component_;
    Steinberg::OPtr<Steinberg::Vst::IEditController> controller_;
    Steinberg::Vst::HostApplication plugin_context_;
    VST3ComponentHandler component_handler_;

    Steinberg::IPtr<Steinberg::IPlugView> view;

    VST3Host();
    bool open_module(const std::string& path);
    bool init_view();
};

PluginInterface* vst3_open_plugin(PluginUID uid, const PluginInfo& info);
void vst3_close_plugin(PluginInterface* plugin);
VST3HostApplication* get_vst3_host_application();

} // namespace wb