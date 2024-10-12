#pragma once

#include "clip.h"
#include "clip_edit.h"
#include "etypes.h"
#include "core/audio_buffer.h"
#include "core/common.h"
#include "core/thread.h"
#include <functional>
#include <vector>

namespace wb {

struct Track;

struct ProjectInfo {
    std::string author;
    std::string title;
    std::string genre;
    std::string description;
};

struct Engine {
    using OnBpmChangeFn = std::function<void(double, double)>;

    ProjectInfo project_info;
    std::vector<Track*> tracks;
    mutable Spinlock editor_lock;
    mutable Spinlock delete_lock;

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

    TrackEditResult add_clip_from_file(Track* track, const std::filesystem::path& path, double min_time);
    TrackEditResult add_audio_clip(Track* track, const std::string& name, double min_time, double max_time,
                        double start_offset, const AudioClip& clip_info, bool active = true);
    TrackEditResult add_midi_clip(Track* track, const std::string& name, double min_time, double max_time,
                       double start_offset, const MidiClip& clip_info, bool active = true);
    TrackEditResult emplace_clip(Track* track, const Clip& new_clip);
    TrackEditResult duplicate_clip(Track* track, Clip* clip_to_duplicate, double min_time, double max_time);
    TrackEditResult move_clip(Track* track, Clip* clip, double relative_pos);
    TrackEditResult resize_clip(Track* track, Clip* clip, double relative_pos, double min_length,
                     bool right_side);
    void delete_clip(Track* track, Clip* clip);
    TrackEditResult add_to_cliplist(Track* track, Clip* clip);
    std::optional<ClipQueryResult> query_clip_by_range(Track* track, double min, double max) const;

    TrackEditResult reserve_track_region(Track* track, uint32_t first_clip, uint32_t last_clip, double min, double max,
                                        bool dont_sort, Clip* ignore_clip);

    double get_song_length() const;

    /*
        Process the whole thing.
        This runs on the audio thread.
    */
    void process(AudioBuffer<float>& output_buffer, double sample_rate);

    inline double playhead_pos() const { return playhead_ui.load(std::memory_order_relaxed); }

    inline double get_beat_duration() const {
        return beat_duration.load(std::memory_order_relaxed);
    }

    inline double get_bpm() const { return 60.0 / beat_duration.load(std::memory_order_relaxed); }

    inline bool is_playing() const { return playing.load(std::memory_order_relaxed); }

    template <typename Fn>
    void add_on_bpm_change_listener(Fn&& fn) {
        on_bpm_change_listener.push_back(fn);
    }

    void update_clip_ordering(Track* track);
};

extern Engine g_engine;

} // namespace wb