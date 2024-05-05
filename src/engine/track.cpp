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

Clip* Track::add_audio_clip(const std::string& name, double min_time, double max_time,
                            const AudioClip& clip_info, double beat_duration) {
    Clip* clip = (Clip*)clip_allocator.allocate();
    if (!clip)
        return nullptr;
    new (clip) Clip(name, color, min_time, max_time);
    clip->as_audio_clip(clip_info);
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
            clip->audio.min_sample_pos =
                beat_to_samples(clip->relative_start_time,
                                (double)asset->sample_instance.sample_rate, beat_duration);
        }

        /*if (clip->type == ClipType::Audio) {
            SampleAsset* asset = clip->audio.asset;

            intptr_t new_start_sample = (intptr_t)beat_to_samples(
                new_min, (double)asset->sample_instance.sample_rate, beat_duration);
            intptr_t old_start_sample = (intptr_t)beat_to_samples(
                old_min, (double)asset->sample_instance.sample_rate, beat_duration);

            intptr_t sample_offset = clip->audio.min_sample_pos;
            if (old_min < new_min)
                sample_offset -= old_start_sample - new_start_sample;
            else
                sample_offset += new_start_sample - old_start_sample;
            sample_offset = std::max(sample_offset, 0ll);

            Log::info("{}", clip->audio.min_sample_pos);

            clip->audio.min_sample_pos = (size_t)sample_offset;
        }*/
    } else {
        double new_max = std::max(clip->max_time + relative_pos, 0.0);
        if (new_max <= clip->min_time)
            new_max = clip->min_time + min_length;
        clip->max_time = new_max;
    }
    update(clip, beat_duration);
}

void Track::delete_clip(uint32_t id) {
    auto iter = clips.begin() + id;
    (*iter)->~Clip();
    clip_allocator.free(*iter);
    clips.erase(iter);
    update(nullptr, 0.0);
}

void Track::update(Clip* updated_clip, double beat_duration) {
    std::sort(clips.begin(), clips.end(),
              [](const Clip* a, const Clip* b) { return a->min_time < b->min_time; });

    // Trim overlapping clips or delete if completely overlapped
    if (updated_clip) {
        std::vector<Clip*> deleted_clips;
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
                            clip->audio.min_sample_pos = beat_to_samples(
                                clip->relative_start_time,
                                (double)asset->sample_instance.sample_rate, beat_duration);
                        }
                    }
                }
            }

            // The clip is completely overlapped, delete this!
            if (clip->min_time >= clip->max_time)
                deleted_clips.push_back(clip);
        }

        if (!deleted_clips.empty()) {
            std::vector<Clip*> new_clip_list;
            uint32_t i = 0;
            new_clip_list.reserve(clips.size());
            for (auto clip : clips) {
                if (i < deleted_clips.size() && clip == deleted_clips[i]) {
                    clip->~Clip();
                    clip_allocator.free(clip);
                    i++;
                    continue;
                }
                new_clip_list.push_back(clip);
            }
            clips = std::move(new_clip_list);
        }
    }

    for (uint32_t i = 0; i < (uint32_t)clips.size(); i++) {
        clips[i]->id = i;
    }

#if WB_LOG_CLIP_ORDERING
    Log::info("--- Clip Ordering ---");
    for (auto clip : clips) {
        Log::info("{:x}: {} ({}, {}, {} -> {})", (uint64_t)clip, clip->name, clip->id,
                  clip->relative_start_time, clip->min_time, clip->max_time);
    }
#endif
}

Clip* Track::find_next_clip(double time_pos, uint32_t hint) {
    auto begin = clips.begin();
    auto end = clips.end();
    while (begin != end && time_pos > (*begin)->min_time)
        begin++;
    return *begin;
}

void Track::prepare_play(double time_pos) {
    Clip* next_clip = find_next_clip(time_pos);
    playback_state.current_clip = nullptr;
    playback_state.next_clip = next_clip;
}

void Track::process_event(uint32_t buffer_offset, double time_pos, double beat_duration,
                          double sample_rate) {
    if (clips.size() == 0)
        return;

    Clip* current_clip = playback_state.current_clip;
    Clip* next_clip = playback_state.next_clip;

    if (current_clip) {
        if (time_pos >= current_clip->max_time) {
            event_queue.push({
                .type = EventType::AudioEnd,
                .audio =
                    {
                        .time = time_pos,
                        .buffer_offset = buffer_offset,
                        .clip = current_clip,
                    },
            });
            playback_state.current_clip = nullptr;
        }
    }

    if (current_clip == nullptr && next_clip) {
        assert(next_clip->type == ClipType::Audio);
        if (time_pos >= next_clip->min_time && time_pos < next_clip->max_time) {
            double relative_start_time = time_pos - next_clip->min_time;
            uint64_t sample_offset =
                (uint64_t)(beat_to_samples(relative_start_time, sample_rate, beat_duration));

            event_queue.push({
                .type = EventType::AudioStart,
                .audio =
                    {
                        .time = time_pos,
                        .sample_offset = sample_offset,
                        .buffer_offset = buffer_offset,
                        .clip = next_clip,
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

} // namespace wb