#include "vst3host.h"
#include "core/debug.h"

namespace wb {

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
    //controller_->
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

} // namespace wb