#include "pulseaudio_devicelist_wrapper.h"
#ifdef WB_PLATFORM_LINUX

#include <vector>
#include <pulse/pulseaudio.h>

void PulseAudioDeviceList::pa_state_cb(pa_context *c, void *userdata) {
    PulseAudioDeviceList *instance = static_cast<PulseAudioDeviceList *>(userdata);
    pa_context_state_t state = pa_context_get_state(c);
    if (state == PA_CONTEXT_READY) {
        instance->pa_ready = 1;
    } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        instance->pa_ready = 2;
    }
}

void PulseAudioDeviceList::pa_sinklist_cb(pa_context *c, const pa_sink_info *l, int eol, void *userdata) {
    PulseAudioDeviceList *instance = static_cast<PulseAudioDeviceList *>(userdata);

    if (eol > 0) {
        return;
    }

    pa_device device;
    device.id = l->name ? l->name : "";
    device.name = l->description ? l->description : "";

    instance->output_devices.push_back(device);
}

void PulseAudioDeviceList::pa_sourcelist_cb(pa_context *c, const pa_source_info *l, int eol, void *userdata) {
    PulseAudioDeviceList *instance = static_cast<PulseAudioDeviceList *>(userdata);

    if (eol > 0) {
        return;
    }

    pa_device device;
    device.id = l->name ? l->name : "";
    device.name = l->description ? l->description : "";

    instance->input_devices.push_back(device);
}

PulseAudioDeviceList::PulseAudioDeviceList() {
    pa_ready = 0;
    state = 0;
    context = nullptr;
}

bool PulseAudioDeviceList::update_device_lists(pa_mainloop *pa_ml, pa_mainloop_api *pa_mlapi) {
    pa_context_set_state_callback(context, pa_state_cb, this);

    pa_operation *pa_op = nullptr;

    for (;;) {
        if (pa_ready == 0) {
            pa_mainloop_iterate(pa_ml, 1, nullptr);
            continue;
        }
        if (pa_ready == 2) {
            pa_context_disconnect(context);
            pa_context_unref(context);
            pa_mainloop_free(pa_ml);
            return false;
        }

        switch (state) {
            case 0: {
                pa_op = pa_context_get_sink_info_list(context, pa_sinklist_cb, this);
                state++;
                break;
            }
            case 1: {
                if (pa_op && pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
                    pa_operation_unref(pa_op);

                    pa_op = pa_context_get_source_info_list(context, pa_sourcelist_cb, this);
                    state++;
                }
                break;
            }
            case 2: {
                if (pa_op && pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
                    pa_operation_unref(pa_op);
                    pa_context_disconnect(context);
                    pa_context_unref(context);
                    pa_mainloop_free(pa_ml);
                    return true;
                }
                break;
            }
            default:
                return false;
        }

        pa_mainloop_iterate(pa_ml, 1, nullptr);
    }
}

void PulseAudioDeviceList::set_context(pa_context* pa_ctx) {
    context = pa_ctx;
}

std::vector<pa_device> PulseAudioDeviceList::get_sink_device_list() {
    return output_devices;
}

std::vector<pa_device> PulseAudioDeviceList::get_record_device_list() {
    return input_devices;
}

std::optional<pa_device> PulseAudioDeviceList::get_default_sink_device() {
    if (output_devices.empty()) {
        return std::nullopt;
    }

    return output_devices[0];
}

std::optional<pa_device> PulseAudioDeviceList::get_default_record_device() {
    if (input_devices.empty()) {
        return std::nullopt;
    }

    return input_devices[0];
}

std::optional<pa_device> PulseAudioDeviceList::get_sink_device_by_index(uint32_t index) {
    if (index < output_devices.size()) {
        return output_devices[index];
    }

    return std::nullopt;
}

std::optional<pa_device> PulseAudioDeviceList::get_record_device_by_index(uint32_t index) {
    if (index < input_devices.size()) {
        return input_devices[index];
    }

    return std::nullopt;
}

PulseAudioDeviceList::~PulseAudioDeviceList() {
    input_devices.clear();
    output_devices.clear();
}
#endif // WB_PLATFORM_LINUX