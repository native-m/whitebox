#pragma once

#include "clip.h"
#include "core/audio_buffer.h"
#include "core/common.h"
#include "core/thread.h"
#include <functional>

namespace wb {

struct Track;

struct Engine {
    using OnBpmChangeFn = std::function<void(double, double)>;

    std::vector<Track*> tracks;
    Spinlock editor_lock;
    Spinlock delete_lock;

    double ppq = 96.0;
    double playhead {};
    double playhead_start {};
    double sample_position {};
    std::atomic<double> beat_duration;
    std::atomic<double> playhead_ui;
    std::atomic_bool playing;
    std::atomic_bool playhead_updated;
    std::atomic_bool has_deleted_clips;

    AudioBuffer<float> mixing_buffer;
    std::vector<OnBpmChangeFn> on_bpm_change_listener;
    double phase = 0.0;

    ~Engine();

    void set_bpm(double bpm);
    void set_playhead_position(double beat_position);
    void set_buffer_size(uint32_t channels, uint32_t size);
    void clear_all();
    void play();
    void stop();

    void edit_lock() { editor_lock.lock(); }
    void edit_unlock() { editor_lock.unlock(); }

    Track* add_track(const std::string& name);
    void delete_track(uint32_t slot);
    void move_track(uint32_t from_slot, uint32_t to_slot);
    void solo_track(uint32_t slot);

    Clip* add_clip_from_file(Track* track, const std::filesystem::path& path, double min_time);
    void delete_clip(Track* track, Clip* clip);

    /*
        Process the whole thing.
        This runs on the audio thread.
    */
    void process(AudioBuffer<float>& output_buffer, double sample_rate);

    inline double playhead_pos() const { return playhead_ui.load(std::memory_order_relaxed); }
    
    inline double get_beat_duration() const {
        return beat_duration.load(std::memory_order_relaxed);
    }

    inline bool is_playing() const { return playing.load(std::memory_order_relaxed); }

    template <typename Fn>
    void add_on_bpm_change_listener(Fn&& fn) {
        on_bpm_change_listener.push_back(fn);
    }
};

extern Engine g_engine;

} // namespace wb