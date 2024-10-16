#include "track.h"
#include "assets_table.h"
#include "clip_edit.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "core/queue.h"
#include <algorithm>

#ifndef _NDEBUG
#define WB_DBG_LOG_CLIP_ORDERING 1
#define WB_DBG_LOG_NOTE_ON_EVENT 1
#define WB_DBG_LOG_AUDIO_EVENT 1
#define WB_DBG_LOG_PARAMETER_UPDATE 1
#endif

namespace wb {

Track::Track() {
    ui_parameter_state.volume = 1.0f;
    ui_parameter_state.pan = 0.0f;
    ui_parameter_state.mute = false;

    param_changes.set_max_params(TrackParameter_Max);
    ui_param_changes.push({
        .id = TrackParameter_Volume,
        .sample_offset = 0,
        .value = 1.0,
    });
    ui_param_changes.push({
        .id = TrackParameter_Pan,
        .sample_offset = 0,
        .value = 0.0,
    });
    ui_param_changes.push({
        .id = TrackParameter_Mute,
        .sample_offset = 0,
        .value = 0.0,
    });
}

Track::Track(const std::string& name, const ImColor& color, float height, bool shown,
             const TrackParameterState& track_param) :
    name(name), color(color), height(height), shown(shown), ui_parameter_state(track_param) {
    ui_parameter_state.volume_db = math::linear_to_db(track_param.volume);
    param_changes.set_max_params(TrackParameter_Max);
    ui_param_changes.push({
        .id = TrackParameter_Volume,
        .sample_offset = 0,
        .value = (double)ui_parameter_state.volume,
    });
    ui_param_changes.push({
        .id = TrackParameter_Pan,
        .sample_offset = 0,
        .value = (double)ui_parameter_state.pan,
    });
    ui_param_changes.push({
        .id = TrackParameter_Mute,
        .sample_offset = 0,
        .value = (double)ui_parameter_state.mute,
    });
}

Track::~Track() {
    for (auto clip : clips) {
        clip->~Clip();
        clip_allocator.free(clip);
    }
}

void Track::set_volume(float db) {
    ui_parameter_state.volume_db = db;
    ui_parameter_state.volume = math::db_to_linear(db);
    ui_param_changes.push({
        .id = TrackParameter_Volume,
        .sample_offset = 0,
        .value = (double)ui_parameter_state.volume,
    });
}

void Track::set_pan(float pan) {
    ui_parameter_state.pan = pan;
    ui_param_changes.push({
        .id = TrackParameter_Pan,
        .sample_offset = 0,
        .value = (double)ui_parameter_state.pan,
    });
}

void Track::set_mute(bool mute) {
    ui_parameter_state.mute = mute;
    ui_param_changes.push({
        .id = TrackParameter_Mute,
        .sample_offset = 0,
        .value = (double)ui_parameter_state.mute,
    });
}

Clip* Track::add_audio_clip(const std::string& name, double min_time, double max_time, double start_offset,
                            const AudioClip& clip_info, double beat_duration, bool active) {
    Clip* clip = (Clip*)clip_allocator.allocate();
    if (!clip)
        return nullptr;
    new (clip) Clip(name, color, min_time, max_time);
    clip->init_as_audio_clip(clip_info);
    clip->start_offset = start_offset;
    clip->active.store(active, std::memory_order_release);
    clips.push_back(clip);
    update(clip, beat_duration);
    // add_to_cliplist(clip, beat_duration);
    return clip;
}

Clip* Track::add_midi_clip(const std::string& name, double min_time, double max_time, double start_offset,
                           const MidiClip& clip_info, double beat_duration, bool active) {
    Clip* clip = (Clip*)clip_allocator.allocate();
    if (!clip)
        return nullptr;
    new (clip) Clip(name, color, min_time, max_time);
    clip->init_as_midi_clip(clip_info);
    clip->start_offset = start_offset;
    clip->active.store(active, std::memory_order_release);
    clips.push_back(clip);
    update(clip, beat_duration);
    return clip;
}

Clip* Track::duplicate_clip(Clip* clip_to_duplicate, double min_time, double max_time, double beat_duration) {
    Clip* clip = (Clip*)clip_allocator.allocate();
    if (!clip)
        return nullptr;
    new (clip) Clip(*clip_to_duplicate);
    clip->min_time = min_time;
    clip->max_time = max_time;
    clips.push_back(clip);
    update(clip, beat_duration);
    return clip;
}

void Track::move_clip(Clip* clip, double relative_pos, double beat_duration) {
    if (relative_pos == 0.0)
        return;
    auto [min_time, max_time] = calc_move_clip(clip, relative_pos);
    clip->min_time = min_time;
    clip->max_time = max_time;
    clip->start_offset_changed = true;
    update(clip, beat_duration);
}

void Track::resize_clip(Clip* clip, double relative_pos, double min_length, double beat_duration, bool is_min) {
    if (relative_pos == 0.0)
        return;
    auto [min_time, max_time, start_offset] = calc_resize_clip(clip, relative_pos, min_length, beat_duration, is_min);
    if (is_min) {
        clip->min_time = min_time;
        clip->start_offset = start_offset;
    } else {
        clip->max_time = max_time;
    }
    update(clip, beat_duration);
}

void Track::delete_clip(uint32_t id) {
    clips[id]->mark_deleted();
}

void Track::delete_clip(Clip* clip) {
    clip->mark_deleted();
    has_deleted_clips = true;
}

std::optional<ClipQueryResult> Track::query_clip_by_range(double min, double max) const {
    assert(min <= max && "Minimum value should be less or equal than maximum value");
    auto begin = clips.begin();
    auto end = clips.end();

    if (begin == end)
        return {};

    auto first =
        wb::find_lower_bound(begin, end, min, [](const Clip* clip, double time) { return clip->max_time <= time; });
    auto last =
        wb::find_lower_bound(begin, end, max, [](const Clip* clip, double time) { return clip->min_time <= time; });
    uint32_t first_clip = first - begin;
    uint32_t last_clip = last - begin;
    double first_offset;
    double last_offset;

    if (first == last && ((min < (*first)->min_time && max < (*first)->min_time) ||
                          (min > (*first)->max_time && max > (*first)->max_time))) {
        return {};
    }

    if (min > (*first)->max_time) {
        first_clip++;
        first_offset = min - clips[first_clip]->min_time;
    } else {
        first_offset = min - (*first)->min_time;
    }

    if (max < (*last)->min_time) {
        last_clip--;
        last_offset = max - clips[last_clip]->min_time;
    } else {
        last_offset = max - (*last)->min_time;
    }

    return ClipQueryResult {
        .first = first_clip,
        .last = last_clip,
        .first_offset = first_offset,
        .last_offset = last_offset,
    };
}

void Track::update_clip_ordering() {
    Vector<Clip*> new_cliplist;
    if (has_deleted_clips) {
        for (auto clip : clips) {
            if (clip->is_deleted()) {
                deleted_clips.push_back(clip);
                continue;
            }
            new_cliplist.push_back(clip);
        }
        has_deleted_clips = false;
        clips = std::move(new_cliplist);
    }
    std::sort(clips.begin(), clips.end(), [](const Clip* a, const Clip* b) { return a->min_time < b->min_time; });
    for (uint32_t i = 0; i < (uint32_t)clips.size(); i++) {
        clips[i]->id = i;
    }
}

void Track::update(Clip* updated_clip, double beat_duration) {
    std::sort(clips.begin(), clips.end(), [](const Clip* a, const Clip* b) { return a->min_time < b->min_time; });

    // Trim overlapping clips or delete if completely overlapped
    if (updated_clip && beat_duration != 0.0) {
        // std::vector<Clip*> deleted_clips;
        for (uint32_t i = 0; i < (uint32_t)clips.size(); i++) {
            Clip* clip = clips[i];

            if (clip == updated_clip)
                continue;

            if (clip->min_time <= updated_clip->min_time) {
                if (updated_clip->min_time < clip->max_time) {
                    clip->max_time = updated_clip->min_time;
                }
            } else {
                if (updated_clip->max_time > clip->min_time) {
                    double old_min = clip->min_time;
                    clip->min_time = updated_clip->max_time;

                    if (clip->min_time < clip->max_time) {
                        double start_offset = clip->start_offset;
                        SampleAsset* asset = nullptr;
                        if (clip->is_audio()) {
                            asset = clip->audio.asset;
                            start_offset = samples_to_beat(start_offset, (double)asset->sample_instance.sample_rate,
                                                           beat_duration);
                        }

                        if (old_min < clip->min_time)
                            start_offset -= old_min - clip->min_time;
                        else
                            start_offset += clip->min_time - old_min;

                        start_offset = std::max(start_offset, 0.0);

                        if (clip->is_audio()) {
                            start_offset = beat_to_samples(start_offset, (double)asset->sample_instance.sample_rate,
                                                           beat_duration);
                        }

                        clip->start_offset = start_offset;
                    }
                }
            }

            // The clip is completely overlapped, delete this!
            /*if (clip->min_time >= clip->max_time)
                deleted_clip_ids.insert(clip->id);*/
        }
    }

    // Reconstruct IDs
    for (uint32_t i = 0; i < (uint32_t)clips.size(); i++) {
        clips[i]->id = i;
    }

#if WB_DBG_LOG_CLIP_ORDERING
    Log::debug("--- Clip Ordering ---");
    for (auto clip : clips) {
        Log::debug("{:x}: {} ({}, {}, {} -> {})", (uint64_t)clip, clip->name, clip->id, clip->start_offset,
                   clip->min_time, clip->max_time);
    }
#endif
}

std::optional<uint32_t> Track::find_next_clip(double time_pos, uint32_t hint) {
    auto begin = clips.begin();
    auto end = clips.end();

    if (end - begin <= 64) {
        while (begin != end && time_pos >= (*begin)->max_time) {
            begin++;
        }
        if (begin == end) {
            return {};
        }
        return (*begin)->id;
    }

    auto clip =
        find_lower_bound(begin, end, time_pos, [](Clip* clip, double time_pos) { return clip->max_time <= time_pos; });

    if (clip == end) {
        return {};
    }

    return (*clip)->id;
}

void Track::reset_playback_state(double time_pos, bool refresh_voices) {
    if (refresh_voices) {
        event_state.refresh_voice = true;
    } else {
        std::optional<uint32_t> next_clip = find_next_clip(time_pos);
        event_state.current_clip_idx.reset();
        event_state.next_clip_idx = next_clip;
        event_state.midi_note_idx = 0;
        midi_voice_state.voice_mask = 0;
    }
}

void Track::stop() {
    current_audio_event = {
        .type = EventType::None,
    };
    audio_event_buffer.resize(0);
    midi_event_list.clear();
    // midi_voice_state.voice_mask = 0;
}

void Track::process_event(uint32_t buffer_offset, double time_pos, double beat_duration, double sample_rate, double ppq,
                          double inv_ppq) {
    bool refresh_voices = event_state.refresh_voice;

    if (clips.size() == 0) {
        if (event_state.refresh_voice) {
            event_state.current_clip_idx.reset();
            event_state.next_clip_idx.reset();
            audio_event_buffer.push_back({
                .type = EventType::StopSample,
                .buffer_offset = buffer_offset,
                .time = time_pos,
            });
            stop_midi_notes(buffer_offset, time_pos);
            event_state.midi_note_idx = 0;
            event_state.refresh_voice = false;
        }
        return;
    }

    if (refresh_voices) {
        std::optional<uint32_t> clip_at_playhead = find_next_clip(time_pos);
        if (clip_at_playhead) {
            if (event_state.current_clip_idx) {
                uint32_t idx = *event_state.current_clip_idx;
                if (idx != *clip_at_playhead) {
                    audio_event_buffer.push_back({
                        .type = EventType::StopSample,
                        .buffer_offset = buffer_offset,
                        .time = time_pos,
                    });
                    stop_midi_notes(buffer_offset, time_pos);
                    event_state.current_clip_idx.reset();
                    event_state.next_clip_idx = *clip_at_playhead;
                    event_state.midi_note_idx = 0;
                }
            }
        } else {
            event_state.current_clip_idx.reset();
            event_state.next_clip_idx.reset();
            audio_event_buffer.push_back({
                .type = EventType::StopSample,
                .buffer_offset = buffer_offset,
                .time = time_pos,
            });
            stop_midi_notes(buffer_offset, time_pos);
            event_state.midi_note_idx = 0;
        }
        event_state.refresh_voice = false;
    }

    Clip* current_clip = event_state.current_clip_idx ? clips[*event_state.current_clip_idx] : nullptr;
    Clip* next_clip = event_state.next_clip_idx && event_state.next_clip_idx < clips.size()
                          ? clips[*event_state.next_clip_idx]
                          : nullptr;

    if (current_clip) {
        double max_time = math::uround(current_clip->max_time * ppq) * inv_ppq;
        double min_time = math::uround(current_clip->min_time * ppq) * inv_ppq;
        if (time_pos < min_time || time_pos >= max_time || current_clip->is_deleted() || !current_clip->is_active()) {
            switch (current_clip->type) {
                case ClipType::Audio:
                    audio_event_buffer.push_back({
                        .type = EventType::StopSample,
                        .buffer_offset = buffer_offset,
                        .time = time_pos,
                    });
                    break;
                case ClipType::Midi:
                    stop_midi_notes(buffer_offset, time_pos);
                    event_state.midi_note_idx = 0;
                    break;
                default:
                    break;
            }
            if (time_pos < min_time || current_clip->is_deleted()) {
                event_state.next_clip_idx = find_next_clip(time_pos);
            }
            event_state.current_clip_idx.reset();
        } else if (current_clip->start_offset_changed) {
            double relative_start_time = time_pos - min_time;
            if (current_clip->is_audio()) {
                double sample_pos = beat_to_samples(relative_start_time, sample_rate, beat_duration);
                uint64_t sample_offset = (uint64_t)(sample_pos + current_clip->start_offset);
                audio_event_buffer.push_back({
                    .type = EventType::StopSample,
                    .buffer_offset = buffer_offset,
                    .time = time_pos,
                });
                audio_event_buffer.push_back({
                    .type = EventType::PlaySample,
                    .buffer_offset = buffer_offset,
                    .time = time_pos,
                    .sample_offset = sample_offset,
                    .sample = &current_clip->audio.asset->sample_instance,
                });
            } else {
                stop_midi_notes(buffer_offset, time_pos);
                double clip_pos = time_pos - current_clip->min_time;
                if (clip_pos >= 0.0) {
                    event_state.midi_note_idx =
                        current_clip->midi.asset->find_first_note(clip_pos + current_clip->start_offset, 0);
                }
                process_midi_event(current_clip, buffer_offset, time_pos, beat_duration, ppq, inv_ppq);
            }
            current_clip->start_offset_changed = false;
        } else {
            if (current_clip->is_midi()) {
                process_midi_event(current_clip, buffer_offset, time_pos, beat_duration, ppq, inv_ppq);
            }
        }
    }

    if (next_clip && (!next_clip->is_deleted() || next_clip->is_active())) {
        double min_time = math::uround(next_clip->min_time * ppq) * inv_ppq;
        double max_time = math::uround(next_clip->max_time * ppq) * inv_ppq;

        if (time_pos >= min_time && time_pos < max_time && next_clip->is_active()) {
            double relative_start_time = time_pos - min_time;

            switch (next_clip->type) {
                case ClipType::Audio: {
                    double sample_pos = beat_to_samples(relative_start_time, sample_rate, beat_duration);
                    uint64_t sample_offset = (uint64_t)(sample_pos + next_clip->start_offset);
                    audio_event_buffer.push_back({
                        .type = EventType::PlaySample,
                        .buffer_offset = buffer_offset,
                        .time = time_pos,
                        .sample_offset = sample_offset,
                        .sample = &next_clip->audio.asset->sample_instance,
                    });
                    break;
                }
                case ClipType::Midi: {
                    double clip_pos = time_pos - next_clip->min_time;
                    if (clip_pos >= 0.0) {
                        event_state.midi_note_idx =
                            next_clip->midi.asset->find_first_note(clip_pos + next_clip->start_offset, 0);
                    }
                    process_midi_event(next_clip, buffer_offset, time_pos, beat_duration, ppq, inv_ppq);
                    break;
                }
                default:
                    break;
            }

            uint32_t new_next_clip = next_clip->id + 1;
            event_state.current_clip_idx = event_state.next_clip_idx;
            event_state.next_clip_idx = new_next_clip;
        }
    }
}

void Track::process_midi_event(Clip* clip, uint32_t buffer_offset, double time_pos, double beat_duration, double ppq,
                               double inv_ppq) {
    MidiAsset* asset = clip->midi.asset;
    const MidiNoteBuffer& buffer = asset->data.channels[0];
    double time_offset = clip->min_time - clip->start_offset;

    // Check for active voice and release it if reaches the end of time
    uint64_t active_voice_bits = midi_voice_state.voice_mask;
    uint64_t inactive_voice_bits = 0;
    while (active_voice_bits) {
        int active_voice = next_set_bits(active_voice_bits);
        const MidiVoice& voice = midi_voice_state.voices[active_voice];
        double max_time = voice.max_time;
        if (time_pos >= max_time) {
            inactive_voice_bits |= 1ull << active_voice;
            midi_event_list.add_event({
                .type = MidiEventType::NoteOff,
                .buffer_offset = buffer_offset,
                .time = max_time,
                .note_off =
                    {
                        .channel = 0,
                        .note_number = voice.note_number,
                        .velocity = voice.velocity * 0.5f,
                    },
            });
#if WB_DBG_LOG_NOTE_ON_EVENT
            char note_str[8] {};
            fmt::format_to_n(note_str, std::size(note_str), "{}{}", get_midi_note_scale(voice.note_number),
                             get_midi_note_octave(voice.note_number));
            Log::debug("Note off: {} length: {} at: {}", note_str, max_time, time_pos);
#endif
        }
    }
    midi_voice_state.voice_mask &= ~inactive_voice_bits;

    // Check for incoming midi notes
    while (event_state.midi_note_idx < buffer.size()) {
        const MidiNote& note = buffer[event_state.midi_note_idx];

        double min_time = math::uround((time_offset + note.min_time) * ppq) * inv_ppq;
        if (min_time > time_pos) {
            break;
        }

        double max_time = math::uround((time_offset + note.max_time) * ppq) * inv_ppq;
        if (max_time < time_pos) {
            event_state.midi_note_idx++;
            continue;
        }

        midi_voice_state.add_voice({
            .max_time = max_time,
            .velocity = note.velocity * 0.5f,
            .channel = 0,
            .note_number = note.note_number,
        });

        midi_event_list.add_event({
            .type = MidiEventType::NoteOn,
            .buffer_offset = buffer_offset,
            .time = min_time,
            .note_on =
                {
                    .channel = 0,
                    .note_number = note.note_number,
                    .velocity = note.velocity * 0.5f,
                },
        });

#if WB_DBG_LOG_NOTE_ON_EVENT
        char note_str[8] {};
        fmt::format_to_n(note_str, std::size(note_str), "{}{}", get_midi_note_scale(note.note_number),
                         get_midi_note_octave(note.note_number));
        Log::debug("Note on: {} {} {} -> {} at {}", note.id, note_str, min_time, max_time, time_pos);
#endif

        event_state.midi_note_idx++;
    }

    // Log::debug("{:b}", midi_voice_state.voice_mask);
}

void Track::stop_midi_notes(uint32_t buffer_offset, double time_pos) {
    uint64_t active_voice_bits = midi_voice_state.voice_mask;
    uint64_t inactive_voice_bits = 0;
    while (active_voice_bits) {
        int active_voice = next_set_bits(active_voice_bits);
        inactive_voice_bits |= 1ull << active_voice;
        const MidiVoice& voice = midi_voice_state.voices[active_voice];
        midi_event_list.add_event({
            .type = MidiEventType::NoteOff,
            .buffer_offset = buffer_offset,
            .time = time_pos,
            .note_off =
                {
                    .channel = 0,
                    .note_number = voice.note_number,
                    .velocity = voice.velocity * 0.5f,
                },
        });
    }
    midi_voice_state.voice_mask &= ~inactive_voice_bits;
}

void Track::process(AudioBuffer<float>& output_buffer, double sample_rate, bool playing) {
    param_changes.transfer_changes_from(ui_param_changes);

    for (uint32_t i = 0; i < param_changes.changes_count; i++) {
        ParamValueQueue& queue = param_changes.queues[i];
        if (queue.points.size() == 0) {
            continue;
        }
        size_t last_idx = queue.points.size() - 1;
        double last_value = queue.points[last_idx].value;
        switch (queue.id) {
            case TrackParameter_Volume:
                parameter_state.volume = (float)last_value;
#ifdef WB_DBG_LOG_PARAMETER_UPDATE
                Log::debug("Volume changed: {} {}", parameter_state.volume, math::linear_to_db(parameter_state.volume));
#endif
                break;
            case TrackParameter_Pan:
                parameter_state.pan = (float)last_value;
#ifdef WB_DBG_LOG_PARAMETER_UPDATE
                Log::debug("Pan changed: {}", parameter_state.pan);
#endif
                break;
            case TrackParameter_Mute:
                parameter_state.mute = last_value > 0.0 ? 1.0f : 0.0f;
#ifdef WB_DBG_LOG_PARAMETER_UPDATE
                Log::debug("Mute changed: {}", parameter_state.mute);
#endif
                break;
        }
    }

    if (playing) {
        AudioEvent* event = audio_event_buffer.begin();
        AudioEvent* end = audio_event_buffer.end();
        uint32_t start_sample = 0;
        while (start_sample < output_buffer.n_samples) {
            if (event != end) {
                uint32_t event_length = event->buffer_offset - start_sample;
                render_sample(output_buffer, start_sample, event_length, sample_rate);
#if WB_DBG_LOG_AUDIO_EVENT
                switch (event->type) {
                    case EventType::StopSample:
                        Log::debug("Stop {} {}", event->time, event->buffer_offset);
                        break;
                    case EventType::PlaySample:
                        Log::debug("Play {} {}", event->time, event->buffer_offset);
                        break;
                    default:
                        break;
                }
#endif
                current_audio_event = *event;
                start_sample += event_length;
                event++;
            } else {
                uint32_t event_length = output_buffer.n_samples - start_sample;
                render_sample(output_buffer, start_sample, event_length, sample_rate);
                start_sample = output_buffer.n_samples;
            }
        }
    }

    process_test_synth(output_buffer, sample_rate, playing);

    for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
        level_meter[i].push_samples(output_buffer, i);
    }

    param_changes.clear_changes();

    for (auto deleted_clip : deleted_clips) {
        destroy_clip(deleted_clip);
    }
}

void Track::render_sample(AudioBuffer<float>& output_buffer, uint32_t buffer_offset, uint32_t num_samples,
                          double sample_rate) {
    switch (current_audio_event.type) {
        case EventType::None:
        case EventType::StopSample:
            samples_processed = 0;
            break;
        case EventType::PlaySample: {
            Sample* sample = current_audio_event.sample;
            size_t sample_offset = samples_processed + current_audio_event.sample_offset;
            if (sample_offset >= sample->count)
                break;
            uint32_t min_num_samples = std::min(num_samples, (uint32_t)(sample->count - sample_offset));
            stream_sample(output_buffer, current_audio_event.sample, buffer_offset, min_num_samples, sample_offset);
            samples_processed += min_num_samples;
            break;
        }
        default:
            break;
    }
}

void Track::stream_sample(AudioBuffer<float>& output_buffer, Sample* sample, uint32_t buffer_offset,
                          uint32_t num_samples, size_t sample_offset) {
    static constexpr float i16_pcm_normalizer = 1.0f / static_cast<float>(std::numeric_limits<int16_t>::max());
    static constexpr double i24_pcm_normalizer = 1.0 / static_cast<double>((1 << 23) - 1);
    static constexpr double i32_pcm_normalizer = 1.0 / static_cast<double>(std::numeric_limits<int32_t>::max());

    float volume = parameter_state.mute ? 0.0f : parameter_state.volume;

    switch (sample->format) {
        case AudioFormat::I16:
            for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
                float* output = output_buffer.get_write_pointer(i);
                auto sample_data = sample->get_read_pointer<int16_t>(i % sample->channels);
                for (uint32_t j = 0; j < num_samples; j++) {
                    float sample = (float)sample_data[sample_offset + j] * i16_pcm_normalizer;
                    output[j + buffer_offset] += std::clamp(sample, -1.0f, 1.0f) * volume;
                }
            }
            break;
        case AudioFormat::I24:
            for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
                float* output = output_buffer.get_write_pointer(i);
                auto sample_data = sample->get_read_pointer<int32_t>(i % sample->channels);
                for (uint32_t j = 0; j < num_samples; j++) {
                    double sample = (double)sample_data[sample_offset + j] * i24_pcm_normalizer;
                    output[j + buffer_offset] += (float)std::clamp(sample, -1.0, 1.0) * volume;
                }
            }
            break;
        case AudioFormat::I32:
            for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
                float* output = output_buffer.get_write_pointer(i);
                auto sample_data = sample->get_read_pointer<int32_t>(i % sample->channels);
                for (uint32_t j = 0; j < num_samples; j++) {
                    double sample = (double)sample_data[sample_offset + j] * i32_pcm_normalizer;
                    output[j + buffer_offset] += (float)std::clamp(sample, -1.0, 1.0) * volume;
                }
            }
            break;
        case AudioFormat::F32:
            for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
                float* output = output_buffer.get_write_pointer(i);
                auto sample_data = sample->get_read_pointer<float>(i % sample->channels);
                for (uint32_t j = 0; j < num_samples; j++) {
                    output[j + buffer_offset] += sample_data[sample_offset + j] * volume;
                }
            }
            break;
        case AudioFormat::F64:
            for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
                float* output = output_buffer.get_write_pointer(i);
                auto sample_data = sample->get_read_pointer<double>(i % sample->channels);
                for (uint32_t j = 0; j < num_samples; j++) {
                    output[j + buffer_offset] += (float)sample_data[sample_offset + j] * volume;
                }
            }
            break;
        default:
            assert(false && "Unsupported format");
            break;
    }
}

// This code is only made for testing purposes, the code will be removed later
void Track::process_test_synth(AudioBuffer<float>& output_buffer, double sample_rate, bool playing) {
    uint32_t event_idx = 0;
    uint32_t event_count = midi_event_list.size();
    uint32_t start_sample = 0;
    uint32_t next_buffer_offset = 0;
    while (start_sample < output_buffer.n_samples) {
        if (event_idx < event_count) {
            // Find next event buffer offset
            for (uint32_t i = event_idx; i < event_count; i++) {
                MidiEvent& event = midi_event_list.get_event(i);
                if (start_sample > event.buffer_offset) {
                    break;
                }
                next_buffer_offset = event.buffer_offset;
            }

            // Continue until the next event
            uint32_t event_length = next_buffer_offset - start_sample;
            test_synth.render(output_buffer, sample_rate, start_sample, event_length);

            // Set next state
            for (; event_idx < event_count; event_idx++) {
                MidiEvent& event = midi_event_list.get_event(event_idx);
                if (start_sample > event.buffer_offset) {
                    break;
                }
                switch (event.type) {
                    case MidiEventType::NoteOn:
                        test_synth.add_voice(event);
                        break;
                    case MidiEventType::NoteOff:
                        test_synth.remove_note(event.note_off.note_number);
                        break;
                    default:
                        break;
                }
            }

            start_sample += event_length;
        } else {
            test_synth.render(output_buffer, sample_rate, start_sample, output_buffer.n_samples - start_sample);
            start_sample = output_buffer.n_samples;
        }
    }
}

void Track::flush_deleted_clips(double time_pos) {
    uint32_t i = 0;
    bool is_playing_current_clip = false;
    Vector<Clip*> new_clip_list;
    new_clip_list.reserve(clips.size());
    for (auto clip : clips) {
        if (clip->is_deleted()) {
            clip->~Clip();
            clip_allocator.free(clip);
            continue;
        }
        clip->id = i;
        new_clip_list.push_back(clip);
        i++;
    }
    // deleted_clip_ids.clear();
    clips = std::move(new_clip_list);
}

} // namespace wb