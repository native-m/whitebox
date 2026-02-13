#include "midi_io.h"

#ifdef WB_PLATFORM_LINUX

#include <alsa/asoundlib.h>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <cstring>

#include "core/debug.h"

namespace wb {

class MidiIOAlsa : public MidiIO {
public:
    snd_seq_t* seq_handle = nullptr;
    int port_id = -1;
    std::thread poll_thread;
    std::atomic<bool> running{false};
    MidiCallback input_callback;

    std::mutex callback_mutex;
    std::mutex connection_mutex;

    std::vector<MidiDeviceInfo> cached_devices;
    bool connected = false;
    uint32_t connected_device_id = 0;

    MidiIOAlsa() = default;

    ~MidiIOAlsa() override {
        disconnect();
        if (seq_handle) {
            snd_seq_close(seq_handle);
            seq_handle = nullptr;
        }
    }

    bool init() override {
        int err = snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0);
        if (err < 0) {
            Log::error("Failed to open ALSA sequencer: {}", snd_strerror(err));
            return false;
        }

        snd_seq_set_client_name(seq_handle, "Whitebox");

        port_id = snd_seq_create_simple_port(seq_handle, "MIDI In",
            SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_MIDI_GENERIC);

        if (port_id < 0) {
            Log::error("Failed to create ALSA MIDI port: {}", snd_strerror(port_id));
            snd_seq_close(seq_handle);
            seq_handle = nullptr;
            return false;
        }

        Log::info("ALSA MIDI initialized: client={} port={}", snd_seq_client_id(seq_handle), port_id);
        rescan();
        return true;
    }

    bool rescan() override {
        if (!seq_handle) return false;

        std::lock_guard<std::mutex> lock(connection_mutex);
        cached_devices.clear();

        snd_seq_client_info_t* cinfo;
        snd_seq_port_info_t* pinfo;

        snd_seq_client_info_alloca(&cinfo);
        snd_seq_port_info_alloca(&pinfo);

        snd_seq_client_info_set_client(cinfo, -1);

        while (snd_seq_query_next_client(seq_handle, cinfo) >= 0) {
            int client = snd_seq_client_info_get_client(cinfo);

            if (client == snd_seq_client_id(seq_handle)) continue;

            snd_seq_port_info_set_client(pinfo, client);
            snd_seq_port_info_set_port(pinfo, -1);

            while (snd_seq_query_next_port(seq_handle, pinfo) >= 0) {
                unsigned int cap = snd_seq_port_info_get_capability(pinfo);

                bool can_read = (cap & SND_SEQ_PORT_CAP_READ) && (cap & SND_SEQ_PORT_CAP_SUBS_READ);
                bool can_write = (cap & SND_SEQ_PORT_CAP_WRITE) && (cap & SND_SEQ_PORT_CAP_SUBS_WRITE);

                if (!can_read && !can_write) continue;

                int port = snd_seq_port_info_get_port(pinfo);
                const char* name = snd_seq_port_info_get_name(pinfo);
                const char* client_name = snd_seq_client_info_get_name(cinfo);

                MidiDeviceInfo info;
                info.id = (client << 8) | port;
                info.name = std::string(client_name) + ": " + name;
                info.is_input = can_read;
                info.is_output = can_write;

                cached_devices.push_back(info);
            }
        }

        return true;
    }

    std::vector<MidiDeviceInfo> get_devices() override {
        std::lock_guard<std::mutex> lock(connection_mutex);
        return cached_devices;
    }

    bool connect(uint32_t device_id) override {
        std::lock_guard<std::mutex> lock(connection_mutex);

        if (!seq_handle) return false;
        if (connected) {
            disconnect_internal();
        }

        int src_client = (device_id >> 8) & 0xFF;
        int src_port = device_id & 0xFF;

        int err = snd_seq_connect_from(seq_handle, port_id, src_client, src_port);
        if (err < 0) {
            Log::error("Failed to connect MIDI: {}", snd_strerror(err));
            return false;
        }

        connected = true;
        connected_device_id = device_id;

        running = true;
        poll_thread = std::thread([this]() {
            poll_midi_events();
        });

        Log::info("Connected to MIDI device {}:{}", src_client, src_port);
        return true;
    }

    void disconnect() override {
        std::lock_guard<std::mutex> lock(connection_mutex);
        disconnect_internal();
    }

    bool is_connected() const override {
        return connected;
    }

    uint32_t get_connected_device_id() const override {
        return connected_device_id;
    }

    void set_input_callback(MidiCallback callback) override {
        std::lock_guard<std::mutex> lock(callback_mutex);
        input_callback = std::move(callback);
    }

    void process(uint32_t n_frames) override {
    }

    void send_event(const MidiEventRaw& event) override {
    }

private:
    void disconnect_internal() {
        if (!connected) return;

        running = false;
        if (poll_thread.joinable()) {
            poll_thread.join();
        }

        if (seq_handle && connected_device_id != 0) {
            int src_client = (connected_device_id >> 8) & 0xFF;
            int src_port = connected_device_id & 0xFF;
            snd_seq_disconnect_from(seq_handle, port_id, src_client, src_port);
        }

        connected = false;
        connected_device_id = 0;
    }

    void poll_midi_events() {
        int npfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
        std::vector<pollfd> pfds(npfds);
        snd_seq_poll_descriptors(seq_handle, pfds.data(), npfds, POLLIN);

        while (running) {
            if (poll(pfds.data(), npfds, 100) > 0) {
                snd_seq_event_t* ev;
                while (snd_seq_event_input(seq_handle, &ev) >= 0) {
                    if (!ev) continue;

                    MidiEventRaw raw_event{};
                    raw_event.time = 0;

                    switch (ev->type) {
                        case SND_SEQ_EVENT_NOTEON:
                            raw_event.size = 3;
                            raw_event.bytes[0] = 0x90 | (ev->data.note.channel & 0x0F);
                            raw_event.bytes[1] = ev->data.note.note;
                            raw_event.bytes[2] = ev->data.note.velocity;
                            break;
                        case SND_SEQ_EVENT_NOTEOFF:
                            raw_event.size = 3;
                            raw_event.bytes[0] = 0x80 | (ev->data.note.channel & 0x0F);
                            raw_event.bytes[1] = ev->data.note.note;
                            raw_event.bytes[2] = ev->data.note.velocity;
                            break;
                        case SND_SEQ_EVENT_KEYPRESS:
                            raw_event.size = 3;
                            raw_event.bytes[0] = 0xA0 | (ev->data.note.channel & 0x0F);
                            raw_event.bytes[1] = ev->data.note.note;
                            raw_event.bytes[2] = ev->data.note.velocity;
                            break;
                        case SND_SEQ_EVENT_CONTROLLER:
                            raw_event.size = 3;
                            raw_event.bytes[0] = 0xB0 | (ev->data.control.channel & 0x0F);
                            raw_event.bytes[1] = ev->data.control.param;
                            raw_event.bytes[2] = ev->data.control.value;
                            break;
                        case SND_SEQ_EVENT_PGMCHANGE:
                            raw_event.size = 2;
                            raw_event.bytes[0] = 0xC0 | (ev->data.control.channel & 0x0F);
                            raw_event.bytes[1] = ev->data.control.value;
                            break;
                        case SND_SEQ_EVENT_CHANPRESS:
                            raw_event.size = 2;
                            raw_event.bytes[0] = 0xD0 | (ev->data.control.channel & 0x0F);
                            raw_event.bytes[1] = ev->data.control.value;
                            break;
                        case SND_SEQ_EVENT_PITCHBEND: {
                            int value = ev->data.control.value + 8192;
                            raw_event.size = 3;
                            raw_event.bytes[0] = 0xE0 | (ev->data.control.channel & 0x0F);
                            raw_event.bytes[1] = value & 0x7F;
                            raw_event.bytes[2] = (value >> 7) & 0x7F;
                            break;
                        }
                        case SND_SEQ_EVENT_SYSEX: {
                            if (ev->data.ext.len > 0 && ev->data.ext.len <= 3) {
                                raw_event.size = ev->data.ext.len;
                                std::memcpy(raw_event.bytes, ev->data.ext.ptr, ev->data.ext.len);
                            } else {
                                snd_seq_free_event(ev);
                                continue;
                            }
                            break;
                        }
                        default:
                            snd_seq_free_event(ev);
                            continue;
                    }

                    if (raw_event.size > 0) {
                        std::lock_guard<std::mutex> lock(callback_mutex);
                        if (input_callback) {
                            input_callback(raw_event);
                        }
                    }

                    snd_seq_free_event(ev);
                }
            }
        }
    }
};

MidiIO* create_midi_io_alsa() {
    return new MidiIOAlsa();
}

} // namespace wb

#endif