#include "track.h"
#include "core/debug.h"
#include "core/math.h"
#include "core/queue.h"
#include "sample_table.h"
#include <algorithm>

#ifndef _NDEBUG
#define WB_LOG_CLIP_ORDERING 1
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

void Track::set_mute(bool mute) {
    ui_parameter_state.mute = mute;
    ui_param_changes.push({
        .id = TrackParameter_Mute,
        .sample_offset = 0,
        .value = (double)ui_parameter_state.mute,
    });
}

Clip* Track::add_audio_clip(const std::string& name, double min_time, double max_time,
                            const AudioClip& clip_info, double beat_duration) {
    Clip* clip = (Clip*)clip_allocator.allocate();
    if (!clip)
        return nullptr;
    new (clip) Clip(name, color, min_time, max_time);
    clip->init_as_audio_clip(clip_info);
    clips.push_back(clip);
    update(clip, beat_duration);
    return clip;
}

Clip* Track::duplicate_clip(Clip* clip_to_duplicate, double min_time, double max_time,
                            double beat_duration) {
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
    double new_pos = std::max(clip->min_time + relative_pos, 0.0);
    clip->max_time = new_pos + (clip->max_time - clip->min_time);
    clip->min_time = new_pos;
    update(clip, beat_duration);
}

void Track::resize_clip(Clip* clip, double relative_pos, double min_length, double beat_duration,
                        bool is_min) {
    if (relative_pos == 0.0)
        return;

    if (is_min) {
        // Compute new minimum time
        double old_min = clip->min_time;
        double new_min = std::max(clip->min_time + relative_pos, 0.0);
        if (new_min >= clip->max_time)
            new_min = clip->max_time - min_length;
        clip->min_time = new_min;

        double rel_offset = clip->relative_start_time;
        if (old_min < new_min)
            rel_offset -= old_min - new_min;
        else
            rel_offset += new_min - old_min;
        clip->relative_start_time = std::max(rel_offset, 0.0);

        // For audio, we also need to adjust the sample starting point
        if (clip->type == ClipType::Audio) {
            SampleAsset* asset = clip->audio.asset;
            clip->audio.start_sample_pos =
                beat_to_samples(clip->relative_start_time,
                                (double)asset->sample_instance.sample_rate, beat_duration);
        }
    } else {
        double new_max = std::max(clip->max_time + relative_pos, 0.0);
        if (new_max <= clip->min_time)
            new_max = clip->min_time + min_length;
        clip->max_time = new_max;
    }

    update(clip, beat_duration);
}

void Track::delete_clip(uint32_t id) {
    clips[id]->mark_deleted();
}

void Track::update(Clip* updated_clip, double beat_duration) {
    std::sort(clips.begin(), clips.end(),
              [](const Clip* a, const Clip* b) { return a->min_time < b->min_time; });

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
                        double rel_offset = clip->relative_start_time;
                        if (old_min < clip->min_time)
                            rel_offset -= old_min - clip->min_time;
                        else
                            rel_offset += clip->min_time - old_min;
                        clip->relative_start_time = std::max(rel_offset, 0.0);

                        if (clip->type == ClipType::Audio) {
                            SampleAsset* asset = clip->audio.asset;
                            clip->audio.start_sample_pos = beat_to_samples(
                                clip->relative_start_time,
                                (double)asset->sample_instance.sample_rate, beat_duration);
                        }
                    }
                }
            }

            // The clip is completely overlapped, delete this!
            if (clip->min_time >= clip->max_time)
                deleted_clip_ids.insert(clip->id);
        }
    }

    // Reconstruct IDs
    for (uint32_t i = 0; i < (uint32_t)clips.size(); i++) {
        clips[i]->id = i;
    }

#if WB_LOG_CLIP_ORDERING
    Log::debug("--- Clip Ordering ---");
    for (auto clip : clips) {
        Log::debug("{:x}: {} ({}, {}, {} -> {})", (uint64_t)clip, clip->name, clip->id,
                   clip->relative_start_time, clip->min_time, clip->max_time);
    }
#endif
}

Clip* Track::find_next_clip(double time_pos, uint32_t hint) {
    auto begin = clips.begin();
    auto end = clips.end();
    while (begin != end && time_pos >= (*begin)->max_time) {
        begin++;
    }
    if (begin == end)
        return nullptr;
    return *begin;
}

void Track::prepare_play(double time_pos) {
    Clip* next_clip = find_next_clip(time_pos);
    playback_state.current_clip = nullptr;
    playback_state.next_clip = next_clip;
}

void Track::stop() {
    current_event = {
        .type = EventType::None,
    };
    event_buffer.resize(0);
}

void Track::process_event(uint32_t buffer_offset, double time_pos, double beat_duration,
                          double sample_rate, double ppq, double inv_ppq) {
    if (clips.size() == 0)
        return;

    Clip* current_clip = playback_state.current_clip;
    Clip* next_clip = playback_state.next_clip;

    if (current_clip) {
        double max_time = math::uround(current_clip->max_time * ppq) * inv_ppq;
        if (time_pos >= max_time || current_clip->is_deleted()) {
            event_buffer.push_back({
                .type = EventType::StopSample,
                .buffer_offset = buffer_offset,
                .time = time_pos,
            });
            playback_state.current_clip = nullptr;
        }
    }

    while (next_clip && next_clip->is_deleted()) {
        auto new_next_clip = clips.begin() + (next_clip->id + 1);
        if (new_next_clip != clips.end()) {
            next_clip = *new_next_clip;
        } else {
            next_clip = nullptr;
        }
        playback_state.next_clip = next_clip;
    }

    if (next_clip) {
        double min_time = math::uround(next_clip->min_time * ppq) * inv_ppq;
        double max_time = math::uround(next_clip->max_time * ppq) * inv_ppq;
        assert(next_clip->type == ClipType::Audio);

        if (time_pos >= min_time && time_pos < max_time) {
            double relative_start_time = time_pos - min_time;
            double sample_pos = beat_to_samples(relative_start_time, sample_rate, beat_duration);
            uint64_t sample_offset = (uint64_t)(sample_pos + next_clip->audio.start_sample_pos);

            event_buffer.push_back({
                .type = EventType::PlaySample,
                .buffer_offset = buffer_offset,
                .time = time_pos,
                .audio =
                    {
                        .sample_offset = sample_offset,
                        .sample = &next_clip->audio.asset->sample_instance,
                    },
            });
            
            auto new_next_clip = clips.begin() + (next_clip->id + 1);
            if (new_next_clip != clips.end()) {
                playback_state.next_clip = *new_next_clip;
            } else {
                playback_state.next_clip = nullptr;
            }
            playback_state.current_clip = next_clip;
        }
    }
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
                Log::debug("Volume changed: {}", parameter_state.volume);
                break;
            case TrackParameter_Pan:
                parameter_state.pan = (float)last_value;
                Log::debug("Pan changed: {}", parameter_state.pan);
                break;
            case TrackParameter_Mute:
                parameter_state.mute = last_value > 0.0 ? 1.0f : 0.0f;
                Log::debug("Mute changed: {}", parameter_state.mute);
                break;
        }
    }

    if (playing) {
        Event* event = event_buffer.begin();
        Event* end = event_buffer.end();
        uint32_t start_sample = 0;
        while (start_sample < output_buffer.n_samples) {
            if (event != end) {
                uint32_t event_length = event->buffer_offset - start_sample;
                render_sample(output_buffer, start_sample, event_length, sample_rate);
                switch (event->type) {
                    case EventType::StopSample:
                        Log::debug("Stop {} {}", event->time, event->buffer_offset);
                        break;
                    case EventType::PlaySample:
                        Log::debug("Play {} {}", event->time, event->buffer_offset);
                        break;
                }
                current_event = *event;
                start_sample += event_length;
                event++;
            } else {
                uint32_t event_length = output_buffer.n_samples - start_sample;
                render_sample(output_buffer, start_sample, event_length, sample_rate);
                start_sample = output_buffer.n_samples;
            }
        }
    }

    for (uint32_t i = 0; i < output_buffer.n_channels; i++) {
        vu_meter[i].push_samples(output_buffer, i);
    }

    param_changes.clear_changes();
}

void Track::render_sample(AudioBuffer<float>& output_buffer, uint32_t buffer_offset,
                          uint32_t num_samples, double sample_rate) {
    switch (current_event.type) {
        case EventType::None:
        case EventType::StopSample:
            samples_processed = 0;
            break;
        case EventType::PlaySample: {
            Sample* sample = current_event.audio.sample;
            size_t sample_offset = samples_processed + current_event.audio.sample_offset;
            if (sample_offset >= sample->count)
                break;
            uint32_t min_num_samples =
                std::min(num_samples, (uint32_t)(sample->count - sample_offset));
            stream_sample(output_buffer, current_event.audio.sample, buffer_offset, min_num_samples,
                          sample_offset);
            samples_processed += min_num_samples;
            break;
        }
        default:
            break;
    }
}

void Track::update_playback_state(Event& event) {
    // TODO: Should put something here...
}

void Track::stream_sample(AudioBuffer<float>& output_buffer, Sample* sample, uint32_t buffer_offset,
                          uint32_t num_samples, size_t sample_offset) {
    static constexpr float i16_pcm_normalizer =
        1.0f / static_cast<float>(std::numeric_limits<int16_t>::max());
    static constexpr double i24_pcm_normalizer = 1.0 / static_cast<double>((1 << 23) - 1);
    static constexpr double i32_pcm_normalizer =
        1.0 / static_cast<double>(std::numeric_limits<int32_t>::max());

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

void Track::flush_deleted_clips() {
    uint32_t i = 0;
    Vector<Clip*> new_clip_list;
    new_clip_list.reserve(clips.size());
    for (auto clip : clips) {
        if (clip->is_deleted()) {
            // Make sure we don't touch this deleted clip
            if (clip == playback_state.next_clip)
                playback_state.next_clip = nullptr;
            if (clip == playback_state.current_clip)
                playback_state.current_clip = nullptr;
            clip->~Clip();
            clip_allocator.free(clip);
            continue;
        }
        clip->id = i;
        new_clip_list.push_back(clip);
        i++;
    }
    deleted_clip_ids.clear();
    clips = std::move(new_clip_list);
}

} // namespace wb