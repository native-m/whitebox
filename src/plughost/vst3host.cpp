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

    const VST3::Hosting::PluginFactory& factory = module_->getFactory();
    class_infos_ = factory.classInfos();
    for (auto& class_info : class_infos_) {
        Log::debug("{}", class_info.name());
        if (class_info.category() == kVstAudioEffectClass) {
            plug_provider_ =
                Steinberg::owned(new Steinberg::Vst::PlugProvider(factory, class_info));
            break;
        }
    }

    if (!plug_provider_) {
        Log::debug("VST3 Error: No module classes found");
        return false;
    }

    component_.reset(plug_provider_->getComponent());
    controller_.reset(plug_provider_->getController());
    controller_->setComponentHandler(&component_handler_);
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