#include "vst3host.h"
#include "core/debug.h"
#include "core/memory.h"
#include "extern/xxhash.h"
#include <unordered_map>

namespace wb {
static VST3HostApplication vst3_host_app;
static std::unordered_map<uint64_t, VST3Module> vst3_module_cache;
static Pool<VST3PluginWrapper> vst3_plugins;

static VST3Module* create_module(const std::string& path) {
    uint64_t hash = XXH64(path.data(), path.size(), 69420);

    auto module = vst3_module_cache.find(hash);
    if (module != vst3_module_cache.end()) {
        module->second.ref_count++;
        return &module->second;
    }

    std::string error;
    VST3::Hosting::Module::Ptr mod_instance = VST3::Hosting::Module::create(path, error);
    if (!mod_instance) {
        Log::debug("Cannot open VST3 module {}", path);
        Log::debug("Reason {}", error);
        return nullptr;
    }

    auto new_module = vst3_module_cache.try_emplace(hash, hash, std::move(mod_instance));
    return &new_module.first->second;
}

static void release_module(uint64_t hash) {
    auto module = vst3_module_cache.find(hash);
    if (module == vst3_module_cache.end())
        return;
    if (module->second.ref_count-- == 1)
        vst3_module_cache.erase(hash);
}

//

Steinberg::tresult VST3HostApplication::getName(Steinberg::Vst::String128 name) {
    static const char host_name[] = "whitebox";
    std::memcpy(name, host_name, sizeof(host_name));
    return Steinberg::kResultOk;
}

//

Steinberg::tresult PLUGIN_API VST3ComponentHandler::beginEdit(Steinberg::Vst::ParamID id) {
    // SMTG_DBPRT1("beginEdit called (%d)\n", id);
    return Steinberg::kNotImplemented;
}

Steinberg::tresult PLUGIN_API VST3ComponentHandler::performEdit(Steinberg::Vst::ParamID id,
                                                                Steinberg::Vst::ParamValue valueNormalized) {
    // SMTG_DBPRT2("performEdit called (%d, %f)\n", id, valueNormalized);
    return Steinberg::kNotImplemented;
}

Steinberg::tresult PLUGIN_API VST3ComponentHandler::endEdit(Steinberg::Vst::ParamID id) {
    // SMTG_DBPRT1("endEdit called (%d)\n", id);
    return Steinberg::kNotImplemented;
}

Steinberg::tresult PLUGIN_API VST3ComponentHandler::restartComponent(Steinberg::int32 flags) {
    // SMTG_DBPRT1("restartComponent called (%d)\n", flags);
    return Steinberg::kNotImplemented;
}

//

VST3PluginWrapper::VST3PluginWrapper(uint64_t module_hash, Steinberg::Vst::IComponent* component) :
    PluginInterface(module_hash, PluginFormat::VST3), component_(component) {
}

VST3PluginWrapper::~VST3PluginWrapper() {
}

PluginResult VST3PluginWrapper::init() {
    Steinberg::IPluginBase* plugin_base = component_;
    if (plugin_base->initialize(&vst3_host_app) != Steinberg::kResultOk)
        return PluginResult::Failed;
    return PluginResult::Unimplemented;
}

PluginResult VST3PluginWrapper::shutdown() {
    return PluginResult::Unimplemented;
}

uint32_t VST3PluginWrapper::get_plugin_param_count() const {
    return 0;
}

uint32_t VST3PluginWrapper::get_audio_bus_count() const {
    return 0;
}

uint32_t VST3PluginWrapper::get_event_bus_count() const {
    return 0;
}

PluginResult VST3PluginWrapper::get_plugin_param_info(uint32_t id, PluginParamInfo* result) const {
    return PluginResult::Unimplemented;
}

PluginResult VST3PluginWrapper::get_audio_bus_info(bool is_input, uint32_t index, PluginAudioBusInfo* bus) const {
    return PluginResult::Unimplemented;
}

PluginResult VST3PluginWrapper::get_event_bus_info(bool is_input, uint32_t index, PluginEventBusInfo* bus) const {
    return PluginResult::Unimplemented;
}

uint32_t VST3PluginWrapper::get_latency_samples() const {
    return 0;
}

PluginResult VST3PluginWrapper::init_processing(PluginProcessingMode mode, uint32_t max_samples_per_block,
                                                double sample_rate) {
    return PluginResult::Unimplemented;
}

PluginResult VST3PluginWrapper::process(const AudioBuffer<float>& input, AudioBuffer<float>& output) {
    return PluginResult::Unimplemented;
}

//

VST3Host::VST3Host() {
    Steinberg::Vst::PluginContextFactory::instance().setPluginContext(&plugin_context_);
}

bool VST3Host::open_module(const std::string& path) {
    std::string error_msg;
    module_ = VST3::Hosting::Module::create(path, error_msg);
    if (!module_) {
        Log::debug("VST3 Error: {}", error_msg);
        return false;
    }

    int32_t index = -1;
    const VST3::Hosting::PluginFactory& factory = module_->getFactory();
    class_infos_ = factory.classInfos();
    for (int32_t idx = 0; auto& class_info : class_infos_) {
        Log::debug("----------------------");
        Log::debug("Name: {}", class_info.name());
        Log::debug("ID: {}", class_info.ID().toString());
        if (class_info.category() == kVstAudioEffectClass)
            index = idx;
        idx++;
    }

    plug_provider_ = Steinberg::owned(new Steinberg::Vst::PlugProvider(factory, class_infos_[index]));

    if (!plug_provider_) {
        Log::debug("VST3 Error: No module classes found");
        return false;
    }

    component_.reset(plug_provider_->getComponent());
    controller_.reset(plug_provider_->getController());
    controller_->setComponentHandler(&component_handler_);
    // controller_->
    return true;
}

bool VST3Host::init_view() {
    if (!controller_) {
        Log::debug("VST plugin has no view!");
        return false;
    }
    view = controller_->createView(Steinberg::Vst::ViewType::kEditor);
    if (!view) {
        Log::debug("Cannot create plugin view");
        return false;
    }
    return true;
}

PluginInterface* vst3_open_plugin(PluginUID uid, const std::string& descriptor_id, const std::string& module_path) {
    assert(descriptor_id.size() == sizeof(Steinberg::TUID));
    VST3Module* module = create_module(module_path);
    return nullptr;
}

void vst3_close_plugin(PluginInterface* plugin) {

}

VST3HostApplication* get_vst3_host_application() {
    return &vst3_host_app;
}

} // namespace wb