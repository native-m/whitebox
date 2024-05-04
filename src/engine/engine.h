#pragma once

#include "clip.h"
#include "core/audio_buffer.h"
#include "core/common.h"
#include "core/thread.h"
#include "track.h"
#include <functional>
#include <string>
#include <vector>

namespace wb {

struct Engine {
    using OnBpmChangeFn = std::function<void(double, double)>;

    std::vector<Track*> tracks;
    Spinlock editor_lock;
    
    std::atomic<double> beat_duration;
    double ppq = 96.0;
    double playhead {};
    double playhead_start {};
    std::atomic<double> playhead_ui;
    std::atomic_bool playing;
    std::atomic_bool playhead_updated;

    std::vector<OnBpmChangeFn> on_bpm_change_listener;
    double phase = 0.0;

    ~Engine();

    void set_bpm(double bpm);
    void set_playhead_position(double beat_position);
    void play();
    void stop();
    bool is_playing() const { return playing.load(std::memory_order_relaxed); }

    void edit_lock() { editor_lock.lock(); }
    void edit_unlock() { editor_lock.unlock(); }

    Track* add_track(const std::string& name);
    void delete_track(uint32_t slot);
    void move_track(uint32_t from_slot, uint32_t to_slot);

    Clip* add_audio_clip_from_file(Track* track, const std::filesystem::path& path,
                                   double min_time);

    
    /*
        Process the whole thing.
        This runs in the audio thread.
    */
    void process(AudioBuffer<float>& output_buffer, double sample_rate);

    template <typename Fn>
    void add_on_bpm_change_listener(Fn&& fn) {
        on_bpm_change_listener.push_back(fn);
    }

};

extern Engine g_engine;

} // namespace wb