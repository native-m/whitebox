#ifndef WHITEBOX_PULSEAUDIO_DEVICELIST_WRAPPER_H
#define WHITEBOX_PULSEAUDIO_DEVICELIST_WRAPPER_H

#include "core/common.h"

#ifdef WB_PLATFORM_LINUX

#include <vector>
#include <string>
#include <optional>

#include <pulse/pulseaudio.h>

struct pa_device {
    std::string id;
    std::string name;
    pa_device_type type;
};

class PulseAudioDeviceList {
  private:
    std::vector<pa_device> input_devices;
    std::vector<pa_device> output_devices;
    int pa_ready;
    int state;
    pa_context *context;

    static void pa_state_cb(pa_context *c, void *userdata);
    static void pa_sinklist_cb(pa_context *c, const pa_sink_info *l, int eol, void *userdata);
    static void pa_sourcelist_cb(pa_context *c, const pa_source_info *l, int eol, void *userdata);

  public:
    PulseAudioDeviceList();
    void set_context(pa_context *pa_ctx);
    bool update_device_lists(pa_mainloop *pa_ml, pa_mainloop_api *pa_mlapi);
    std::vector<pa_device> get_sink_device_list();
    std::vector<pa_device> get_record_device_list();
    std::optional<pa_device> get_default_sink_device();
    std::optional<pa_device> get_default_record_device();
    std::optional<pa_device> get_sink_device_by_index(uint32_t index);
    std::optional<pa_device> get_record_device_by_index(uint32_t index);
    ~PulseAudioDeviceList();
};

#endif //WB_PLATFORM_LINUX
#endif //WHITEBOX_PULSEAUDIO_DEVICELIST_WRAPPER_H
