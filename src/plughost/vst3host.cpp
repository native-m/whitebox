#include "vst3host.h"
#include "core/bit_manipulation.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "core/memory.h"
#include "extern/xxhash.h"
#include <SDL_syswm.h>
#include <SDL_video.h>
#include <public.sdk/source/common/memorystream.h>
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

static VST3Module* get_module(uint64_t hash) {
    auto module = vst3_module_cache.find(hash);
    if (module != vst3_module_cache.end())
        return &module->second;
    return nullptr;
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
    Log::debug("beginEdit called ({})", id);
    return Steinberg::kNotImplemented;
}

Steinberg::tresult PLUGIN_API VST3ComponentHandler::performEdit(Steinberg::Vst::ParamID id,
                                                                Steinberg::Vst::ParamValue valueNormalized) {
    // SMTG_DBPRT2("performEdit called (%d, %f)\n", id, valueNormalized);
    Log::debug("performEdit called ({}, {})", id, valueNormalized);
    return Steinberg::kNotImplemented;
}

Steinberg::tresult PLUGIN_API VST3ComponentHandler::endEdit(Steinberg::Vst::ParamID id) {
    // SMTG_DBPRT1("endEdit called (%d)\n", id);
    Log::debug("endEdit called ({})", id);
    return Steinberg::kNotImplemented;
}

Steinberg::tresult PLUGIN_API VST3ComponentHandler::restartComponent(Steinberg::int32 flags) {
    // SMTG_DBPRT1("restartComponent called (%d)\n", flags);
    Log::debug("restartComponent called ({})", flags);
    return Steinberg::kNotImplemented;
}

//

Steinberg::tresult PLUGIN_API VST3PlugFrame::resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* rect) {
    Log::debug("resizeView called ({:x})", (size_t)view);
    SDL_SetWindowSize(instance->window_handle, rect->getWidth(), rect->getHeight());
    view->onSize(rect);
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API VST3PlugFrame::queryInterface(const Steinberg::TUID _iid, void** obj) {
    if (Steinberg::FUnknownPrivate::iidEqual(_iid, IPlugFrame::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(_iid, FUnknown::iid)) {
        *obj = this;
        addRef();
        return Steinberg::kResultTrue;
    }
    return Steinberg::kNoInterface;
}

//

VST3PluginWrapper::VST3PluginWrapper(uint64_t module_hash, const std::string& name,
                                     Steinberg::Vst::IComponent* component,
                                     Steinberg::Vst::IEditController* controller) :
    PluginInterface(module_hash, PluginFormat::VST3),
    name_(name),
    component_(component),
    controller_(controller),
    plug_frame_(this) {
    last_window_x = SDL_WINDOWPOS_CENTERED;
    last_window_y = SDL_WINDOWPOS_CENTERED;
}

VST3PluginWrapper::~VST3PluginWrapper() {
}

PluginResult VST3PluginWrapper::init() {
    Steinberg::IPluginBase* component_base = component_;
    if (component_base->initialize(&vst3_host_app) != Steinberg::kResultOk)
        return PluginResult::Failed;

    if (component_->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&processor_) != Steinberg::kResultOk)
        return PluginResult::Failed;

    if (!controller_) {
        // The plugin did not separate the controller
        if (component_->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&controller_) !=
            Steinberg::kResultOk) {
            return PluginResult::Failed;
        }
        if (!controller_)
            return PluginResult::Failed;
        single_component_ = true;
    }

    // Initialize the controller
    Steinberg::IPluginBase* controller_base = controller_;
    if (controller_base->initialize(&vst3_host_app) != Steinberg::kResultOk)
        return PluginResult::Failed;

    controller_->setComponentHandler(&component_handler_);

    // If the component is separated, connect the component with controller manually
    if (!single_component_) {
        Steinberg::Vst::IConnectionPoint* component_icp;
        Steinberg::Vst::IConnectionPoint* controller_icp;

        if (component_->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&component_icp) !=
            Steinberg::kResultOk)
            return PluginResult::Failed;
        if (controller_->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&controller_icp) !=
            Steinberg::kResultOk) {
            component_icp->release();
            return PluginResult::Failed;
        }

        component_cp_.emplace(component_icp);
        controller_cp_.emplace(controller_icp);
        if (component_cp_->connect(controller_icp) != Steinberg::kResultOk) {
            component_icp->release();
            controller_icp->release();
            return PluginResult::Failed;
        }

        if (controller_cp_->connect(component_icp) != Steinberg::kResultOk) {
            component_icp->release();
            controller_icp->release();
            return PluginResult::Failed;
        }

        component_icp->release();
        controller_icp->release();
    }

    Steinberg::IPlugView* view = controller_->createView(Steinberg::Vst::ViewType::kEditor);
    if (!view)
        view = controller_->createView(nullptr);
    if (!view)
        controller_->queryInterface(Steinberg::IPlugView::iid, (void**)&view);
    if (view)
        editor_view_ = view;

    // Cache paramater info
    int32_t param_count = controller_->getParameterCount();
    if (params.size == 0) {
        if (auto module = get_module(module_hash)) {
            Steinberg::Vst::ParameterInfo param_info;
            module->param_cache.resize(param_count);
            for (int32_t i = 0; i < param_count; i++) {
                PluginParamInfo& wb = module->param_cache[i];
                controller_->getParameterInfo(i, param_info);
                wb.id = param_info.id;
                if (has_bit(param_info.flags, Steinberg::Vst::ParameterInfo::kCanAutomate))
                    wb.flags |= PluginParamFlags::Automatable;
                if (has_bit(param_info.flags, Steinberg::Vst::ParameterInfo::kIsReadOnly))
                    wb.flags |= PluginParamFlags::ReadOnly;
                if (has_bit(param_info.flags, Steinberg::Vst::ParameterInfo::kIsHidden))
                    wb.flags |= PluginParamFlags::Hidden;
                wb.default_normalized_value = param_info.defaultNormalizedValue;
                std::memcpy(wb.name, param_info.title, sizeof(param_info.title));
            }
            params.assign(module->param_cache.begin(), module->param_cache.end());
        }
    }

    uint32_t num_input = get_audio_bus_count(false);
    uint32_t num_output = get_audio_bus_count(true);
    uint32_t max_arrangements = math::max(num_input, num_output);
    Vector<Steinberg::Vst::SpeakerArrangement> arrangements;
    arrangements.resize(max_arrangements, Steinberg::Vst::SpeakerArr::kStereo);
    Steinberg::tresult result =
        processor_->setBusArrangements(arrangements.data(), num_input, arrangements.data(), num_output);
    if (result == Steinberg::kResultFalse)
        Log::debug("Some plugin buses do not support stereo channel");

    return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::shutdown() {
    disconnect_components_();
    if (editor_view_)
        editor_view_->release();
    if (component_)
        component_->terminate();
    if (controller_ && !single_component_)
        controller_->terminate();
    if (controller_)
        controller_->release();
    if (processor_)
        processor_->release();
    if (component_)
        component_->release();
    return PluginResult::Ok;
}

const char* VST3PluginWrapper::get_name() const {
    return name_.c_str();
}

uint32_t VST3PluginWrapper::get_param_count() const {
    return params.size;
}

uint32_t VST3PluginWrapper::get_audio_bus_count(bool is_output) const {
    return component_->getBusCount(Steinberg::Vst::MediaTypes::kAudio, is_output);
}

uint32_t VST3PluginWrapper::get_event_bus_count(bool is_output) const {
    return component_->getBusCount(Steinberg::Vst::MediaTypes::kEvent, is_output);
}

uint32_t VST3PluginWrapper::get_latency_samples() const {
    return processor_->getLatencySamples();
}

uint32_t VST3PluginWrapper::get_tail_samples() const {
    return processor_->getTailSamples();
}

PluginResult VST3PluginWrapper::get_plugin_param_info(uint32_t index, PluginParamInfo* result) const {
    if (index >= params.size)
        return PluginResult::Failed;
    *result = params[index];
    return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::get_audio_bus_info(bool is_output, uint32_t index, PluginAudioBusInfo* bus) const {
    size_t retval;
    Steinberg::Vst::BusInfo bus_info;
    Steinberg::tresult result = component_->getBusInfo(Steinberg::Vst::MediaTypes::kAudio, is_output, index, bus_info);
    if (result == Steinberg::kInvalidArgument)
        return PluginResult::Failed;
    bus->id = index;
    bus->channel_count = bus_info.channelCount;
    bus->default_bus = has_bit(bus_info.flags, Steinberg::Vst::BusInfo::kDefaultActive);
    std::wcstombs(bus->name, (const wchar_t*)bus_info.name, sizeof(bus_info.name));
    return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::get_event_bus_info(bool is_output, uint32_t index, PluginEventBusInfo* bus) const {
    Steinberg::Vst::BusInfo bus_info;
    Steinberg::tresult result = component_->getBusInfo(Steinberg::Vst::MediaTypes::kAudio, is_output, index, bus_info);
    if (result == Steinberg::kInvalidArgument)
        return PluginResult::Failed;
    size_t retval;
    bus->id = index;
    std::wcstombs(bus->name, (const wchar_t*)bus_info.name, sizeof(bus_info.name));
    return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::activate_audio_bus(bool is_output, uint32_t index, bool state) {
    return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::init_processing(PluginProcessingMode mode, uint32_t max_samples_per_block,
                                                double sample_rate) {
    Steinberg::Vst::ProcessSetup setup {
        .processMode = mode == PluginProcessingMode::Offline ? Steinberg::Vst::kOffline : Steinberg::Vst::kOffline,
        .symbolicSampleSize = Steinberg::Vst::kSample32,
        .maxSamplesPerBlock = (int32_t)max_samples_per_block,
        .sampleRate = sample_rate,
    };

    if (processor_->setupProcessing(setup) != Steinberg::kResultOk)
        return PluginResult::Failed;

    return PluginResult::Unimplemented;
}

PluginResult VST3PluginWrapper::process(const AudioBuffer<float>& input, AudioBuffer<float>& output) {
    return PluginResult::Unimplemented;
}

bool VST3PluginWrapper::has_view() const {
    return editor_view_ != nullptr;
}

bool VST3PluginWrapper::has_window_attached() const {
    return window_handle != nullptr;
}

PluginResult VST3PluginWrapper::get_view_size(uint32_t* width, uint32_t* height) const {
    if (!has_view())
        return PluginResult::Unsupported;
    Steinberg::ViewRect rect;
    if (editor_view_->getSize(&rect) != Steinberg::kResultOk)
        return PluginResult::Failed;
    *width = rect.getWidth();
    *height = rect.getHeight();
    return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::attach_window(SDL_Window* handle) {
    if (!has_view())
        return PluginResult::Unsupported;
    if (has_window_attached())
        return PluginResult::Failed;
    SDL_SysWMinfo wm_info {};
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(handle, &wm_info);
#ifdef WB_PLATFORM_WINDOWS
    if (editor_view_->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) != Steinberg::kResultOk)
        return PluginResult::Unsupported;
    window_handle = handle;
    Steinberg::tresult result = editor_view_->setFrame(&plug_frame_);
    Log::debug("{}", result);
    /*if (editor_view_->setFrame(&plug_frame_) != Steinberg::kResultOk)
        return PluginResult::Failed;*/
    if (editor_view_->attached(wm_info.info.win.window, Steinberg::kPlatformTypeHWND) != Steinberg::kResultOk) {
        window_handle = nullptr;
        return PluginResult::Failed;
    }
    return PluginResult::Ok;
#else
    return PluginResult::Unsupported;
#endif
}

PluginResult VST3PluginWrapper::detach_window() {
    if (!has_view())
        return PluginResult::Unsupported;
    if (window_handle == nullptr)
        return PluginResult::Unsupported;
    editor_view_->setFrame(nullptr);
    if (auto result = editor_view_->removed(); result != Steinberg::kResultOk)
        Log::debug("Failed to detach window {}", result);
    SDL_GetWindowPosition(window_handle, &last_window_x, &last_window_y);
    window_handle = nullptr;
    return PluginResult::Ok;
}

void VST3PluginWrapper::disconnect_components_() {
    if (controller_cp_)
        controller_cp_->disconnect();
    if (component_cp_)
        component_cp_->disconnect();
    controller_cp_.reset();
    component_cp_.reset();
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

PluginInterface* vst3_open_plugin(PluginUID uid, const PluginInfo& info) {
    assert(info.descriptor_id.size() == sizeof(Steinberg::TUID));
    VST3Module* module = create_module(info.path);
    if (!module)
        return nullptr;

    Steinberg::TUID tuid;
    std::memcpy(tuid, info.descriptor_id.data(), info.descriptor_id.size());

    // Create plugin component instance
    auto& factory = module->mod_ptr->getFactory();
    Steinberg::IPtr<Steinberg::Vst::IComponent> component =
        factory.createInstance<Steinberg::Vst::IComponent>(VST3::UID::fromTUID(tuid));
    if (!component) {
        release_module(module->hash);
        Log::debug("Cannot create VST3 plugin component");
        return nullptr;
    }

    // Try creating controller
    Steinberg::TUID controller_uid;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    if (component->getControllerClassId(controller_uid) == Steinberg::kResultOk)
        controller = factory.createInstance<Steinberg::Vst::IEditController>(controller_uid);

    void* ptr = vst3_plugins.allocate();
    if (!ptr) {
        component.reset();
        release_module(module->hash);
        Log::debug("Cannot allocate memory to open VST3 plugin");
        return nullptr;
    }

    return new (ptr) VST3PluginWrapper(module->hash, info.name, component.take(), controller.take());
}

void vst3_close_plugin(PluginInterface* plugin) {
    uint64_t module_hash = plugin->module_hash;
    vst3_plugins.destroy(static_cast<VST3PluginWrapper*>(plugin));
    release_module(module_hash);
}

VST3HostApplication* get_vst3_host_application() {
    return &vst3_host_app;
}

} // namespace wb