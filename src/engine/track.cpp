#include "track.h"
#include "assets_table.h"
#include "clip_edit.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "core/dsp_ops.h"
#include "core/panning_law.h"
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

std::optional<uint32_t> Track::find_next_clip(double time_pos, uint32_t hint) {
    if (clips.size() == 0) {
        return {};
    }

    if (clips.back()->max_time < time_pos) {
        return {};
    }

    auto begin = clips.begin();
    auto end = clips.end();

#if 0
    if (end - begin <= 64) {
        while (begin != end && time_pos >= (*begin)->max_time) {
            begin++;
        }
        if (begin == end) {
            return {};
        }
        return (*begin)->id;
    }
#endif

    auto clip =
        find_lower_bound(begin, end, time_pos, [](Clip* clip, double time_pos) { return clip->max_time <= time_pos; });

    if (clip == end) {
        return {};
    }

    return (*clip)->id;
}

void Track::reset_playback_state(double time_pos, bool refresh_voices) {
    if (!refresh_voices) {
        std::optional<uint32_t> next_clip = find_next_clip(time_pos);
        event_state.current_clip_idx.reset();
        event_state.clip_idx = next_clip;
        event_state.midi_note_idx = 0;
        midi_voice_state.voice_mask = 0;
        midi_voice_state.release_all();
    }
    event_state.refresh_voice = refresh_voices;
    event_state.partially_ended = false;
}

void Track::prepare_record(double time_pos) {
    if (!arm_record || input.mode == TrackInputMode::None)
        return;
    record_min_time = time_pos;
    record_max_time = time_pos;
    recording = true;
}

void Track::stop_record() {
    record_min_time = 0.0;
    record_max_time = 0.0;
    recording = false;
}

void Track::stop() {
    current_audio_event = {
        .type = EventType::None,
    };
    audio_event_buffer.resize(0);
    midi_event_list.clear();
    stop_record();
    // midi_voice_state.voice_mask = 0;
}

void Track::process_event(double start_time, double end_time, double sample_position, double beat_duration,
                          double buffer_duration, double sample_rate, double ppq, double inv_ppq,
                          uint32_t buffer_size) {
    if (clips.size() == 0) {
        if (event_state.refresh_voice) {
            audio_event_buffer.push_back({
                .type = EventType::StopSample,
                .buffer_offset = 0,
                .time = start_time,
            });
            stop_midi_notes(0, start_time);
            event_state.current_clip_idx.reset();
            event_state.clip_idx.reset();
            event_state.midi_note_idx = 0;
            event_state.refresh_voice = false;
        }
        if (recording)
            record_max_time += buffer_duration;
        return;
    }

    uint32_t num_clips = (uint32_t)clips.size();
    if (event_state.refresh_voice) [[unlikely]] {
        std::optional<uint32_t> clip_at_playhead = find_next_clip(start_time);
        if (clip_at_playhead) {
            if (event_state.clip_idx) {
                uint32_t idx = *event_state.clip_idx;
                if (idx < clips.size()) {
                    Clip* clip = clips[idx];
                    if (idx != *clip_at_playhead || start_time < clip->min_time || start_time > clip->max_time) {
                        if (clip->is_audio()) {
                            audio_event_buffer.push_back({
                                .type = EventType::StopSample,
                                .buffer_offset = 0,
                                .time = start_time,
                            });
                        } else {
                            stop_midi_notes(0, start_time);
                        }
                        event_state.clip_idx = *clip_at_playhead;
                        event_state.midi_note_idx = 0;
                    }
                }
            } else {
                event_state.clip_idx = *clip_at_playhead;
                event_state.midi_note_idx = 0;
            }
        } else {
            audio_event_buffer.push_back({
                .type = EventType::StopSample,
                .buffer_offset = 0,
                .time = start_time,
            });
            stop_midi_notes(0, start_time);
            event_state.clip_idx.reset();
            event_state.midi_note_idx = 0;
        }
        event_state.refresh_voice = false;
    }

    if (!event_state.clip_idx) {
        if (recording)
            record_max_time += buffer_duration;
        return;
    }

    uint32_t next_clip = event_state.clip_idx.value();
    while (next_clip < num_clips) {
        Clip* clip = clips[next_clip];
        bool is_audio = clip->is_audio();
        double min_time = clip->min_time;
        double max_time = clip->max_time;

        if (min_time > end_time)
            break;

        if (min_time >= start_time) { // Started from beginning
            if (is_audio) {
                double offset_from_start = beat_to_samples(min_time - start_time, sample_rate, beat_duration);
                double sample_offset = sample_position + offset_from_start;
                uint32_t buffer_offset = (uint32_t)((uint64_t)sample_offset % (uint64_t)buffer_size);
                audio_event_buffer.push_back({
                    .type = EventType::PlaySample,
                    .buffer_offset = buffer_offset,
                    .time = min_time,
                    .sample_offset = (size_t)clip->start_offset,
                    .sample = &clip->audio.asset->sample_instance,
                });
            } else {
                event_state.midi_note_idx = clip->midi.asset->find_first_note(clip->start_offset, 0);
            }
            clip->start_offset_changed = false;
        } else if (start_time > min_time && !event_state.partially_ended) { // Partially started
            double relative_start_time = start_time - min_time;
            if (is_audio) {
                double sample_pos = beat_to_samples(relative_start_time, sample_rate, beat_duration);
                size_t sample_offset = (size_t)(sample_pos + clip->start_offset);
                audio_event_buffer.push_back({
                    .type = EventType::PlaySample,
                    .buffer_offset = 0,
                    .time = start_time,
                    .sample_offset = sample_offset,
                    .sample = &clip->audio.asset->sample_instance,
                });
            } else {
                double actual_start_offset = relative_start_time + clip->start_offset;
                event_state.midi_note_idx = clip->midi.asset->find_first_note(actual_start_offset, 0);
            }
            clip->start_offset_changed = false;
        } else if (clip->start_offset_changed && event_state.partially_ended) [[unlikely]] {
            double relative_start_time = start_time - min_time;
            if (is_audio) {
                double sample_pos = beat_to_samples(relative_start_time, sample_rate, beat_duration);
                size_t sample_offset = (size_t)(sample_pos + clip->start_offset);
                audio_event_buffer.push_back({
                    .type = EventType::StopSample,
                    .buffer_offset = 0,
                    .time = start_time,
                });
                audio_event_buffer.push_back({
                    .type = EventType::PlaySample,
                    .buffer_offset = 0,
                    .time = start_time,
                    .sample_offset = sample_offset,
                    .sample = &clip->audio.asset->sample_instance,
                });
            } else {
                stop_midi_notes(0, start_time);
                double actual_start_offset = relative_start_time + clip->start_offset;
                event_state.midi_note_idx = clip->midi.asset->find_first_note(actual_start_offset, 0);
            }
            clip->start_offset_changed = false;
        }

        if (max_time <= end_time) {
            if (is_audio) {
                double offset_from_start = beat_to_samples(max_time - start_time, sample_rate, beat_duration);
                double sample_offset = sample_position + offset_from_start;
                uint32_t buffer_offset = (uint32_t)((uint64_t)sample_offset % (uint64_t)buffer_size);
                audio_event_buffer.push_back({
                    .type = EventType::StopSample,
                    .buffer_offset = buffer_offset,
                    .time = max_time,
                });
            } else {
                process_midi_event(clip, start_time, max_time, sample_position, beat_duration, sample_rate, ppq,
                                   inv_ppq, buffer_size);
            }
            event_state.partially_ended = false;
        } else {
            if (!is_audio) {
                process_midi_event(clip, start_time, end_time, sample_position, beat_duration, sample_rate, ppq,
                                   inv_ppq, buffer_size);
            }
            event_state.partially_ended = true;
            break;
        }

        next_clip++;
    }

    if (recording)
        record_max_time += buffer_duration;
    event_state.clip_idx = next_clip;
}

void Track::process_midi_event(Clip* clip, double start_time, double end_time, double sample_position,
                               double beat_duration, double sample_rate, double ppq, double inv_ppq,
                               uint32_t buffer_size) {
    MidiAsset* asset = clip->midi.asset;
    const MidiNoteBuffer& buffer = asset->data.channels[0];
    uint32_t midi_note_idx = event_state.midi_note_idx;
    uint32_t note_count = (uint32_t)buffer.size();
    double time_offset = clip->min_time - clip->start_offset;
    double next_time_offset = start_time;

    while (midi_note_idx < note_count) {
        const MidiNote& note = buffer[midi_note_idx];
        double min_time = math::round((time_offset + note.min_time) * ppq) * inv_ppq;
        double max_time = math::round((time_offset + note.max_time) * ppq) * inv_ppq;

        if (min_time > end_time)
            break;

        while (auto voice = midi_voice_state.release_voice(min_time)) {
            double offset_from_start = beat_to_samples(voice->max_time - start_time, sample_rate, beat_duration);
            double sample_offset = sample_position + offset_from_start;
            uint32_t buffer_offset = (uint32_t)((uint64_t)sample_offset % (uint64_t)buffer_size);
            midi_event_list.add_event({
                .type = MidiEventType::NoteOff,
                .buffer_offset = buffer_offset,
                .time = voice->max_time,
                .note_off =
                    {
                        .channel = 0,
                        .note_number = voice->note_number,
                        .velocity = voice->velocity * 0.5f,
                    },
            });
#if WB_DBG_LOG_NOTE_ON_EVENT
            char note_str[8] {};
            fmt::format_to_n(note_str, std::size(note_str), "{}{}", get_midi_note_scale(voice->note_number),
                             get_midi_note_octave(voice->note_number));
            Log::debug("Note off: {} length: {} at: {}", note_str, voice->max_time, buffer_offset);
#endif
        }

        double offset_from_start = beat_to_samples(min_time - start_time, sample_rate, beat_duration);
        double sample_offset = sample_position + offset_from_start;
        uint32_t buffer_offset = (uint32_t)((uint64_t)sample_offset % (uint64_t)buffer_size);

        bool voice_added = midi_voice_state.add_voice2({
            .max_time = max_time,
            .velocity = note.velocity * 0.5f,
            .channel = 0,
            .note_number = note.note_number,
        });

        // Skip if we have reached maximum voices
        if (!voice_added) {
            midi_note_idx++;
            continue;
        }

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
        Log::debug("Note on: {} {} {} -> {} at {}", note.id, note_str, min_time, max_time, buffer_offset);
#endif

        next_time_offset = min_time;
        midi_note_idx++;
    }

    while (auto voice = midi_voice_state.release_voice(end_time)) {
        double offset_from_start = beat_to_samples(voice->max_time - start_time, sample_rate, beat_duration);
        double sample_offset = sample_position + offset_from_start;
        uint32_t buffer_offset = (uint32_t)((uint64_t)sample_offset % (uint64_t)buffer_size);
        midi_event_list.add_event({
            .type = MidiEventType::NoteOff,
            .buffer_offset = buffer_offset,
            .time = voice->max_time,
            .note_off =
                {
                    .channel = 0,
                    .note_number = voice->note_number,
                    .velocity = voice->velocity * 0.5f,
                },
        });
#if WB_DBG_LOG_NOTE_ON_EVENT
        char note_str[8] {};
        fmt::format_to_n(note_str, std::size(note_str), "{}{}", get_midi_note_scale(voice->note_number),
                         get_midi_note_octave(voice->note_number));
        Log::debug("Note off: {} length: {} at: {}", note_str, voice->max_time, buffer_offset);
#endif
    }

    event_state.midi_note_idx = midi_note_idx;
}

void Track::stop_midi_notes(uint32_t buffer_offset, double time_pos) {
    while (auto voice = midi_voice_state.release_voice(std::numeric_limits<double>::max())) {
        midi_event_list.add_event({
            .type = MidiEventType::NoteOff,
            .buffer_offset = buffer_offset,
            .time = time_pos,
            .note_off =
                {
                    .channel = 0,
                    .note_number = voice->note_number,
                    .velocity = voice->velocity * 0.5f,
                },
        });
    }
}

void Track::process(const AudioBuffer<float>& input_buffer, AudioBuffer<float>& output_buffer, double sample_rate,
                    bool playing) {
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
            case TrackParameter_Pan: {
                parameter_state.pan = (float)last_value;
                PanningCoefficient pan = calculate_panning_coefs(parameter_state.pan, PanningLaw::ConstantPower_3db);
                parameter_state.pan_coeffs[0] = pan.left;
                parameter_state.pan_coeffs[1] = pan.right;
#ifdef WB_DBG_LOG_PARAMETER_UPDATE
                Log::debug("Pan changed: {} {} {}", parameter_state.pan, parameter_state.pan_coeffs[0],
                           parameter_state.pan_coeffs[1]);
#endif
                break;
            }
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
                        Log::debug("{}: Stop {} {}", name, event->time, event->buffer_offset);
                        break;
                    case EventType::PlaySample:
                        Log::debug("{}: Play {} {}", name, event->time, event->buffer_offset);
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

    float volume = parameter_state.mute ? 0.0f : parameter_state.volume;
    for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
        float* buf = output_buffer.channel_buffers[i];
        dsp::apply_gain(buf, output_buffer.n_samples, volume * parameter_state.pan_coeffs[i]);
        level_meter[i].push_samples(output_buffer, i);
    }

    param_changes.clear_changes();

    if (deleted_clips.size() > 0) {
        for (auto deleted_clip : deleted_clips) {
            destroy_clip(deleted_clip);
        }
        deleted_clips.resize(0);
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

    switch (sample->format) {
        case AudioFormat::I16:
            for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
                float* output = output_buffer.get_write_pointer(i);
                auto sample_data = sample->get_read_pointer<int16_t>(i % sample->channels);
                for (uint32_t j = 0; j < num_samples; j++) {
                    float sample = (float)sample_data[sample_offset + j] * i16_pcm_normalizer;
                    output[j + buffer_offset] += std::clamp(sample, -1.0f, 1.0f);
                }
            }
            break;
        case AudioFormat::I24:
            for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
                float* output = output_buffer.get_write_pointer(i);
                auto sample_data = sample->get_read_pointer<int32_t>(i % sample->channels);
                for (uint32_t j = 0; j < num_samples; j++) {
                    double sample = (double)sample_data[sample_offset + j] * i24_pcm_normalizer;
                    output[j + buffer_offset] += (float)std::clamp(sample, -1.0, 1.0);
                }
            }
            break;
        case AudioFormat::I32:
            for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
                float* output = output_buffer.get_write_pointer(i);
                auto sample_data = sample->get_read_pointer<int32_t>(i % sample->channels);
                for (uint32_t j = 0; j < num_samples; j++) {
                    double sample = (double)sample_data[sample_offset + j] * i32_pcm_normalizer;
                    output[j + buffer_offset] += (float)std::clamp(sample, -1.0, 1.0);
                }
            }
            break;
        case AudioFormat::F32:
            for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
                float* output = output_buffer.get_write_pointer(i);
                auto sample_data = sample->get_read_pointer<float>(i % sample->channels);
                for (uint32_t j = 0; j < num_samples; j++) {
                    output[j + buffer_offset] += sample_data[sample_offset + j];
                }
            }
            break;
        case AudioFormat::F64:
            for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
                float* output = output_buffer.get_write_pointer(i);
                auto sample_data = sample->get_read_pointer<double>(i % sample->channels);
                for (uint32_t j = 0; j < num_samples; j++) {
                    output[j + buffer_offset] += (float)sample_data[sample_offset + j];
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
            MidiEvent& event = midi_event_list.get_event(event_idx);

            // Continue until the next event
            uint32_t event_length = event.buffer_offset - start_sample;
            test_synth.render(output_buffer, sample_rate, start_sample, event_length);
            start_sample += event_length;

            // Set next state
            for (; event_idx < event_count; event_idx++) {
                MidiEvent& event = midi_event_list.get_event(event_idx);
                if (event.buffer_offset > start_sample) {
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
                // Log::debug("{}", test_synth.voice_mask);
            }
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