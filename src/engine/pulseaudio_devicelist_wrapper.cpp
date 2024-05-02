#ifdef WB_PLATFORM_LINUX
#include "pulseaudio_devicelist_wrapper.h"
#include <iostream>
#include <vector>
#include <pulse/pulseaudio.h>

class PulseAudioDeviceList {
  private:
    std::vector<pa_devicelist> input_devices;
    std::vector<pa_devicelist> output_devices;
    int pa_ready;
    int state = 0;

  public:
    PulseAudioDeviceList() {
        pa_ready = 0;
        state = 0;
    }

    bool update_device_lists(pa_mainloop *pa_ml, pa_mainloop_api *pa_mlapi, pa_context *pa_ctx) {
        pa_context_set_state_callback(pa_ctx, pa_state_cb, this);

        pa_operation *pa_op = nullptr;

        for (;;) {
            if (pa_ready == 0) {
                pa_mainloop_iterate(pa_ml, 1, nullptr);
                continue;
            }
            if (pa_ready == 2) {
                pa_context_disconnect(pa_ctx);
                pa_context_unref(pa_ctx);
                pa_mainloop_free(pa_ml);
                return true;
            }

            switch (state) {
                case 0: {
                    pa_op = pa_context_get_sink_info_list(pa_ctx, pa_sinklist_cb, this);
                    state++;
                    break;
                }
                case 1: {
                    if (pa_op && pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
                        pa_operation_unref(pa_op);

                        pa_op = pa_context_get_source_info_list(pa_ctx, pa_sourcelist_cb, this);
                        state++;
                    }
                    break;
                }
                case 2: {
                    if (pa_op && pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
                        pa_operation_unref(pa_op);
                        pa_context_disconnect(pa_ctx);
                        pa_context_unref(pa_ctx);
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

    ~PulseAudioDeviceList() {
        input_devices.clear();
        output_devices.clear();
    }
};
#endif // WB_PLATFORM_LINUX