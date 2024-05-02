#ifdef WB_PLATFORM_LINUX
#include <string>

#ifndef WHITEBOX_PULSEAUDIO_DEVICELIST_WRAPPER_H
#define WHITEBOX_PULSEAUDIO_DEVICELIST_WRAPPER_H

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
    int state = 0;

    static void pa_state_cb(pa_context *c, void *userdata) {
        PulseAudioDeviceList *instance = static_cast<PulseAudioDeviceList *>(userdata);
        pa_context_state_t state = pa_context_get_state(c);
        if (state == PA_CONTEXT_READY) {
            instance->pa_ready = 1;
        } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
            instance->pa_ready = 2;
        }
    }

    static void pa_sinklist_cb(pa_context *c, const pa_sink_info *l, int eol, void *userdata) {
        PulseAudioDeviceList *instance = static_cast<PulseAudioDeviceList *>(userdata);

        if (eol > 0) {
            return;
        }

        pa_device device;
        device.id = l->name ? l->name : "";
        device.name = l->description ? l->description : "";

        instance->output_devices.push_back(device);
    }

    static void pa_sourcelist_cb(pa_context *c, const pa_source_info *l, int eol, void *userdata) {
        PulseAudioDeviceList *instance = static_cast<PulseAudioDeviceList *>(userdata);

        if (eol > 0) {
            return;
        }

        pa_device device;
        device.id = l->name ? l->name : "";
        device.name = l->description ? l->description : "";

        instance->input_devices.push_back(device);
    }

  public:
    PulseAudioDeviceList() {
        pa_ready = 0;
        state = 0;
    }

    virtual bool update_device_lists(pa_mainloop *pa_ml, pa_mainloop_api *pa_mlapi, pa_context *pa_ctx) {}

    std::vector<pa_device> get_sink_device_list() {
        return output_devices;
    }

    std::vector<pa_device> get_record_device_list() {
        return input_devices;
    }

    pa_device get_default_sink_device() {
        return output_devices[0];
    }

    pa_device get_default_record_device() {
        return input_devices[0];
    }

    pa_device get_sink_device_by_index(uint32_t index) {
        return output_devices[index];
    }

    pa_device get_record_device_by_index(uint32_t index) {
        return input_devices[index];
    }

    ~PulseAudioDeviceList() {}
};

#endif // WHITEBOX_PULSEAUDIO_DEVICELIST_WRAPPER_H
#endif // WB_PLATFORM_LINUX