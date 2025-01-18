#include "engine.h"
#include "audio_io.h"
#include "clip_edit.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "track.h"
#include <fmt/chrono.h>
#include <numbers>

namespace wb {

Engine::~Engine() {
}

void Engine::set_bpm(double bpm) {
    double new_beat_duration = 60.0 / bpm;
    beat_duration.store(new_beat_duration, std::memory_order_release);
    for (auto& listener : on_bpm_change_listener) {
        listener(new_beat_duration, bpm);
    }
}

void Engine::set_playhead_position(double beat_position) {
    // TODO: Allow playhead dragging.
    // assert(!playing && "Dragging playhead while playing is not allowed yet!");
    editor_lock.lock();
    playhead_start = beat_position;
    playhead = playhead_start;
    playhead_ui = playhead_start;
    playhead_updated.store(true, std::memory_order_release);
    editor_lock.unlock();
}

void Engine::set_audio_channel_config(uint32_t input_channels, uint32_t output_channels, uint32_t buffer_size,
                                      uint32_t sample_rate) {
    num_input_channels = input_channels;
    num_output_channels = output_channels;
    audio_buffer_size = buffer_size;
    audio_sample_rate = sample_rate;
    audio_buffer_duration_ms = period_to_ms(buffer_size_to_period(buffer_size, sample_rate));
    mixing_buffer.resize(buffer_size);
    mixing_buffer.resize_channel(output_channels);
    for (auto track : tracks)
        track->prepare_effect_buffer(num_output_channels, buffer_size);
}

void Engine::clear_all() {
    track_input_groups.clear();
    for (auto track : tracks) {
        delete_plugin_from_track(track);
        delete track;
    }
    tracks.clear();
}

void Engine::play() {
    Log::debug("-------------- Playing --------------");
    editor_lock.lock();
    for (auto track : tracks) {
        if (recording)
            track->prepare_record(playhead_start);
        track->reset_playback_state(playhead_start, false);
    }
    playhead_updated.store(false, std::memory_order_release);
    sample_position = 0;
    playing = true;
    editor_lock.unlock();
}

void Engine::stop() {
    if (recording)
        stop_record();
    editor_lock.lock();
    playing = false;
    playhead = playhead_start;
    playhead_ui = playhead_start;
    for (auto track : tracks)
        track->stop();
    editor_lock.unlock();
    Log::debug("-------------- Stop --------------");
}

void Engine::record() {
    if (recording && playing)
        return;
    if (track_input_groups.size() != 0) {
        recorder_queue.start(AudioFormat::F32, audio_record_buffer_size / 4, track_input_groups);
        recorder_thread = std::thread(recorder_thread_runner_, this);
    }
    recording = true;
    play();
    Log::debug("-------------- Record --------------");
}

void Engine::stop_record() {
    if (!recording)
        return;
    recording = false;
    if (track_input_groups.size() != 0) {
        recorder_queue.stop();
        recorder_thread.join();
    }
    for (auto track : tracks) {
        if (track->input_attr.recording) {
            std::string name;
            auto current_datetime = std::chrono::system_clock::now();
            // Set sample name
            fmt::format_to(std::back_inserter(name), "{} - {}", current_datetime, track->name);
            std::replace(name.begin(), name.end(), ':', '_'); // Path does not support colon
            track->recorded_samples->name = std::move(name);
            track->recorded_samples->path = track->recorded_samples->name;
            // Adjust the sample count to the actual number of samples written
            track->recorded_samples->resize(track->num_samples_written, track->recorded_samples->channels);
            track->num_samples_written = 0; // Reset back to zero
            // Transform the recorded sample into asset and create the audio clip
            SampleAsset* asset = g_sample_table.create_from_existing_sample(std::move(*track->recorded_samples));
            add_audio_clip(track, asset->sample_instance.name, track->record_min_time, track->record_max_time, 0.0,
                           AudioClip {.asset = asset});
            track->recorded_samples.reset();
        }
        track->stop_record();
    }
}

void Engine::arm_track_recording(uint32_t slot, bool armed) {
    Track* track = tracks[slot];
    set_track_input(slot, track->input.type, track->input.index, armed);
}

void Engine::set_track_input(uint32_t slot, TrackInputType type, uint32_t index, bool armed) {
    assert(slot < tracks.size());
    Track* track = tracks[slot];
    uint32_t new_input = TrackInput {type, index}.as_packed_u32();
    uint32_t old_input = track->input.as_packed_u32();
    auto new_pred = [new_input](const TrackInputGroup& x) { return x.input == new_input; };
    auto old_pred = [old_input](const TrackInputGroup& x) { return x.input == old_input; };
    track->input_attr.armed = armed;

    if (armed && (track->input.type != type || track->input.index != index)) {
        // Remove previous input assignment
        auto input_map = std::find_if(track_input_groups.begin(), track_input_groups.end(), old_pred);
        if (input_map != track_input_groups.end() && input_map->input_attrs == &track->input_attr) {
            input_map->input_attrs = track->input_attr.next();
            if (input_map->input_attrs == nullptr)
                track_input_groups.erase(input_map);
        }
        track->input_attr.remove_from_list();
        // Assign new input
        if (type != TrackInputType::None) {
            input_map = std::find_if(track_input_groups.begin(), track_input_groups.end(), new_pred);
            if (input_map == track_input_groups.end()) {
                track_input_groups.emplace_back(new_input, &track->input_attr);
            } else {
                input_map->input_attrs->push_item_front(&track->input_attr);
                input_map->input_attrs = &track->input_attr;
            }
        }
    } else {
        auto input_map = std::find_if(track_input_groups.begin(), track_input_groups.end(), new_pred);
        if (armed && type != TrackInputType::None) {
            // Assign new input
            if (input_map == track_input_groups.end()) {
                track_input_groups.emplace_back(new_input, &track->input_attr);
            } else if (track->input.type != type || track->input.index != index) {
                input_map->input_attrs->push_item_front(&track->input_attr);
                input_map->input_attrs = &track->input_attr;
            }
        } else {
            // Remove input assignment if not armed
            if (input_map != track_input_groups.end() && input_map->input_attrs == &track->input_attr) {
                input_map->input_attrs = track->input_attr.next();
                if (input_map->input_attrs == nullptr)
                    track_input_groups.erase(input_map);
            }
            track->input_attr.remove_from_list();
        }
    }

    track->input.type = type;
    track->input.index = index;
}

Track* Engine::add_track(const std::string& name) {
    Track* new_track = new Track();
    new_track->name = name;
    tracks.push_back(new_track);
    new_track->prepare_effect_buffer(num_output_channels, audio_buffer_size);
    return new_track;
}

void Engine::delete_track(uint32_t slot) {
    editor_lock.lock();
    delete_lock.lock();
    Track* track = tracks[slot];
    if (track->input.type != TrackInputType::None)
        set_track_input(slot, TrackInputType::None, 0, false);
    tracks.erase(tracks.begin() + slot);
    delete track;
    delete_lock.unlock();
    editor_lock.unlock();
}

void Engine::move_track(uint32_t from_slot, uint32_t to_slot) {
    if (from_slot == to_slot)
        return;

    Track* tmp = tracks[from_slot];

    if (from_slot < to_slot) {
        for (uint32_t i = from_slot; i < to_slot; i++)
            tracks[i] = tracks[i + 1];
    } else {
        for (uint32_t i = from_slot; i > to_slot; i--)
            tracks[i + 1] = tracks[i];
    }

    tracks[to_slot] = tmp;
}

void Engine::solo_track(uint32_t slot) {
    bool mute = false;
    if (tracks[slot]->ui_parameter_state.solo) {
        tracks[slot]->ui_parameter_state.solo = false;
    } else {
        tracks[slot]->ui_parameter_state.solo = true;
        tracks[slot]->set_mute(false);
        mute = true;
    }

    for (uint32_t i = 0; i < tracks.size(); i++) {
        if (i == slot)
            continue;
        if (tracks[i]->ui_parameter_state.solo)
            tracks[i]->ui_parameter_state.solo = false;
        tracks[i]->set_mute(mute);
    }
}

TrackEditResult Engine::add_clip_from_file(Track* track, const std::filesystem::path& path, double min_time) {
    bool is_midi = false;
    Clip* clip = nullptr;

    if (SampleAsset* sample_asset = g_sample_table.load_from_file(path)) {
        double sample_rate = (double)sample_asset->sample_instance.sample_rate;
        double clip_length = samples_to_beat(sample_asset->sample_instance.count, sample_rate, beat_duration);
        double max_time = min_time + math::uround(clip_length * ppq) / ppq;
        return add_audio_clip(track, path.filename().string(), min_time, max_time, 0.0, {.asset = sample_asset});
    }

    if (MidiAsset* midi_asset = g_midi_table.load_from_file(path)) {
        return add_midi_clip(track, "", min_time, min_time + midi_asset->data.max_length, 0.0, {.asset = midi_asset});
    }

    return {};
}

TrackEditResult Engine::add_audio_clip(Track* track, const std::string& name, double min_time, double max_time,
                                       double start_offset, const AudioClip& clip_info, bool active) {
    std::unique_lock lock(editor_lock);
    Clip* clip = track->allocate_clip();
    assert(clip && "Cannot allocate clip");
    new (clip) Clip(name, track->color, min_time, max_time);
    clip->init_as_audio_clip(clip_info);
    clip->start_offset = start_offset;
    clip->set_active(active);
    return add_to_cliplist(track, clip);
}

TrackEditResult Engine::add_midi_clip(Track* track, const std::string& name, double min_time, double max_time,
                                      double start_offset, const MidiClip& clip_info, bool active) {
    std::unique_lock lock(editor_lock);
    Clip* clip = track->allocate_clip();
    assert(clip && "Cannot allocate clip");
    new (clip) Clip(name, track->color, min_time, max_time);
    clip->init_as_midi_clip(clip_info);
    clip->start_offset = start_offset;
    clip->set_active(active);
    return add_to_cliplist(track, clip);
}

TrackEditResult Engine::emplace_clip(Track* track, const Clip& new_clip) {
    std::unique_lock lock(editor_lock);
    Clip* clip = track->allocate_clip();
    new (clip) Clip(new_clip);
    return add_to_cliplist(track, clip);
}

TrackEditResult Engine::duplicate_clip(Track* track, Clip* clip_to_duplicate, double min_time, double max_time) {
    std::unique_lock lock(editor_lock);
    Clip* clip = track->allocate_clip();
    assert(clip && "Cannot allocate clip");
    new (clip) Clip(*clip_to_duplicate);
    clip->min_time = min_time;
    clip->max_time = max_time;
    return add_to_cliplist(track, clip);
}

TrackEditResult Engine::move_clip(Track* track, Clip* clip, double relative_pos) {
    if (relative_pos == 0.0)
        return {};
    std::unique_lock lock(editor_lock);
    auto [min_time, max_time] = calc_move_clip(clip, relative_pos);
    auto query_result = track->query_clip_by_range(min_time, max_time);
    TrackEditResult trim_result = query_result ? reserve_track_region(track, query_result->first, query_result->last,
                                                                      min_time, max_time, true, clip)
                                               : TrackEditResult {};
    trim_result.deleted_clips.push_back(*clip); // Save previous state as deleted
    clip->min_time = min_time;
    clip->max_time = max_time;
    clip->start_offset_changed = true;
    track->update_clip_ordering();
    track->reset_playback_state(playhead, true);
    trim_result.added_clips.push_back(clip);
    return trim_result;
}

TrackEditResult Engine::resize_clip(Track* track, Clip* clip, double relative_pos, double min_length, bool right_side,
                                    bool shift) {
    if (relative_pos == 0.0)
        return {};
    std::unique_lock lock(editor_lock);
    auto [min_time, max_time, start_offset] =
        calc_resize_clip(clip, relative_pos, min_length, beat_duration, right_side, shift);
    auto query_result = track->query_clip_by_range(min_time, max_time);
    TrackEditResult trim_result = query_result ? reserve_track_region(track, query_result->first, query_result->last,
                                                                      min_time, max_time, true, clip)
                                               : TrackEditResult {};
    trim_result.deleted_clips.push_back(*clip);
    if (right_side) {
        clip->min_time = min_time;
        clip->start_offset = start_offset;
    } else {
        clip->max_time = max_time;
        clip->start_offset = start_offset;
    }
    track->update_clip_ordering();
    track->reset_playback_state(playhead, true);
    trim_result.added_clips.push_back(clip);
    return trim_result;
}

TrackEditResult Engine::delete_clip(Track* track, Clip* clip) {
    TrackEditResult result;
    result.deleted_clips.push_back(*clip);
    track->delete_clip(clip);
    track->update_clip_ordering();
    track->reset_playback_state(playhead, true);
    return result;
}

TrackEditResult Engine::add_to_cliplist(Track* track, Clip* clip) {
    auto& clips = track->clips;
    if (clips.size() == 0) {
        TrackEditResult trim_result;
        trim_result.added_clips.push_back(clip);
        clip->id = 0;
        clips.push_back(clip);
        track->reset_playback_state(playhead, true);
        return trim_result;
    }

    // Add to the back
    if (auto last_clip = clips.back(); last_clip->max_time < clip->min_time) {
        TrackEditResult trim_result;
        trim_result.added_clips.push_back(clip);
        clip->id = last_clip->id + 1;
        clips.push_back(clip);
        track->reset_playback_state(playhead, true);
        return trim_result;
    }
    // Add to the front
    if (auto first_clip = clips.front(); first_clip->min_time > clip->max_time) {
        TrackEditResult trim_result;
        trim_result.added_clips.push_back(clip);
        clips.push_front(clip);
        for (uint32_t i = 0; i < (uint32_t)clips.size(); i++)
            clips[i]->id = i;
        track->reset_playback_state(playhead, true);
        return trim_result;
    }

    auto result = track->query_clip_by_range(clip->min_time, clip->max_time);

    // No clip found
    if (!result) {
        TrackEditResult trim_result;
        trim_result.added_clips.push_back(clip);
        clips.push_back(clip);
        track->update_clip_ordering();
        track->reset_playback_state(playhead, true);
        return trim_result;
    }

    // Trim to reserve space for the clip
    TrackEditResult trim_result =
        reserve_track_region(track, result->first, result->last, clip->min_time, clip->max_time, true, nullptr);
    trim_result.added_clips.push_back(clip);
    clips.push_back(clip);
    track->update_clip_ordering();
    track->reset_playback_state(playhead, true);

    return trim_result;
}

TrackEditResult Engine::delete_region(Track* track, double min, double max) {
    auto query_result = track->query_clip_by_range(min, max);
    if (!query_result) {
        return {};
    }
    TrackEditResult result =
        g_engine.reserve_track_region(track, query_result->first, query_result->last, min, max, false, nullptr);
    track->update_clip_ordering();
    track->reset_playback_state(g_engine.playhead, true);
    return result;
}

std::optional<ClipQueryResult> Engine::query_clip_by_range(Track* track, double min, double max) const {
    return track->query_clip_by_range(min, max);
}

TrackEditResult Engine::reserve_track_region(Track* track, uint32_t first_clip, uint32_t last_clip, double min,
                                             double max, bool dont_sort, Clip* ignore_clip) {
    Vector<Clip*>& clips = track->clips;
    if (clips.size() == 0)
        return {};

    double current_beat_duration = beat_duration.load(std::memory_order_relaxed);
    Vector<Clip> deleted_clips;
    Vector<Clip*> added_clips;
    Vector<Clip*> modified_clips;

    if (first_clip == last_clip) {
        Clip* clip = clips[first_clip];
        if (clip == ignore_clip) {
            return {};
        }
        deleted_clips.push_back(*clip);
        if (min > clip->min_time && max < clip->max_time) {
            // Split clip into two parts
            Clip* new_clip = (Clip*)track->clip_allocator.allocate();
            if (!new_clip) {
                Log::error("Cannot allocate new clip");
                return {};
            }
            new (new_clip) Clip(*clip);
            new_clip->min_time = max;
            new_clip->start_offset = shift_clip_content(new_clip, clip->min_time - max, current_beat_duration);
            modified_clips.push_back(new_clip);
            clip->max_time = min;
            bool locked = editor_lock.try_lock();
            clips.push_back(new_clip);
            if (locked) {
                editor_lock.unlock();
            }
        } else if (min > clip->min_time) {
            clip->max_time = min;
        } else if (max < clip->max_time) {
            clip->start_offset = shift_clip_content(clip, clip->min_time - max, current_beat_duration);
            clip->min_time = max;
        } else {
            track->delete_clip(clip);
            return {
                .deleted_clips = std::move(deleted_clips),
            };
        }
        modified_clips.push_back(clip);
        return {
            .deleted_clips = std::move(deleted_clips),
            .added_clips = std::move(added_clips),
            .modified_clips = std::move(modified_clips),
        };
    }

    Clip* first = clips[first_clip];
    Clip* last = clips[last_clip];

    if (first != ignore_clip && min > first->min_time) {
        deleted_clips.push_back(*first);
        modified_clips.push_back(first);
        first->max_time = min;
        first_clip++;
    }

    if (last != ignore_clip && max < last->max_time) {
        deleted_clips.push_back(*last);
        modified_clips.push_back(last);
        last->start_offset = shift_clip_content(last, last->min_time - max, current_beat_duration);
        last->min_time = max;
        last_clip--;
    }

    if (first_clip <= last_clip) {
        deleted_clips.reserve((last_clip - first_clip) + 1);
        for (uint32_t i = first_clip; i <= last_clip; i++) {
            if (clips[i] != ignore_clip) {
                deleted_clips.push_back(*clips[i]);
                track->delete_clip(clips[i]);
            }
        }
    }

    return {
        .deleted_clips = std::move(deleted_clips),
        .modified_clips = std::move(modified_clips),
    };
}

PluginInterface* Engine::add_plugin_to_track(Track* track, PluginUID uid) {
    PluginInterface* plugin = pm_open_plugin(uid);
    if (!plugin) {
        Log::error("Failed to open plugin");
        return nullptr;
    }

    if (WB_PLUG_FAIL(plugin->init())) {
        plugin->shutdown();
        pm_close_plugin(plugin);
        Log::error("Failed to initialize plugin");
        return nullptr;
    }

    plugin->set_handler(&track->plugin_handler, track);

    uint32_t input_audio_bus_count = plugin->get_audio_bus_count(false);
    uint32_t output_audio_bus_count = plugin->get_audio_bus_count(true);
    uint32_t input_event_bus_count = plugin->get_event_bus_count(false);
    uint32_t default_input_bus = 0;
    uint32_t default_output_bus = 0;

    Log::debug("---- Plugin audio input bus ----");
    for (uint32_t i = 0; i < input_audio_bus_count; i++) {
        PluginAudioBusInfo bus_info;
        plugin->get_audio_bus_info(false, i, &bus_info);
        Log::debug("Bus: {} ({})", bus_info.name, bus_info.id);
        Log::debug("\tChannel count: {}", bus_info.channel_count);
        Log::debug("\tDefault bus: {}", bus_info.default_bus);
        if (bus_info.default_bus) {
            if (WB_PLUG_FAIL(plugin->activate_audio_bus(false, i, true)))
                Log::error("Failed to open audio input bus {}", i);
            default_input_bus = i;
        }
    }

    Log::debug("---- Plugin audio output bus ----");
    for (uint32_t i = 0; i < output_audio_bus_count; i++) {
        PluginAudioBusInfo bus_info;
        plugin->get_audio_bus_info(true, i, &bus_info);
        Log::debug("Bus: {} ({})", bus_info.name, bus_info.id);
        Log::debug("\tChannel count: {}", bus_info.channel_count);
        Log::debug("\tDefault bus: {}", bus_info.default_bus);
        if (bus_info.default_bus) {
            if (WB_PLUG_FAIL(plugin->activate_audio_bus(true, i, true)))
                Log::error("Failed to open audio input bus {}", i);
            default_output_bus = i;
        }
    }

    Log::debug("---- Plugin event input bus ----");
    for (uint32_t i = 0; i < input_event_bus_count; i++) {
        PluginEventBusInfo bus_info;
        plugin->get_event_bus_info(false, i, &bus_info);
        Log::debug("Bus: {} ({})", bus_info.name, bus_info.id);
        if (WB_PLUG_FAIL(plugin->activate_event_bus(false, i, true)))
            Log::error("Failed to open audio input bus {}", i);
    }

    if (WB_PLUG_FAIL(
            plugin->init_processing(PluginProcessingMode::Realtime, audio_buffer_size, (double)audio_sample_rate))) {
        Log::error("Cannot initialize processing");
    }

    if (WB_PLUG_FAIL(plugin->start_processing()))
        Log::error("Cannot start plugin processing");

    editor_lock.lock();
    track->default_input_bus = default_input_bus;
    track->default_output_bus = default_output_bus;
    track->plugin_instance = plugin;
    editor_lock.unlock();
    return plugin;
}

void Engine::delete_plugin_from_track(Track* track) {
    if (track->plugin_instance) {
        PluginInterface* plugin = track->plugin_instance;
        editor_lock.lock();
        track->plugin_instance = nullptr;
        editor_lock.unlock();
        plugin->stop_processing();
        plugin->shutdown();
        pm_close_plugin(plugin);
    }
}

double Engine::get_song_length() const {
    double max_length = std::numeric_limits<double>::min();
    for (auto track : tracks) {
        if (!track->clips.empty()) {
            Clip* clip = track->clips.back();
            max_length = math::max(max_length, clip->max_time * ppq);
        } else {
            max_length = math::max(max_length, 10000.0);
        }
    }
    return max_length;
}

void Engine::update_audio_visualization(float frame_rate) {
    double frame_rate_sec = 1.0 / (double)frame_rate;
    double buffer_duration_sec = audio_buffer_duration_ms / 1000.0;
    double speed = (double)frame_rate * std::max(frame_rate_sec, buffer_duration_sec);
    for (auto track : tracks) {
        for (auto& vu_channel : track->level_meter) {
            vu_channel.update(frame_rate, (float)(speed * 0.1));
        }
    }
}

void Engine::process(const AudioBuffer<float>& input_buffer, AudioBuffer<float>& output_buffer, double sample_rate) {
    double buffer_duration = (double)output_buffer.n_samples / sample_rate;
    double current_beat_duration = beat_duration.load(std::memory_order_relaxed);
    double current_playhead_position = playhead;
    int64_t playhead_in_samples = beat_to_samples(playhead, sample_rate, current_beat_duration);
    bool currently_playing = playing.load(std::memory_order_relaxed);

    editor_lock.lock();

    for (uint32_t i = 0; i < tracks.size(); i++) {
        auto track = tracks[i];
        track->audio_event_buffer.resize(0);
        track->midi_event_list.clear();
        if (track->midi_voice_state.has_voice() && !currently_playing) {
            track->stop_midi_notes(0, playhead);
        }
    }

    if (currently_playing) {
        double inv_ppq = 1.0 / ppq;
        double buffer_duration_in_beats = buffer_duration / current_beat_duration;
        double next_playhead_pos = playhead + buffer_duration_in_beats;

        for (auto track : tracks)
            track->process_event(playhead, next_playhead_pos, sample_position, current_beat_duration,
                                 buffer_duration_in_beats, sample_rate, ppq, inv_ppq, output_buffer.n_samples);

        sample_position += beat_to_samples(buffer_duration_in_beats, sample_rate, current_beat_duration);
        playhead = next_playhead_pos;
        playhead_ui.store(playhead, std::memory_order_release);
    }

    output_buffer.clear();

    for (uint32_t i = 0; i < tracks.size(); i++) {
        auto track = tracks[i];
        mixing_buffer.clear();
        track->process(input_buffer, mixing_buffer, sample_rate, current_beat_duration, current_playhead_position,
                       playhead_in_samples, currently_playing);
        output_buffer.mix(mixing_buffer);
    }

    // output_buffer.mix(input_buffer);

    for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
        float* channel = output_buffer.get_write_pointer(i);
        for (uint32_t j = 0; j < output_buffer.n_samples; j++) {
            if (channel[j] > 1.0) {
                channel[j] = 1.0;
            } else if (channel[j] < -1.0) {
                channel[j] = -1.0;
            }
        }
    }

    if (currently_playing && track_input_groups.size() != 0 && recording) {
        recorder_queue.begin_write(audio_buffer_size);
        for (uint32_t i = 0; i < track_input_groups.size(); i++) {
            TrackInput input = TrackInput::from_packed_u32(track_input_groups[i].input);
            switch (input.type) {
                case TrackInputType::ExternalStereo:
                    recorder_queue.write(i, input.index * 2, 2, input_buffer);
                    break;
                case TrackInputType::ExternalMono:
                    recorder_queue.write(i, input.index, 1, input_buffer);
                    break;
                default:
                    WB_UNREACHABLE();
            }
        }
        recorder_queue.end_write();
    }

    editor_lock.unlock();

    if (has_deleted_clips.load(std::memory_order_relaxed)) {
        delete_lock.lock();
        for (auto track : tracks) {
            Log::debug("Deleting pending clips for track: {} ...", (uintptr_t)track);
            track->flush_deleted_clips(playhead);
        }
        delete_lock.unlock();
        has_deleted_clips.store(false, std::memory_order_relaxed);
    }
}

void Engine::write_recorded_samples_(uint32_t num_samples) {
    for (uint32_t i = 0; i < track_input_groups.size(); i++) {
        TrackInputGroup& group = track_input_groups[i];
        TrackInput input = TrackInput::from_packed_u32(group.input);
        uint32_t num_channels = input.type == TrackInputType::ExternalMono ? 1 : 2;
        for (auto input_attr = group.input_attrs; input_attr != nullptr; input_attr = input_attr->next()) {
            Track* track = input_attr->track;
            size_t required_size = track->num_samples_written + num_samples;
            if (!track->recorded_samples) {
                // Create new sample instance if not exist
                track->recorded_samples.emplace(AudioFormat::F32, audio_sample_rate);
                track->recorded_samples->resize(audio_record_chunk_size / 4, num_channels);
            } else if (required_size >= track->recorded_samples->count) {
                // Resize the storage size if the required size exceeds current sample count
                track->recorded_samples->resize(track->recorded_samples->count + audio_record_chunk_size / 4,
                                                num_channels);
                Log::debug("Resize sample");
            }
            auto sample_data = track->recorded_samples->get_sample_data<float>();
            recorder_queue.read(i, sample_data, track->num_samples_written, 0, num_channels);
            track->num_samples_written = required_size;
        }
    }
}

void Engine::recorder_thread_runner_(Engine* engine) {
    uint32_t num_samples_to_read = engine->audio_record_file_chunk_size / 4;
    while (engine->recorder_queue.begin_read(num_samples_to_read)) {
        engine->write_recorded_samples_(num_samples_to_read);
        engine->recorder_queue.end_read();
    }
    uint32_t remaining_samples = engine->recorder_queue.size();
    if (remaining_samples > 0) {
        engine->recorder_queue.begin_read(remaining_samples);
        engine->write_recorded_samples_(remaining_samples);
        engine->recorder_queue.end_read();
    }
}

Engine g_engine;

} // namespace wb