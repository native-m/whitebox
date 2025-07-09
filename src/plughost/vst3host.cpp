#include "vst3host.h"

#include <pluginterfaces/vst/ivstprocesscontext.h>
#include <public.sdk/source/common/memorystream.h>

#include <unordered_map>

#include "core/bit_manipulation.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "core/memory.h"
#include "extern/sdl_wm.h"
#include "extern/xxhash.h"
#include "window_manager.h"

#define VST3_WARN(x)                                 \
  if (auto ret = (x); ret != Steinberg::kResultOk) { \
    Log::debug(#x " returned {}", ret);              \
  }

#define VST3_FAILED(x) ((x) != Steinberg::kResultOk)

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

inline static Steinberg::tresult vst3_result(PluginResult result) {
  switch (result) {
    case PluginResult::Ok: return Steinberg::kResultOk;
    case PluginResult::Failed: return Steinberg::kResultFalse;
    case PluginResult::Unimplemented: return Steinberg::kNotImplemented;
    default: WB_UNREACHABLE();
  }
}

//

Steinberg::tresult VST3HostApplication::getName(Steinberg::Vst::String128 name) {
  static const char host_name[] = "whitebox";
  std::memcpy(name, host_name, sizeof(host_name));
  return Steinberg::kResultOk;
}

//

Steinberg::tresult PLUGIN_API VST3InputEventList::getEvent(Steinberg::int32 index, Steinberg::Vst::Event& e) {
  if (index >= 0 || index < event_list->size()) {
    const auto& event = event_list->events[index];
    e.busIndex = event.bus_index;
    e.sampleOffset = event.buffer_offset;
    e.ppqPosition = event.time;
    switch (event.type) {
      case MidiEventType::NoteOn:
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.noteOn.channel = event.note_on.channel;
        e.noteOn.pitch = event.note_on.key;
        e.noteOn.tuning = event.note_on.tuning;
        e.noteOn.velocity = event.note_on.velocity;
        e.noteOn.length = 0;
        e.noteOn.noteId = -1;
        break;
      case MidiEventType::NoteOff:
        e.type = Steinberg::Vst::Event::kNoteOffEvent;
        e.noteOff.channel = event.note_off.channel;
        e.noteOff.pitch = event.note_off.key;
        e.noteOff.tuning = event.note_off.tuning;
        e.noteOff.velocity = event.note_off.velocity;
        e.noteOff.noteId = -1;
        break;
      default: return Steinberg::kResultFalse;
    }
    return Steinberg::kResultOk;
  }
  return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API VST3InputEventList::addEvent(Steinberg::Vst::Event& e) {
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API VST3InputEventList::queryInterface(const Steinberg::TUID iid, void** obj) {
  if (Steinberg::FUnknownPrivate::iidEqual(iid, IEventList::iid) ||
      Steinberg::FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
    *obj = this;
    addRef();
    return Steinberg::kResultTrue;
  }
  return Steinberg::kNoInterface;
}

//

VST3PluginWrapper::VST3PluginWrapper(
    uint64_t module_hash,
    const std::string& name,
    Steinberg::Vst::IComponent* component,
    Steinberg::Vst::IEditController* controller)
    : PluginInterface(module_hash, PluginFormat::VST3),
      name_(name),
      component_(component),
      controller_(controller) {
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
    if (component_->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&controller_) != Steinberg::kResultOk) {
      return PluginResult::Failed;
    }
    if (!controller_)
      return PluginResult::Failed;
    single_component_ = true;
  } else {
    Steinberg::IPluginBase* controller_base = controller_;
    if (controller_base->initialize(&vst3_host_app) != Steinberg::kResultOk)
      return PluginResult::Failed;
  }

  controller_->setComponentHandler(this);

  // If the component is separated, connect the component with controller manually
  if (!single_component_) {
    if (component_->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&component_icp_) != Steinberg::kResultOk)
      return PluginResult::Failed;
    if (controller_->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&controller_icp_) !=
        Steinberg::kResultOk) {
      component_icp_->release();
      return PluginResult::Failed;
    }

    if (component_icp_->connect(controller_icp_) != Steinberg::kResultOk) {
      component_icp_->release();
      controller_icp_->release();
      return PluginResult::Failed;
    }

    if (controller_icp_->connect(component_icp_) != Steinberg::kResultOk) {
      component_icp_->release();
      controller_icp_->release();
      return PluginResult::Failed;
    }
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
        std::wcstombs(wb.name, (const wchar_t*)param_info.title, sizeof(wb.name));
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

  input_bus_buffers_.resize(num_input);
  output_bus_buffers_.resize(num_output);

  if (processor_->canProcessSampleSize(Steinberg::Vst::kSample32) == Steinberg::kResultOk)
    sample_size_ = Steinberg::Vst::kSample32;
  else if (processor_->canProcessSampleSize(Steinberg::Vst::kSample64) == Steinberg::kResultOk)
    sample_size_ = Steinberg::Vst::kSample64;

  assert(sample_size_ == Steinberg::Vst::kSample32 && "kSample64 is not supported at the moment");

  return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::shutdown() {
  disconnect_components_();
  /*for (uint32_t i = 0; i < get_audio_bus_count(false); i++)
      if (active_input_audio_bus_.get(i))
          activate_audio_bus(false, i, false);
  for (uint32_t i = 0; i < get_audio_bus_count(true); i++)
      if (active_output_audio_bus_.get(i))
          activate_audio_bus(true, i, false);*/
  if (editor_view_)
    editor_view_->release();
  if (controller_ && !single_component_)
    controller_->terminate();
  if (component_)
    component_->terminate();
  if (controller_)
    controller_->release();
  if (processor_)
    processor_->release();
  if (component_)
    component_->release();
  return PluginResult::Ok;
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

const char* VST3PluginWrapper::get_name() const {
  return name_.c_str();
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
  std::wcstombs(bus->name, (const wchar_t*)bus_info.name, sizeof(bus->name));
  return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::get_event_bus_info(bool is_output, uint32_t index, PluginEventBusInfo* bus) const {
  Steinberg::Vst::BusInfo bus_info;
  Steinberg::tresult result = component_->getBusInfo(Steinberg::Vst::MediaTypes::kEvent, is_output, index, bus_info);
  if (result == Steinberg::kInvalidArgument)
    return PluginResult::Failed;
  size_t retval;
  bus->id = index;
  std::wcstombs(bus->name, (const wchar_t*)bus_info.name, sizeof(bus->name));
  return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::activate_audio_bus(bool is_output, uint32_t index, bool state) {
  if (component_->activateBus(Steinberg::Vst::MediaTypes::kAudio, is_output, index, state) != Steinberg::kResultOk)
    return PluginResult::Failed;
  return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::activate_event_bus(bool is_output, uint32_t index, bool state) {
  if (component_->activateBus(Steinberg::Vst::MediaTypes::kEvent, is_output, index, state) != Steinberg::kResultOk)
    return PluginResult::Failed;
  return PluginResult::Ok;
}

PluginResult
VST3PluginWrapper::init_processing(PluginProcessingMode mode, uint32_t max_samples_per_block, double sample_rate) {
  int32_t process_mode = mode == PluginProcessingMode::Offline ? Steinberg::Vst::kOffline : Steinberg::Vst::kRealtime;
  Steinberg::Vst::ProcessSetup setup{
    .processMode = process_mode,
    .symbolicSampleSize = Steinberg::Vst::kSample32,
    .maxSamplesPerBlock = (int32_t)max_samples_per_block,
    .sampleRate = sample_rate,
  };

  if (VST3_FAILED(processor_->setupProcessing(setup)))
    return PluginResult::Failed;

  max_samples_per_block_ = max_samples_per_block;
  current_process_mode_ = process_mode;
  return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::start_processing() {
  if (VST3_FAILED(component_->setActive(true)))
    return PluginResult::Failed;
  VST3_WARN(processor_->setProcessing(true));
  return PluginResult::Ok;
}

PluginResult VST3PluginWrapper::stop_processing() {
  VST3_WARN(processor_->setProcessing(false));
  if (VST3_FAILED(component_->setActive(false)))
    return PluginResult::Failed;
  return PluginResult::Ok;
}

void VST3PluginWrapper::transfer_param(uint32_t param_id, double normalized_value) {
  int32_t index;
  Steinberg::Vst::ParameterValueQueue* queue =
      (Steinberg::Vst::ParameterValueQueue*)input_param_changes_.addParameterData(param_id, index);
  queue->addPoint(max_samples_per_block_ - 1, normalized_value, index);
}

PluginResult VST3PluginWrapper::process(PluginProcessInfo& process_info) {
  output_events_.clear();

  uint32_t input_buffer_count = math::min((uint32_t)input_bus_buffers_.size(), process_info.input_buffer_count);
  for (uint32_t i = 0; i < input_buffer_count; i++) {
    auto& vst_buffer = input_bus_buffers_[i];
    const auto& wb_buffer = process_info.input_buffer[i];
    vst_buffer.numChannels = wb_buffer.n_channels;
    vst_buffer.channelBuffers32 = wb_buffer.channel_buffers;
    vst_buffer.silenceFlags = 0;  //(1 << wb_buffer.n_channels) - 1;
  }

  for (uint32_t i = 0; i < process_info.output_buffer_count; i++) {
    auto& vst_buffer = output_bus_buffers_[i];
    const auto& wb_buffer = process_info.output_buffer[i];
    vst_buffer.numChannels = wb_buffer.n_channels;
    vst_buffer.channelBuffers32 = wb_buffer.channel_buffers;
    vst_buffer.silenceFlags = 0;  //(1 << wb_buffer.n_channels) - 1;
  }

  Steinberg::Vst::ProcessContext process_ctx{};

  if (process_info.playing)
    process_ctx.state |= Steinberg::Vst::ProcessContext::kPlaying;
  process_ctx.state |= Steinberg::Vst::ProcessContext::kTempoValid;
  process_ctx.state |= Steinberg::Vst::ProcessContext::kProjectTimeMusicValid;
  process_ctx.state |= Steinberg::Vst::ProcessContext::kTimeSigValid;
  process_ctx.sampleRate = process_info.sample_rate;
  process_ctx.tempo = process_info.tempo;
  process_ctx.projectTimeMusic = process_info.project_time_in_ppq;
  process_ctx.projectTimeSamples = process_info.project_time_in_samples;
  process_ctx.timeSigNumerator = 4;
  process_ctx.timeSigDenominator = 4;
  // process_ctx.samplesToNextClock

  input_events_.event_list = process_info.input_event_list;

  Steinberg::Vst::ProcessData process_data;
  process_data.processMode = current_process_mode_;
  process_data.symbolicSampleSize = sample_size_;
  process_data.numSamples = process_info.sample_count;
  process_data.numInputs = input_buffer_count;
  process_data.numOutputs = process_info.output_buffer_count;
  process_data.inputs = input_bus_buffers_.data();
  process_data.outputs = output_bus_buffers_.data();
  process_data.inputParameterChanges = &input_param_changes_;
  process_data.inputEvents = &input_events_;
  process_data.outputEvents = &output_events_;
  process_data.processContext = &process_ctx;
  VST3_WARN(processor_->process(process_data));

  input_param_changes_.clearQueue();

  return PluginResult::Ok;
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

PluginResult VST3PluginWrapper::attach_window(SDL_Window* window) {
  if (!has_view())
    return PluginResult::Unsupported;
  if (has_window_attached())
    return PluginResult::Failed;
  WindowNativeHandle handle = wm_get_native_window_handle(window);
#ifdef WB_PLATFORM_WINDOWS
  if (editor_view_->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) != Steinberg::kResultOk)
    return PluginResult::Unsupported;
  window_handle = window;
  /*if (editor_view_->setFrame(&plug_frame_) != Steinberg::kResultOk)
      return PluginResult::Failed;*/
  VST3_WARN(editor_view_->setFrame(this));
  if (editor_view_->attached(handle.window, Steinberg::kPlatformTypeHWND) != Steinberg::kResultOk) {
    VST3_WARN(editor_view_->setFrame(nullptr));
    window_handle = nullptr;
    return PluginResult::Failed;
  }
  return PluginResult::Ok;
#else
  return PluginResult::Unsupported;
#endif
  return PluginResult::Unsupported;
}

PluginResult VST3PluginWrapper::detach_window() {
  if (!has_view())
    return PluginResult::Unsupported;
  if (window_handle == nullptr)
    return PluginResult::Unsupported;
  VST3_WARN(editor_view_->removed());
  VST3_WARN(editor_view_->setFrame(nullptr));
  SDL_GetWindowPosition(window_handle, &last_window_x, &last_window_y);
  window_handle = nullptr;
  return PluginResult::Ok;
}

void VST3PluginWrapper::disconnect_components_() {
  if (component_icp_)
    component_icp_->disconnect(controller_icp_);
  if (controller_icp_)
    controller_icp_->disconnect(component_icp_);
  if (component_icp_)
    component_icp_->release();
  if (controller_icp_)
    controller_icp_->release();
}

Steinberg::tresult PLUGIN_API VST3PluginWrapper::beginEdit(Steinberg::Vst::ParamID id) {
  return vst3_result(handler->begin_edit(handler_userdata, this, id));
}

Steinberg::tresult PLUGIN_API
VST3PluginWrapper::performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue valueNormalized) {
  return vst3_result(handler->perform_edit(handler_userdata, this, id, valueNormalized));
}

Steinberg::tresult PLUGIN_API VST3PluginWrapper::endEdit(Steinberg::Vst::ParamID id) {
  return vst3_result(handler->end_edit(handler_userdata, this, id));
}

Steinberg::tresult PLUGIN_API VST3PluginWrapper::restartComponent(Steinberg::int32 flags) {
  // SMTG_DBPRT1("restartComponent called (%d)\n", flags);
  Log::debug("restartComponent called ({})", flags);
  return Steinberg::kNotImplemented;
}

Steinberg::tresult PLUGIN_API VST3PluginWrapper::resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* rect) {
  Log::debug("resizeView called ({:x})", (size_t)view);
  SDL_SetWindowSize(window_handle, rect->getWidth(), rect->getHeight());
  view->onSize(rect);
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API VST3PluginWrapper::queryInterface(const Steinberg::TUID _iid, void** obj) {
  if (Steinberg::FUnknownPrivate::iidEqual(_iid, IComponentHandler::iid) ||
      Steinberg::FUnknownPrivate::iidEqual(_iid, IPlugFrame::iid) ||
      Steinberg::FUnknownPrivate::iidEqual(_iid, FUnknown::iid)) {
    *obj = this;
    addRef();
    return Steinberg::kResultTrue;
  }
  return Steinberg::kNoInterface;
}

//

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

}  // namespace wb